#include "cmp/Circuit.h"
#include "cmp/ElectricalDiagnostics.h"
#include "cmp/ExpandedLibrary.h"
#include "cmp/Recovery.h"
#include "cmp/Simulator.h"
#include <QtCore>
#include <cmath>
#include <iostream>
using namespace cmp;
static Circuit divider(QString kind, double resistance = 10000)
{
    Circuit c;
    c.operatingPoint = true;
    c.parts = {{"V1", "V", 5, 0, {}},
               {"R1", "R", resistance, 0, {}},
               {"X1", kind, spec(kind).value, 0, {}},
               {"G1", "GND", 0, 0, {}}};
    c.wires = {
        {{"V1", 0}, {"R1", 0}}, {{"R1", 1}, {"X1", 0}}, {{"X1", 1}, {"G1", 0}}, {{"V1", 1}, {"G1", 0}}};
    return c;
}
static Circuit source(QString kind, double resistance = 1000)
{
    Circuit c;
    c.parts = {
        {"X1", kind, spec(kind).value, 0, {}}, {"R1", "R", resistance, 0, {}}, {"G1", "GND", 0, 0, {}}};
    c.wires = {{{"X1", 0}, {"R1", 0}}, {{"X1", 1}, {"G1", 0}}, {{"R1", 1}, {"G1", 0}}};
    c.duration = .02;
    c.step = .00001;
    return c;
}
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    int failed = 0;
    auto check = [&](const char *name, bool ok)
    {
        std::cout << (ok ? "PASS " : "FAIL ") << name << "\n";
        failed += !ok;
    };
    Simulator engine;
    Samples samples;
    QStringList messages;
    QObject::connect(&engine, &Simulator::samplesReady, &app, [&](Samples s) { samples += s; });
    QObject::connect(&engine, &Simulator::message, &app, [&](QString s) { messages << s; });
    auto run = [&](const Circuit &c)
    {
        samples.clear();
        messages.clear();
        bool ok = false;
        QEventLoop loop;
        QTimer watchdog;
        watchdog.setSingleShot(true);
        auto connection = QObject::connect(&engine, &Simulator::completed, &loop,
                                           [&](bool v)
                                           {
                                               ok = v;
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
            std::cerr << messages.join("\n").toStdString();
        return ok;
    };
    auto value = [&](const Circuit &c, const Sample &s, int a = 0, int b = 1)
    {
        const auto map = c.nodes();
        auto read = [&](int pin)
        {
            const auto node = map.value(Pin{"X1", pin}.key());
            return node == "0" ? 0. : s.values.value(node, -1e10);
        };
        return read(a) - read(b);
    };
    auto last = [&](const Circuit &c, int a = 0, int b = 1)
    { return samples.isEmpty() ? -1e20 : value(c, samples.back(), a, b); };
    auto c = divider("LDR");
    c.part("X1")->parameters["lux"] = 100;
    bool ldr = run(c) && std::abs(last(c) - 2.5) < .01;
    c.part("X1")->parameters["lux"] = 0;
    ldr &= run(c) && last(c) > 4.99;
    c.part("X1")->parameters["lux"] = 10000;
    check("photoresistor follows declared light/dark law", ldr && run(c) && last(c) < .25);
    c = divider("NTC");
    bool ntc = true;
    for (double temperature : {0., 25., 50.})
    {
        c.part("X1")->parameters["temperature"] = temperature;
        double r = 10000 * std::exp(3950 * (1 / (temperature + 273.15) - 1 / 298.15));
        ntc &= run(c) && std::abs(last(c) - 5 * r / (10000 + r)) < .01;
    }
    check("NTC matches beta law at three temperatures", ntc);
    c = divider("PTC", 100);
    bool ptc = true;
    for (double temperature : {0., 25., 50.})
    {
        c.part("X1")->parameters["temperature"] = temperature;
        double r = 100 * (1 + .004 * (temperature - 25));
        ptc &= run(c) && std::abs(last(c) - 5 * r / (100 + r)) < .01;
    }
    check("PTC matches declared linear law", ptc);
    c = divider("PHOTODIODE");
    c.wires = {
        {{"V1", 0}, {"R1", 0}}, {{"R1", 1}, {"X1", 1}}, {{"X1", 0}, {"G1", 0}}, {{"V1", 1}, {"G1", 0}}};
    c.part("X1")->parameters["lux"] = 100;
    bool photo = run(c) && std::abs(samples.back().values.value(currentKey(*c.part("X1"))) + .0001) < 1e-7;
    c.part("X1")->parameters["lux"] = 0;
    check("photodiode photocurrent, polarity and dark state",
          photo && run(c) && std::abs(samples.back().values.value(currentKey(*c.part("X1")))) < 1e-8);
    c = divider("PHOTOTRANS");
    c.part("X1")->parameters["lux"] = 100;
    bool transistor = run(c) && std::abs(last(c) - 4) < .01;
    c.part("X1")->parameters["lux"] = 10000;
    check("phototransistor response saturates under load", transistor && run(c) && last(c) < .1);
    c = source("SENSOR", 1e9);
    c.parts.append({"V1", "V", 5, 0, {}});
    c.wires = {{{"V1", 0}, {"X1", 0}},
               {{"V1", 1}, {"G1", 0}},
               {{"X1", 1}, {"G1", 0}},
               {{"X1", 2}, {"R1", 0}},
               {{"R1", 1}, {"G1", 0}}};
    c.operatingPoint = true;
    c.part("X1")->ratings["power"] = .1;
    check("generic sensor interpolates configured linear transfer",
          run(c) && std::abs(last(c, 2, 1) - 2.5) < .01);
    c.part("X1")->data = "0, 0.1\n10, 1\n20, 4";
    c.part("X1")->dataType = "curve";
    c.part("X1")->parameters["stimulus"] = 15;
    check("generic sensor supports persisted custom transfer points",
          run(c) && std::abs(last(c, 2, 1) - 2.5) < .01);
    c.part("R1")->value = 1000;
    check("sensor output impedance affects loaded voltage",
          run(c) && std::abs(last(c, 2, 1) - 2.5 / 1.1) < .01);
    Circuit copy;
    QString error;
    check("sensor curve payload round-trips", Circuit::parse(c.json(), copy, error) &&
                                                  copy.json() == c.json() &&
                                                  copy.part("X1")->data == c.part("X1")->data);
    c.part("X1")->parameters["stimulus"] = 100;
    check("out-of-curve stimulus stays explicitly unchecked",
          run(c) && diagnoseElectrical(c, samples).incomplete.join(" ").contains("outside the declared"));
    c = source("PWL");
    c.part("X1")->data = "0 0\n.01 5\n.02 0";
    c.part("X1")->dataType = "pwl";
    bool pwl = run(c);
    double errorMaximum = 0;
    for (const auto &s : samples)
    {
        double expected = s.time <= .01 ? 500 * s.time : 5 - 500 * (s.time - .01);
        errorMaximum = qMax(errorMaximum, std::abs(value(c, s) - expected));
    }
    check("PWL source interpolates exact user points", pwl && errorMaximum < 1e-6);
    check("PWL payload survives project round-trip",
          Circuit::parse(c.json(), copy, error) && copy.part("X1")->data == c.part("X1")->data);
    auto invalid = c.json();
    auto parts = invalid["parts"].toArray();
    auto entry = parts[0].toObject();
    entry["data"] = "not base64!";
    parts[0] = entry;
    invalid["parts"] = parts;
    check("invalid payload encoding rejected", !Circuit::parse(invalid, copy, error));
    c.part("X1")->data = ".01 5\n0 0";
    check("backward waveform times rejected", !validateExpanded(*c.part("X1")).isEmpty());
    c = source("RAMP");
    c.part("X1")->parameters = {{"delay", .001}, {"rise", .005}};
    bool ramp = run(c);
    errorMaximum = 0;
    for (const auto &s : samples)
        errorMaximum = qMax(errorMaximum, std::abs(value(c, s) - 5 * qBound(0., (s.time - .001) / .005, 1.)));
    check("ramp source follows delay/rise/hold", ramp && errorMaximum < 1e-6);
    c = source("TRIANGLE");
    c.duration = .04;
    bool triangle = run(c);
    double peak = -100, trough = 100;
    for (const auto &s : samples)
    {
        peak = qMax(peak, value(c, s));
        trough = qMin(trough, value(c, s));
    }
    check("triangle source reaches configured signed peaks",
          triangle && std::abs(peak - 5) < .02 && std::abs(trough + 5) < .02);
    c = source("SQUARE");
    c.duration = .004;
    c.step = 1e-6;
    c.part("X1")->parameters["duty"] = .25;
    bool square = run(c);
    double high = 0;
    for (int i = 1; i < samples.size(); ++i)
        high += (samples[i].time - samples[i - 1].time) * .5 *
                ((value(c, samples[i]) > 2.5) + (value(c, samples[i - 1]) > 2.5));
    check("square source has configured duty fraction", square && std::abs(high / c.duration - .25) < .005);
    c = source("IPULSE");
    c.part("X1")->parameters = {{"delay", .001}, {"width", .002}, {"period", .004}};
    bool pulse = run(c), seen = false;
    for (const auto &s : samples)
        if (s.time > .0015 && s.time < .0025)
            seen |= std::abs(value(c, s) + 1) < .01;
    check("pulsed current uses declared current direction", pulse && seen);
    c = source("NOISE");
    c.duration = .01;
    c.step = .0001;
    bool noise = run(c);
    Samples first = samples;
    noise &= run(c) && samples.size() == first.size();
    bool bounded = true;
    for (int i = 0; i < samples.size(); ++i)
    {
        bounded &= std::abs(value(c, samples[i])) <= .100001;
        if (i < first.size())
            noise &= std::abs(value(c, samples[i]) - value(c, first[i])) < 1e-12;
    }
    check("seeded sample-hold noise is bounded and repeatable", noise && bounded);
    c.part("X1")->parameters["seed"] = 9;
    bool changed = run(c);
    double difference = 0;
    for (int i = 0; i < qMin(samples.size(), first.size()); ++i)
        difference = qMax(difference, std::abs(value(c, samples[i]) - value(c, first[i])));
    check("changing noise seed changes stimulus", changed && difference > .01);
    c = source("BATTERY", 100);
    check("battery fixed mode includes internal resistance",
          run(c) && std::abs(last(c) - 5 * 100 / 100.1) < .01);
    double fixed = last(c);
    c.part("X1")->parameters["capacity"] = 1e-6;
    check("battery capacity mode produces charge-dependent droop",
          run(c) && last(c) < fixed - .1 && last(c) > 4);
    QDir().mkpath("work");
    QTemporaryDir temp(QDir::current().filePath("work/curve-recovery-XXXXXX"));
    Document doc;
    doc.replace(copy);
    doc.edit("Store waveform",
             [](Circuit &c)
             {
                 c.parts.clear();
                 c.wires.clear();
                 c.probes.clear();
                 c.parts.append({"X1", "PWL", 0, 0, {}});
                 c.part("X1")->data = "0 0\n1 5";
                 c.part("X1")->dataType = "pwl";
             });
    {
        Recovery previous(&doc, temp.path());
        previous.flush();
    }
    Document restored;
    Recovery recovery(&restored, temp.path());
    QStringList errors;
    auto snapshots = recovery.available(errors);
    check("waveform payload survives interrupted-session recovery",
          snapshots.size() == 1 && recovery.restore(snapshots[0]) &&
              restored.circuit.part("X1")->data == "0 0\n1 5");
    recovery.closeSession();
    return failed ? 1 : 0;
}
