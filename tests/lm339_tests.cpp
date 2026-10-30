#include "cmp/Circuit.h"
#include "cmp/ElectricalDiagnostics.h"
#include "cmp/PackageArtwork.h"
#include "cmp/Recovery.h"
#include "cmp/Simulator.h"
#include <QtWidgets>
#include <iostream>
using namespace cmp;
static Circuit fixture(const QVector<double> &input)
{
    Circuit c = Circuit::comparator();
    c.operatingPoint = true;
    c.parts.erase(std::remove_if(c.parts.begin(), c.parts.end(), [](const Part &p) { return p.id == "IN"; }),
                  c.parts.end());
    c.wires.erase(std::remove_if(c.wires.begin(), c.wires.end(),
                                 [](const Wire &w) { return w.a.part == "IN" || w.b.part == "IN"; }),
                  c.wires.end());
    c.probes.remove("input");
    const QVector<int> positive{6, 4, 8, 10};
    for (int i = 0; i < 4; ++i)
    {
        const auto id = "VIN" + QString::number(i + 1);
        c.parts.append({id, "V", input[i], 0, {0, double(i * 100)}});
        c.wires.append(Wire{{id, 0}, {"U1", positive[i]}});
        c.wires.append(Wire{{id, 1}, {"G1", 0}});
    }
    return c;
}
int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    int failed = 0;
    auto check = [&](const char *name, bool pass)
    {
        std::cout << (pass ? "PASS " : "FAIL ") << name << "\n";
        failed += !pass;
    };
    Simulator engine;
    Samples samples;
    QStringList messages;
    QObject::connect(&engine, &Simulator::samplesReady, &app, [&](Samples batch) { samples += batch; });
    QObject::connect(&engine, &Simulator::message, &app, [&](QString line) { messages << line; });
    auto run = [&](const Circuit &c)
    {
        samples.clear();
        messages.clear();
        bool ok = false;
        QEventLoop loop;
        QTimer watchdog;
        watchdog.setSingleShot(true);
        auto connection = QObject::connect(&engine, &Simulator::completed, &loop,
                                           [&](bool result)
                                           {
                                               ok = result;
                                               loop.quit();
                                           });
        QObject::connect(&watchdog, &QTimer::timeout, &loop,
                         [&]
                         {
                             engine.stop();
                             loop.quit();
                         });
        watchdog.start(10000);
        engine.start(c.netlist(), false);
        loop.exec();
        QObject::disconnect(connection);
        if (!ok)
            std::cerr << messages.join("\n").toStdString() << "\n";
        return ok;
    };
    auto value = [&](const Circuit &c, int pin)
    {
        return samples.isEmpty() ? -999.
                                 : samples.back().values.value(c.nodes().value(Pin{"U1", pin}.key()), -999.);
    };
    auto issue = [](const ElectricalReport &r, const QString &rule)
    {
        for (const auto &i : r.issues)
            if (i.component == "U1" && i.rule == rule)
                return true;
        return false;
    };
    const auto &specification = spec("LM339");
    check("LM339 package and model identity", specification.pins.size() == 14 && specification.sprite < 0 &&
                                                  specification.model == "CMP_LM339" &&
                                                  specification.package.contains("PDIP-14"));
    const QVector<int> outs{0, 1, 13, 12}, negatives{5, 3, 7, 9}, positives{6, 4, 8, 10};
    bool mapping = specification.comparators.size() == 4;
    for (int i = 0; i < 4 && mapping; ++i)
        mapping &= specification.comparators[i].output == outs[i] &&
                   specification.comparators[i].negative == negatives[i] &&
                   specification.comparators[i].positive == positives[i];
    check("classic 14-pin datasheet channel mapping", mapping);
    bool truth = true, coverage = true;
    for (int mask = 0; mask < 16; ++mask)
    {
        QVector<double> input;
        for (int i = 0; i < 4; ++i)
            input << (mask & (1 << i) ? 3. : 2.);
        auto c = fixture(input);
        truth &= c.validate().isEmpty() && run(c);
        for (int i = 0; i < 4; ++i)
            truth &= (mask & (1 << i)) ? value(c, outs[i]) > 4.5
                                       : value(c, outs[i]) < .4 && value(c, outs[i]) >= 0;
        const auto report = diagnoseElectrical(c, samples);
        coverage &= report.issues.isEmpty() && report.incomplete.isEmpty();
    }
    check("all 16 independent four-channel truth patterns", truth);
    check("normal channel patterns have complete clean diagnostics", coverage);
    auto c = fixture({2, 3, 2, 3});
    for (auto &w : c.wires)
        for (auto *p : {&w.a, &w.b})
            if (p->part == "U1")
            {
                if (p->index == 5)
                    p->index = 6;
                else if (p->index == 6)
                    p->index = 5;
            }
    check("swapping physical inputs reverses channel 1 only",
          run(c) && value(c, 0) > 4.5 && value(c, 1) > 4.5 && value(c, 13) < .4 && value(c, 12) > 4.5);
    c = fixture({3, 3, 3, 3});
    c.part("R1")->value = 1e6;
    check("released output follows weak external high bias", run(c) && value(c, 0) > 4.5);
    for (auto &w : c.wires)
        if (w.a == Pin{"R1", 0})
            w.b = {"G1", 0};
    check("released output follows weak external low bias without actively driving high",
          run(c) && std::abs(value(c, 0)) < .1);
    c = fixture({3, 3, 3, 3});
    c.wires.erase(std::remove_if(c.wires.begin(), c.wires.end(),
                                 [](const Wire &w) { return w.a.part == "R1" || w.b.part == "R1"; }),
                  c.wires.end());
    c.parts.erase(std::remove_if(c.parts.begin(), c.parts.end(), [](const Part &p) { return p.id == "R1"; }),
                  c.parts.end());
    check("unused open-collector output allowed without hidden bias",
          c.validate().isEmpty() && !c.hasExternalBias({"U1", 0}));
    check("floating output can solve but is explicitly unchecked",
          run(c) && diagnoseElectrical(c, samples).incomplete.join(" ").contains("no recognized external"));
    c = fixture({2, 3, 3, 3});
    c.wires.append(Wire{{"U1", 0}, {"U1", 1}});
    check("wired-OR output sinks when either comparator requests low",
          run(c) && value(c, 0) < .4 && value(c, 1) < .4);
    c.part("VIN1")->value = 3;
    check("wired-OR output releases only when both release",
          run(c) && value(c, 0) > 4.5 && value(c, 1) > 4.5);
    c = fixture({2, 2, 2, 2});
    c.parts.append({"BIAS", "V", 5, 0, {}});
    c.wires.append(Wire{{"BIAS", 1}, {"G1", 0}});
    for (auto &w : c.wires)
        if (w.a.part.startsWith('R') && w.a.index == 0)
            w.b = {"BIAS", 0};
    c.part("VCC")->value = 0;
    bool disabled = run(c);
    for (int output : outs)
        disabled &= value(c, output) > 4.5;
    check("shared supply disables sinking on all four channels",
          disabled && issue(diagnoseElectrical(c, samples), "Supply below recommended range"));
    c = fixture({2, 2, 2, 2});
    c.wires.erase(std::remove_if(c.wires.begin(), c.wires.end(),
                                 [](const Wire &w) { return w.a == Pin{"VCC", 0} && w.b == Pin{"U1", 2}; }),
                  c.wires.end());
    check("missing actual VCC pin is not hidden-powered", c.validate().join(" ").contains("3 VCC"));
    c = fixture({5, 6, 5, 6});
    c.part("VCC")->value = 8;
    c.part("REF")->value = 5.5;
    c.parts.append({"OFFSET", "V", 3, 0, {}});
    c.wires.append(Wire{{"OFFSET", 1}, {"G1", 0}});
    for (auto &w : c.wires)
        if (w.a == Pin{"U1", 11})
            w.b = {"OFFSET", 0};
    const bool offsetSolved = run(c);
    const auto offsetReport = diagnoseElectrical(c, samples);
    if (!offsetSolved || !offsetReport.issues.isEmpty())
        for (const auto &line : offsetReport.lines(offsetSolved))
            std::cerr << line.toStdString() << "\n";
    std::cout << "offset outputs " << value(c, 0) << " " << value(c, 1) << "\n";
    check("comparators use actual package ground not global ground",
          offsetSolved && value(c, 0) > 3 && value(c, 0) < 3.4 && value(c, 1) > 7.5 &&
              offsetReport.issues.isEmpty());
    c = fixture({2, 2, 2, 2});
    c.part("VCC")->value = 31;
    check("31 V recommended range distinct from absolute maximum",
          run(c) && issue(diagnoseElectrical(c, samples), "Supply above recommended range") &&
              !issue(diagnoseElectrical(c, samples), "Supply maximum"));
    c.part("VCC")->value = 40;
    check("supply absolute maximum diagnostic",
          run(c) && issue(diagnoseElectrical(c, samples), "Supply maximum"));
    c = fixture({3.8, 2, 2, 2});
    check("input common-mode diagnostic identifies physical input",
          run(c) && issue(diagnoseElectrical(c, samples), "Channel 1 pin 7 input common-mode headroom"));
    c = fixture({-.4, 2, 2, 2});
    check("negative input absolute limit diagnostic",
          run(c) && issue(diagnoseElectrical(c, samples), "Channel 1 pin 7 input below absolute minimum"));
    c = fixture({2, 2, 2, 2});
    c.part("R1")->value = 10;
    check("measured output sink overcurrent diagnostic",
          run(c) && issue(diagnoseElectrical(c, samples), "Channel 1 output current"));
    c = fixture({3, 3, 3, 3});
    c.parts.append({"BIAS", "V", 50, 0, {}});
    c.wires.append(Wire{{"BIAS", 1}, {"G1", 0}});
    for (auto &w : c.wires)
        if (w.a == Pin{"R1", 0})
            w.b = {"BIAS", 0};
    check("external output pull-up voltage has its own absolute limit",
          run(c) && issue(diagnoseElectrical(c, samples), "Channel 1 output voltage"));
    c = Circuit::blinker();
    c.duration = .02;
    c.parts.append({"OFFSET", "V", 3, 0, {}});
    c.wires.append(Wire{{"OFFSET", 1}, {"G1", 0}});
    for (auto &w : c.wires)
        for (auto *pin : {&w.a, &w.b})
            if (pin->part == "G1" && pin->index == 0 && w.a.part != "OFFSET")
                *pin = {"OFFSET", 0};
    const bool timerOffsetSolved = run(c);
    const auto timerNodes = c.nodes();
    double minimumOutput = 1e9;
    for (const auto &sample : samples)
        minimumOutput = qMin(minimumOutput, sample.values.value(timerNodes.value(Pin{"U1", 2}.key())));
    check("existing 555 model also respects its actual offset package ground",
          timerOffsetSolved && minimumOutput >= 3 && diagnoseElectrical(c, samples).issues.isEmpty());
    c = Circuit::comparator();
    check("threshold example validates and runs transient", c.validate().isEmpty() && run(c));
    bool pulseLow = false, pulseHigh = false;
    const auto outputNode = c.nodes().value(Pin{"U1", 0}.key());
    for (const auto &sample : samples)
    {
        if (sample.time > .0005 && sample.time < .0015)
            pulseLow |= sample.values.value(outputNode) > 0 && sample.values.value(outputNode) < .4;
        if (sample.time > .003 && sample.time < .006)
            pulseHigh |= sample.values.value(outputNode) > 4.5;
    }
    check("transient threshold crossings produce low and released high", pulseLow && pulseHigh);
    check("threshold example diagnostic coverage", diagnoseElectrical(c, samples).issues.isEmpty() &&
                                                       diagnoseElectrical(c, samples).incomplete.isEmpty());
    auto missing = samples;
    for (auto &s : missing)
        s.values.remove("voc_u1_1#branch");
    check("missing output current vector never silently passes",
          !diagnoseElectrical(c, missing).incomplete.isEmpty());
    Circuit restored;
    QString error;
    c.part("U1")->ratings["current"] = .01;
    c.part("U1")->rotation = 90;
    check("LM339 model package properties persist",
          Circuit::parse(c.json(), restored, error) && restored.json() == c.json() &&
              spec(restored.part("U1")->kind).package == specification.package);
    Document doc;
    doc.replace(c);
    doc.edit("Rotate comparator", [](Circuit &c) { c.part("U1")->rotation = 180; });
    doc.undo.undo();
    check("package rotation undo", doc.circuit.part("U1")->rotation == 90);
    doc.undo.redo();
    check("package rotation redo", doc.circuit.part("U1")->rotation == 180);
    QDir().mkpath("work");
    QTemporaryDir temp(QDir::current().filePath("work/lm339-tests-XXXXXX"));
    {
        Recovery previous(&doc, temp.path());
        check("LM339 recovery snapshot writes", previous.flush());
    }
    Document recovered;
    Recovery recovery(&recovered, temp.path());
    QStringList errors;
    auto snapshots = recovery.available(errors);
    check("LM339 recovery restores full package and rating",
          snapshots.size() == 1 && recovery.restore(snapshots[0]) &&
              recovered.circuit.json() == doc.circuit.json() && !recovered.undo.isClean());
    recovery.closeSession();
    QImage artwork(184, 336, QImage::Format_ARGB32_Premultiplied);
    artwork.fill(Qt::transparent);
    QPainter painter(&artwork);
    painter.scale(2, 2);
    painter.translate(46, 84);
    paintDipPackage(painter, "LM339", 14);
    painter.end();
    bool anchors = true;
    for (int i = 0; i < 14; ++i)
    {
        const auto pin = (dipPinPosition(i, 14) + QPointF(46, 84)) * 2;
        anchors &= artwork.pixelColor(qRound(pin.x()), qRound(pin.y())).alpha() > 128;
    }
    check("native DIP artwork has metal at every one of its 14 pin anchors", anchors);
    check("native library icon renders", !dipIcon("LM339", 14).isNull());
    return failed ? 1 : 0;
}
