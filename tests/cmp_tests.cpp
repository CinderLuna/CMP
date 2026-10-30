#include "cmp/Circuit.h"
#include "cmp/ElectricalDiagnostics.h"
#include "cmp/Simulator.h"
#include <cmath>
#include <iostream>
using namespace cmp;
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    int failed = 0;
    QJsonArray report;
    auto check = [&](QString name, bool pass, double measured = 0)
    {
        std::cout << (pass ? "PASS " : "FAIL ") << name.toStdString() << " " << measured << "\n";
        if (!pass)
            failed++;
        report.append(QJsonObject{{"test", name}, {"pass", pass}, {"measured", measured}});
    };
    Simulator engine;
    Samples samples;
    QStringList messages;
    bool success = false;
    QObject::connect(&engine, &Simulator::samplesReady, &app, [&](Samples a) { samples += a; });
    QObject::connect(&engine, &Simulator::message, &app, [&](QString m) { messages << m; });
    auto run = [&](const Circuit &c)
    {
        samples.clear();
        messages.clear();
        success = false;
        QEventLoop loop;
        QTimer timeout;
        timeout.setSingleShot(true);
        auto conn = QObject::connect(&engine, &Simulator::completed, &loop,
                                     [&](bool ok)
                                     {
                                         success = ok;
                                         loop.quit();
                                     });
        QObject::connect(&timeout, &QTimer::timeout, &loop,
                         [&]
                         {
                             engine.stop();
                             loop.quit();
                         });
        timeout.start(15000);
        engine.start(c.netlist(), false);
        loop.exec();
        QObject::disconnect(conn);
        if (!success)
            for (auto &m : messages)
                std::cerr << m.toStdString() << "\n";
        return success;
    };
    auto value = [&](const Circuit &c, const Sample &s, QString probe)
    {
        QString node = c.nodes().value(c.probes[probe].key());
        return node == "0" ? 0 : s.values.value(node, s.values.value("v(" + node + ")"));
    };
    Circuit c = Circuit::blinker();
    check("A00 fixture validation", c.validate().isEmpty());
    check("A04 solver completes", run(c) && samples.size() > 1000);
    Samples baseline = samples;
    const auto normalReport = diagnoseElectrical(c, baseline);
    check("Electrical normal blinker has no violations or incomplete checks",
          normalReport.issues.isEmpty() && normalReport.incomplete.isEmpty());
    for (const auto &line : normalReport.lines(true))
        std::cout << line.toStdString() << "\n";
    auto timing = [&](const Circuit &circuit, const Samples &data)
    {
        QVector<double> up, down;
        for (int i = 1; i < data.size(); i++)
        {
            double prev = value(circuit, data[i - 1], "out"), now = value(circuit, data[i], "out");
            if (prev < 2.3 && now >= 2.3)
                up << data[i].time;
            if (prev >= 2.3 && now < 2.3)
                down << data[i].time;
        }
        double period = up.size() > 2 ? (up.back() - up[0]) / (up.size() - 1) : 0;
        double high = 0, low = 0;
        int count = 0;
        for (int i = 0; i + 1 < up.size(); i++)
            for (double d : down)
                if (d > up[i] && d < up[i + 1])
                {
                    high += d - up[i];
                    low += up[i + 1] - d;
                    count++;
                    break;
                }
        return QVector<double>{period, count ? high / count : 0, count ? low / count : 0, double(up.size())};
    };
    auto times = timing(c, baseline);
    check("A04 four steady cycles", times[3] >= 4, times[3]);
    double expected = std::log(2.) * 146000 * 1e-5;
    check("A05 period within 5 percent", std::abs(times[0] - expected) < expected * .05, times[0]);
    check("A05 high time", std::abs(times[1] - .54065) < .0271, times[1]);
    check("A05 low time", std::abs(times[2] - .47134) < .0236, times[2]);
    double lo = 5, hi = 0, on = 0, off = 1;
    for (auto &s : baseline)
        if (s.time > 1.5)
        {
            double v = value(c, s, "timing");
            lo = qMin(lo, v);
            hi = qMax(hi, v);
            double current = s.values.value("vi_d1#branch");
            if (value(c, s, "out") > 2.3)
                on = qMax(on, current);
            else
                off = qMin(off, current);
        }
    check("A06 timing thresholds", std::abs(lo - 5. / 3) < .15 && std::abs(hi - 10. / 3) < .15, lo);
    check("A07 LED on current", on > .0005, on);
    check("A07 LED off current", std::abs(off) < 1e-5, off);
    c.part("C1")->value = 20e-6;
    c.duration = 12;
    check("A08 doubled capacitance solver", run(c));
    double twice = timing(c, samples)[0];
    check("A08 doubled period", std::abs(twice / (times[0] * 2) - 1) < .05, twice);
    QDir().mkpath("work");
    {
        QFile f("work/doubled-debug.csv");
        f.open(QIODevice::WriteOnly);
        QTextStream out(&f);
        out << "time,out,timing,iled\n";
        for (auto &s : samples)
            out << QString::number(s.time, 'g', 15) << "," << value(c, s, "out") << ","
                << value(c, s, "timing") << "," << s.values.value("vi_d1#branch") << "\n";
    }
    c = Circuit::rc();
    check("A02 RC solver", run(c));
    double best = 100, vc = 0;
    for (auto &s : samples)
        if (std::abs(s.time - .1) < best)
        {
            best = std::abs(s.time - .1);
            vc = value(c, s, "capacitor");
        }
    check("A02 RC time constant", std::abs(vc - 5 * (1 - std::exp(-1))) < .0633, vc);
    c = Circuit::transistor();
    check("A03 transistor on solver", run(c));
    double collector = samples.isEmpty() ? 99 : value(c, samples.back(), "collector");
    check("A03 transistor on", collector < .3, collector);
    c.part("V2")->value = 0;
    check("A03 transistor off solver", run(c));
    collector = samples.isEmpty() ? 0 : value(c, samples.back(), "collector");
    check("A03 transistor off", collector > 4.5, collector);
    c = Circuit::blinker();
    c.parts.append({"VRESET", "PULSE", 5, 0, {0, 0}}); // Hold reset low for 0.2s then release.
    for (int i = c.wires.size() - 1; i >= 0; i--)
        if (c.wires[i].a == Pin{"U1", 3} || c.wires[i].b == Pin{"U1", 3})
            c.wires.remove(i);
    c.wires.append(Wire{{"VRESET", 0}, {"U1", 3}});
    c.wires.append(Wire{{"VRESET", 1}, {"G1", 0}});
    check("A09 reset solver", run(c));
    bool held = true, released = false;
    for (auto &s : samples)
    {
        if (s.time > .01 && s.time < .19)
            held &= value(c, s, "out") < .4;
        if (s.time > .21 && value(c, s, "out") > 2.3)
            released = true;
    }
    check("A09 reset hold/release", held && released);
    // A one-shot uses separate trigger and threshold pins, not astable-only shortcuts.
    c = Circuit::blinker();
    c.parts.removeIf([](const Part &p) { return p.id == "RB"; });
    c.part("RA")->value = 100000;
    c.parts.append(
        {"VTRIG", "PULSE", 0, 0, {100, 400}, 0, {{"low", 5}, {"delay", .1}, {"width", .01}, {"period", 20}}});
    c.wires.clear();
    auto net = [&](std::initializer_list<Pin> pins)
    {
        auto it = pins.begin();
        Pin first = *it++;
        for (; it != pins.end(); ++it)
            c.wires.append(Wire{first, *it});
    };
    net({{"V1", 0}, {"RA", 0}, {"U1", 7}, {"U1", 3}, {"CB", 0}});
    net({{"RA", 1}, {"C1", 0}, {"U1", 5}, {"U1", 6}});
    net({{"VTRIG", 0}, {"U1", 1}});
    net({{"U1", 4}, {"CC", 0}});
    net({{"U1", 2}, {"RL", 0}});
    net({{"RL", 1}, {"D1", 0}});
    net({{"G1", 0}, {"V1", 1}, {"VTRIG", 1}, {"C1", 1}, {"CC", 1}, {"CB", 1}, {"U1", 0}, {"D1", 1}});
    c.duration = 3;
    check("monostable circuit valid", c.validate().isEmpty());
    check("monostable solver", run(c));
    double startPulse = 0, stopPulse = 0;
    for (int i = 1; i < samples.size(); ++i)
    {
        if (value(c, samples[i - 1], "out") < 2.3 && value(c, samples[i], "out") >= 2.3)
            startPulse = samples[i].time;
        if (startPulse && value(c, samples[i - 1], "out") >= 2.3 && value(c, samples[i], "out") < 2.3)
        {
            stopPulse = samples[i].time;
            break;
        }
    }
    check("monostable pulse width", std::abs(stopPulse - startPulse - std::log(3.)) < .055,
          stopPulse - startPulse);
    c = Circuit::rc();
    c.part("C1")->kind = "R";
    c.part("C1")->value = 10000;
    c.operatingPoint = true;
    check("DC operating point solver", run(c));
    check("DC divider voltage",
          !samples.isEmpty() && std::abs(value(c, samples.back(), "capacitor") - 2.5) < .001);
    c = Circuit::blinker();
    QString error;
    Circuit copy;
    check("A10 JSON roundtrip", Circuit::parse(c.json(), copy, error) && copy.json() == c.json());
    auto bad = c.json();
    bad["version"] = 99;
    check("reject future version", !Circuit::parse(bad, copy, error));
    Document document;
    document.edit("change C1", [](Circuit &d) { d.part("C1")->value = 20e-6; });
    document.undo.undo();
    check("undo parameter", document.circuit.part("C1")->value == 1e-5);
    document.undo.redo();
    check("redo parameter", document.circuit.part("C1")->value == 20e-6);
    error.clear();
    QDir().mkpath("work");
    QTemporaryDir dir(QDir::currentPath() + "/work/test-XXXXXX");
    QString path = dir.filePath("test.cmp");
    check("atomic project save", document.save(path, error));
    if (!error.isEmpty())
        std::cerr << error.toStdString() << "\n";
    Document reopened;
    check("A10 disk roundtrip",
          reopened.load(path, error) && reopened.circuit.json() == document.circuit.json());
    auto persisted = reopened.circuit;
    persisted.duration = 12;
    check("A10 rerun saved project", run(persisted));
    double savedPeriod = timing(persisted, samples)[0];
    check("A10 deterministic period", std::abs(savedPeriod / twice - 1) < .001, savedPeriod);
    auto connections = persisted.nodes();
    persisted.part("U1")->position = {100, 100};
    check("moving preserves electrical nets", persisted.nodes() == connections);
    auto invalid = c.json();
    invalid["futureField"] = true;
    check("reject unknown project fields", !Circuit::parse(invalid, copy, error));
    c = Circuit::blinker();
    c.parts.removeLast();
    check("A11 missing ground", !c.validate().isEmpty());
    c = Circuit::blinker();
    c.part("C1")->value = -1;
    check("A11 negative capacitance", !c.validate().isEmpty());
    c = Circuit::blinker();
    c.wires.append(Wire{{"V1", 0}, {"V1", 1}});
    check("A11 shorted source", !c.validate().isEmpty());
    double parsed = 0;
    check("SI units", parseQuantity("10 uF", parsed) && std::abs(parsed - 1e-5) < 1e-12 &&
                          !parseQuantity("10 garbage", parsed));
    c = Circuit::blinker();
    c.operatingPoint = true;
    c.part("V1")->value = 500;
    const bool highVoltageSolved = run(c);
    const auto highVoltageReport = diagnoseElectrical(c, samples);
    bool supplyViolation = false;
    for (const auto &issue : highVoltageReport.issues)
        supplyViolation |= issue.component == "U1" && issue.rule == "Supply maximum" && issue.peak > 499;
    check("Electrical 500 V blinker identifies U1 supply violation", highVoltageSolved && supplyViolation);
    c = Circuit::transistor();
    check("Electrical transistor sensors solve", run(c));
    const auto transistorReport = diagnoseElectrical(c, samples);
    check("Electrical transistor measurement coverage", transistorReport.incomplete.isEmpty());
    // Pause at a native accepted timestep, retain partial data, and resume the same analysis.
    c = Circuit::rc();
    c.wires.removeFirst();
    c.labels = {{"SUPPLY", {"V1", 0}}, {"Supply", {"R1", 0}}};
    auto labelled = c.nodes();
    check("named nets connect pins",
          labelled[Pin{"V1", 0}.key()] == labelled[Pin{"R1", 0}.key()] && c.validate().isEmpty());
    check("net label persistence", Circuit::parse(c.json(), copy, error) && copy.json() == c.json());
    c.labels[1].name = "Other";
    labelled = c.nodes();
    check("different labels remain separate",
          labelled[Pin{"V1", 0}.key()] != labelled[Pin{"R1", 0}.key()] && !c.validate().isEmpty());
    samples.clear();
    success = false;
    QEventLoop controlLoop;
    QTimer watchdog;
    watchdog.setSingleShot(true);
    bool paused = false, stable = false;
    int afterPause = 0;
    auto finished = QObject::connect(&engine, &Simulator::completed, &controlLoop,
                                     [&](bool ok)
                                     {
                                         success = ok;
                                         controlLoop.quit();
                                     });
    QObject::connect(&watchdog, &QTimer::timeout, &controlLoop, &QEventLoop::quit);
    watchdog.start(5000);
    c = Circuit::rc();
    engine.start(c.netlist(), true);
    QTimer::singleShot(100, &controlLoop,
                       [&]
                       {
                           engine.pause();
                           paused = true;
                           QTimer::singleShot(80, &controlLoop,
                                              [&]
                                              {
                                                  afterPause = samples.size();
                                                  QTimer::singleShot(80, &controlLoop,
                                                                     [&]
                                                                     {
                                                                         stable =
                                                                             samples.size() == afterPause;
                                                                         engine.resume();
                                                                     });
                                              });
                       });
    controlLoop.exec();
    QObject::disconnect(finished);
    check("A12 native pause/resume", paused && stable && success);
    QDir().mkpath("work");
    QFile output("work/acceptance-results.json");
    output.open(QIODevice::WriteOnly);
    output.write(QJsonDocument(QJsonObject{{"engine", "ngspice 47"},
                                           {"model", "CMP functional v0.1"},
                                           {"tests", report},
                                           {"failures", failed}})
                     .toJson());
    return failed ? 1 : 0;
}
