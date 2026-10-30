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

    Circuit c;
    c.operatingPoint = true;
    c.parts = {
        {"V1", "V", 12, 0, {}}, {"X1", "REG5", 5, 0, {}}, {"R1", "R", 100, 0, {}}, {"G1", "GND", 0, 0, {}}};
    c.wires = {{{"V1", 0}, {"X1", 0}},
               {{"V1", 1}, {"G1", 0}},
               {{"X1", 1}, {"G1", 0}},
               {{"X1", 2}, {"R1", 0}},
               {{"R1", 1}, {"G1", 0}}};
    bool reg = run(c) && std::abs(last(c, 2, 1) - 5) < .01;
    c.part("R1")->value = 1;
    check("fixed regulator regulates and limits load current",
          reg && run(c) && last(c, 2, 1) > .99 && last(c, 2, 1) < 1.01);
    c.part("R1")->value = 100;
    c.part("V1")->value = 6;
    check("fixed regulator loses headroom at declared dropout",
          run(c) && last(c, 2, 1) > 3.99 && last(c, 2, 1) < 4.01);
    c.part("X1")->kind = "LDO";
    c.part("X1")->value = 3.3;
    c.part("V1")->value = 3.5;
    check("LDO follows configured dropout", run(c) && last(c, 2, 1) > 3.19 && last(c, 2, 1) < 3.21);
    c.part("X1")->kind = "VREF";
    c.part("X1")->value = 2.5;
    c.part("V1")->value = 5;
    c.part("R1")->value = 10000;
    check("reference output includes finite load impedance",
          run(c) && last(c, 2, 1) > 2.499 && last(c, 2, 1) < 2.5);
    c = {};
    c.operatingPoint = true;
    c.parts = {{"V1", "V", 12, 0, {}},
               {"VI", "V", 1, 0, {}},
               {"X1", "LM358", 100000, 0, {}},
               {"R1", "R", 1000, 0, {}},
               {"G1", "GND", 0, 0, {}}};
    c.wires = {{{"V1", 0}, {"X1", 7}}, {{"V1", 1}, {"G1", 0}}, {{"VI", 1}, {"G1", 0}}, {{"X1", 3}, {"G1", 0}},
               {{"VI", 0}, {"X1", 2}}, {{"X1", 0}, {"X1", 1}}, {{"X1", 0}, {"R1", 0}}, {{"R1", 1}, {"G1", 0}},
               {{"X1", 4}, {"G1", 0}}, {{"X1", 6}, {"X1", 5}}};
    check("dual op-amp buffer follows input under load", run(c) && std::abs(last(c, 0, 3) - 1) < .001);
    c.parts << Part{"R2", "R", 10000, 0, {}} << Part{"R3", "R", 10000, 0, {}};
    c.wires.removeAt(5);
    c.wires << Wire{{"X1", 0}, {"R2", 0}} << Wire{{"R2", 1}, {"X1", 1}} << Wire{{"X1", 1}, {"R3", 0}}
            << Wire{{"R3", 1}, {"G1", 0}};
    check("op-amp non-inverting gain uses actual feedback network",
          run(c) && std::abs(last(c, 0, 3) - 2) < .002);
    c = {};
    c.operatingPoint = true;
    c.parts = {{"V1", "V", 12, 0, {}},  {"X1", "LM317", 1.25, 0, {}}, {"R1", "R", 240, 0, {}},
               {"R2", "R", 720, 0, {}}, {"RL", "R", 1000, 0, {}},     {"G1", "GND", 0, 0, {}}};
    c.wires = {{{"V1", 0}, {"X1", 2}}, {{"V1", 1}, {"G1", 0}}, {{"X1", 1}, {"R1", 0}},
               {{"R1", 1}, {"X1", 0}}, {{"X1", 0}, {"R2", 0}}, {{"R2", 1}, {"G1", 0}},
               {{"X1", 1}, {"RL", 0}}, {{"RL", 1}, {"G1", 0}}};
    check("adjustable regulator respects resistor network and adjust current",
          run(c) && std::abs(last(c, 1, 0) - 1.25) < .001 &&
              samples.back().values.value(c.nodes().value(Pin{"X1", 1}.key())) > 5);
    c.part("R2")->value = 240;
    check("adjustable regulator supports second resistor setting",
          run(c) && std::abs(last(c, 1, 0) - 1.25) < .001 &&
              samples.back().values.value(c.nodes().value(Pin{"X1", 1}.key())) < 2.52);

    c = {};
    c.operatingPoint = true;
    c.parts = {{"V1", "V", 5, 0, {}},  {"VA", "V", 1, 0, {}},     {"VB", "V", 3, 0, {}},
               {"VS", "V", 0, 0, {}},  {"X1", "MUX2", 50, 0, {}}, {"RL", "R", 10000, 0, {}},
               {"G1", "GND", 0, 0, {}}};
    c.wires = {{{"V1", 0}, {"X1", 5}}, {{"V1", 0}, {"X1", 4}}, {{"V1", 1}, {"G1", 0}},
               {{"VA", 1}, {"G1", 0}}, {{"VB", 1}, {"G1", 0}}, {{"VS", 1}, {"G1", 0}},
               {{"VA", 0}, {"X1", 0}}, {{"VB", 0}, {"X1", 1}}, {{"VS", 0}, {"X1", 3}},
               {{"X1", 6}, {"G1", 0}}, {{"X1", 2}, {"RL", 0}}, {{"RL", 1}, {"G1", 0}}};
    bool mux = run(c) && std::abs(last(c, 2, 6) - 10000. / 10050) < .001;
    c.part("VS")->value = 5;
    check("analog mux selects channels with finite on resistance",
          mux && run(c) && std::abs(last(c, 2, 6) - 30000. / 10050) < .001);
    c.part("X1")->kind = "COMP_OC";
    c.part("X1")->value = 0;
    c.wires = {{{"V1", 0}, {"X1", 3}}, {{"V1", 1}, {"G1", 0}}, {{"VA", 1}, {"G1", 0}},
               {{"VB", 1}, {"G1", 0}}, {{"VA", 0}, {"X1", 2}}, {{"VB", 0}, {"X1", 1}},
               {{"X1", 4}, {"G1", 0}}, {{"X1", 0}, {"RL", 0}}, {{"RL", 1}, {"V1", 0}}};
    c.parts.removeAt(3);
    c.part("RL")->value = 10000;
    bool cmp = run(c) && last(c, 0, 4) < .03;
    c.part("VA")->value = 3;
    c.part("VB")->value = 1;
    check("single open-collector comparator sinks and releases pull-up",
          cmp && run(c) && last(c, 0, 4) > 4.99);
    c = Circuit::blinker();
    c.duration = .4;
    c.step = .0001;
    c.part("RA")->value = 1000;
    c.part("RB")->value = 10000;
    c.part("C1")->value = 1e-6;
    c.part("U1")->kind = "NE556";
    int map[] = {6, 5, 4, 3, 2, 1, 0, 13};
    for (auto &w : c.wires)
    {
        if (w.a.part == "U1")
            w.a.index = map[w.a.index];
        if (w.b.part == "U1")
            w.b.index = map[w.b.index];
    }
    c.probes.clear();
    c.parts << Part{"RA2", "R", 1000, 0, {}} << Part{"RB2", "R", 20000, 0, {}}
            << Part{"CT2", "C", 1e-6, 0, {}} << Part{"CC2", "C", 1e-8, 0, {}}
            << Part{"RL2", "R", 10000, 0, {}};
    c.wires << Wire{{"V1", 0}, {"RA2", 0}} << Wire{{"RA2", 1}, {"RB2", 0}} << Wire{{"RA2", 1}, {"U1", 12}}
            << Wire{{"RB2", 1}, {"CT2", 0}} << Wire{{"CT2", 0}, {"U1", 7}} << Wire{{"CT2", 0}, {"U1", 11}}
            << Wire{{"CT2", 1}, {"G1", 0}} << Wire{{"U1", 9}, {"V1", 0}} << Wire{{"U1", 10}, {"CC2", 0}}
            << Wire{{"CC2", 1}, {"G1", 0}} << Wire{{"U1", 8}, {"RL2", 0}} << Wire{{"RL2", 1}, {"G1", 0}};
    bool dual = run(c);
    int edge1 = 0, edge2 = 0;
    bool high1 = false, high2 = false;
    auto nodes = c.nodes();
    for (auto &s : samples)
    {
        bool a = s.values.value(nodes.value(Pin{"U1", 4}.key())) > 2.5,
             b = s.values.value(nodes.value(Pin{"U1", 8}.key())) > 2.5;
        if (s.time > .05)
        {
            edge1 += a && !high1;
            edge2 += b && !high2;
        }
        high1 = a;
        high2 = b;
    }
    check("NE556 channels oscillate independently at distinct RC periods",
          dual && edge1 > edge2 * 1.6 && edge2 > 7);
    return failed ? 1 : 0;
}
