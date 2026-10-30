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
        {"V1", "V", 5, 0, {}}, {"R1", "R", 1000, 0, {}}, {"X1", "SEG_CC", 0, 0, {}}, {"G1", "GND", 0, 0, {}}};
    c.wires = {
        {{"V1", 0}, {"R1", 0}}, {{"R1", 1}, {"X1", 0}}, {{"X1", 8}, {"G1", 0}}, {{"V1", 1}, {"G1", 0}}};
    for (int i = 1; i < 8; ++i)
        c.wires << Wire{{"X1", i}, {"G1", 0}};
    bool cc = run(c);
    double current = samples.isEmpty() ? 0 : samples.back().values.value(currentKey(*c.part("X1"), 0));
    check("common-cathode segment draws actual resistor-limited current",
          cc && current > .002 && current < .004);
    c.part("X1")->kind = "SEG_CA";
    c.wires = {
        {{"V1", 0}, {"X1", 8}}, {{"X1", 0}, {"R1", 0}}, {{"R1", 1}, {"G1", 0}}, {{"V1", 1}, {"G1", 0}}};
    for (int i = 1; i < 8; ++i)
        c.wires << Wire{{"X1", i}, {"V1", 0}};
    bool ca = run(c);
    double caCurrent = samples.isEmpty() ? 0 : -samples.back().values.value(currentKey(*c.part("X1"), 0));
    check("common-anode polarity produces equivalent segment current",
          ca && std::abs(caCurrent - current) < 1e-7);
    c.part("X1")->kind = "MATRIX";
    c.wires = {
        {{"V1", 0}, {"R1", 0}}, {{"R1", 1}, {"X1", 0}}, {{"X1", 4}, {"G1", 0}}, {{"V1", 1}, {"G1", 0}}};
    for (int i = 1; i < 4; ++i)
        c.wires << Wire{{"X1", i}, {"G1", 0}};
    for (int i = 5; i < 8; ++i)
        c.wires << Wire{{"X1", i}, {"V1", 0}};
    bool matrix = run(c);
    double pixel = samples.isEmpty() ? 0 : samples.back().values.value(internalKey(*c.part("X1"), "cell00"));
    double off = samples.isEmpty() ? 1 : samples.back().values.value(internalKey(*c.part("X1"), "cell01"), 1);
    check("matrix resolves individual pixel currents with inactive cells isolated",
          matrix && pixel > .002 && pixel < .004 && std::abs(off) < 1e-6);
    c.part("X1")->kind = "KEYPAD";
    c.parts << Part{"R2", "R", 1000, 0, {}};
    c.wires = {{{"V1", 0}, {"R1", 0}},
               {{"R1", 1}, {"X1", 0}},
               {{"X1", 4}, {"R2", 0}},
               {{"R2", 1}, {"G1", 0}},
               {{"V1", 1}, {"G1", 0}}};
    for (int i = 1; i < 4; ++i)
        c.wires << Wire{{"X1", i}, {"G1", 0}};
    for (int i = 5; i < 8; ++i)
        c.wires << Wire{{"X1", i}, {"G1", 0}};
    bool keypad = run(c) && last(c, 4, 1) < .001;
    c.part("X1")->parameters["key0"] = 1;
    check("keypad closes only configured physical row-column contact",
          keypad && run(c) && std::abs(last(c, 4, 1) - 2.5) < .001);
    return failed ? 1 : 0;
}
