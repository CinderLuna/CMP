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

    auto c = source("MOTOR", .001);
    c.parts[1] = {"R1", "V", 5, 0, {}};
    c.duration = .1;
    c.step = .0001;
    bool motor = run(c);
    double speed = samples.isEmpty() ? 0 : samples.back().values.value(internalKey(*c.part("X1"), "speed"));
    check("DC motor accelerates with back EMF and mechanical state", motor && speed > 150 && speed < 250);
    c.part("X1")->parameters["load"] = .02;
    check("DC motor slows under mechanical load",
          run(c) && samples.back().values.value(internalKey(*c.part("X1"), "speed")) < speed - 10);
    for (QString kind : {"NMOS", "PMOS"})
    {
        c = {};
        c.operatingPoint = true;
        c.duration = .001;
        c.step = .00001;
        bool pm = kind == "PMOS";
        c.parts = {{"V1", "V", 5, 0, {}},
                   {"VG", "V", pm ? 0. : 5., 0, {}},
                   {"R1", "R", 1000, 0, {}},
                   {"X1", kind, 2, 0, {}},
                   {"G1", "GND", 0, 0, {}}};
        c.wires = {
            {{"V1", 1}, {"G1", 0}}, {{"VG", 1}, {"G1", 0}}, {{"VG", 0}, {"X1", 1}}, {{"X1", 0}, {"R1", 0}}};
        c.wires << Wire{{"X1", 2}, {pm ? "V1" : "G1", 0}} << Wire{{"R1", 1}, {pm ? "G1" : "V1", 0}};
        bool on = run(c) && std::abs(last(c, 0, 2)) < .1;
        c.part("VG")->value = pm ? 5 : 0;
        check(qPrintable(kind + " switches load with correct gate polarity"),
              on && run(c) && std::abs(last(c, 0, 2)) > 4.9);
    }
    c = {};
    c.duration = .03;
    c.step = .00001;
    c.parts = {{"V1", "PWL", 5, 0, {}},  {"VC", "V", 5, 0, {}},    {"X1", "RELAY", 250, 0, {}},
               {"R1", "R", 1000, 0, {}}, {"R2", "R", 1000, 0, {}}, {"G1", "GND", 0, 0, {}}};
    c.part("V1")->data = "0 0; .002 0; .0021 5; .012 5; .0121 0; .03 0";
    c.part("V1")->dataType = "curve";
    c.wires = {{{"V1", 0}, {"X1", 0}}, {{"V1", 1}, {"G1", 0}}, {{"X1", 1}, {"G1", 0}},
               {{"VC", 0}, {"X1", 2}}, {{"VC", 1}, {"G1", 0}}, {{"X1", 3}, {"R1", 0}},
               {{"X1", 4}, {"R2", 0}}, {{"R1", 1}, {"G1", 0}}, {{"R2", 1}, {"G1", 0}}};
    bool relay = run(c);
    double active = 0, returned = 0;
    for (auto &s : samples)
    {
        if (s.time > .008 && s.time < .010)
            active = qMax(active, value(c, s, 4, 1));
        if (s.time > .025)
            returned = qMax(returned, value(c, s, 3, 1));
    }
    check("relay coil energizes and releases isolated SPDT contacts",
          relay && active > 4.9 && returned > 4.9);
    c = {};
    c.duration = .04;
    c.step = .00001;
    c.operatingPoint = false;
    c.parts = {{"V1", "SINE", 1, 0, {}},
               {"X1", "TRANSFORMER", 1, 0, {}},
               {"R1", "R", 1000000, 0, {}},
               {"G1", "GND", 0, 0, {}}};
    c.part("V1")->parameters["frequency"] = 100;
    c.wires = {{{"V1", 0}, {"X1", 0}}, {{"V1", 1}, {"G1", 0}}, {{"X1", 1}, {"G1", 0}},
               {{"X1", 3}, {"G1", 0}}, {{"X1", 2}, {"R1", 0}}, {{"R1", 1}, {"G1", 0}}};
    bool transformer = run(c);
    double peak = 0;
    for (auto &s : samples)
        if (s.time > .02)
            peak = qMax(peak, value(c, s, 2, 3));
    check("coupled inductors produce declared turns ratio", transformer && peak > 1.9 && peak < 2.05);
    c = source("BUZZER");
    c.parts[1] = {"R1", "V", 5, 0, {}};
    c.operatingPoint = true;
    check("active buzzer presents specified powered load",
          run(c) && std::abs(samples.back().values.value(currentKey(*c.part("X1"), 0)) - .02) < 1e-6);
    c = source("PIEZO");
    c.parts[1] = {"R1", "SINE", 1, 0, {}};
    c.operatingPoint = false;
    c.part("R1")->parameters["frequency"] = 1000;
    bool piezo = run(c);
    check("piezo motional impedance converges", piezo && samples.size() > 10);

    c = {};
    c.duration = .012;
    c.step = .000002;
    c.parts = {{"VM", "PWL", 5, 0, {}},
               {"VG", "PWL", 5, 0, {}},
               {"R1", "R", 100, 0, {}},
               {"X1", "SCR", .001, 0, {}},
               {"G1", "GND", 0, 0, {}}};
    c.part("VM")->data = "0 5; .005 5; .00501 0; .007 0; .00701 5; .012 5";
    c.part("VM")->dataType = "pwl";
    c.part("VG")->data = "0 0; .001 0; .00101 5; .002 5; .00201 0; .012 0";
    c.part("VG")->dataType = "pwl";
    c.wires = {{{"VM", 0}, {"R1", 0}}, {{"R1", 1}, {"X1", 0}}, {{"VG", 0}, {"X1", 1}},
               {{"VM", 1}, {"G1", 0}}, {{"VG", 1}, {"G1", 0}}, {{"X1", 2}, {"G1", 0}}};
    bool scr = run(c);
    double latched = 0, released = 0;
    for (auto &s : samples)
    {
        if (s.time > .003 && s.time < .004)
            latched = qMax(latched, std::abs(s.values.value(currentKey(*c.part("X1"), 0))));
        if (s.time > .009)
            released = qMax(released, std::abs(s.values.value(currentKey(*c.part("X1"), 0))));
    }
    check("SCR latches after gate pulse and releases at current zero",
          scr && latched > .04 && released < 1e-6);
    c.part("X1")->kind = "TRIAC";
    c.part("VM")->data = "0 -5; .005 -5; .00501 0; .007 0; .00701 -5; .012 -5";
    bool triac = run(c);
    latched = 0;
    released = 0;
    for (auto &s : samples)
    {
        if (s.time > .003 && s.time < .004)
            latched = qMax(latched, std::abs(s.values.value(currentKey(*c.part("X1"), 0))));
        if (s.time > .009)
            released = qMax(released, std::abs(s.values.value(currentKey(*c.part("X1"), 0))));
    }
    check("TRIAC latches negative main current and releases at zero",
          triac && latched > .04 && released < 1e-6);
    c = {};
    c.operatingPoint = true;
    c.parts = {{"V1", "V", 5, 0, {}},
               {"R1", "R", 1000, 0, {}},
               {"R2", "R", 1000, 0, {}},
               {"X1", "OPTO", 0, 0, {}},
               {"G1", "GND", 0, 0, {}}};
    c.wires = {{{"V1", 0}, {"R1", 0}}, {{"R1", 1}, {"X1", 0}}, {{"X1", 1}, {"G1", 0}}, {{"V1", 0}, {"R2", 0}},
               {{"R2", 1}, {"X1", 2}}, {{"X1", 3}, {"G1", 0}}, {{"V1", 1}, {"G1", 0}}};
    bool opto = run(c);
    double ratio = samples.isEmpty() ? 0
                                     : samples.back().values.value(currentKey(*c.part("X1"), 2)) /
                                           samples.back().values.value(currentKey(*c.part("X1"), 0));
    check("optocoupler output follows configured current transfer", opto && std::abs(ratio - .5) < 1e-4);

    for (QString kind : {"DARL_N", "DARL_P"})
    {
        bool pm = kind == "DARL_P";
        c = {};
        c.operatingPoint = true;
        c.parts = {{"V1", "V", 5, 0, {}},    {"VG", "V", pm ? 0. : 5., 0, {}}, {"RB", "R", 10000, 0, {}},
                   {"RL", "R", 1000, 0, {}}, {"X1", kind, 0, 0, {}},           {"G1", "GND", 0, 0, {}}};
        c.wires = {{{"V1", 1}, {"G1", 0}},
                   {{"VG", 1}, {"G1", 0}},
                   {{"VG", 0}, {"RB", 0}},
                   {{"RB", 1}, {"X1", 1}},
                   {{"X1", 0}, {"RL", 0}},
                   {{"X1", 2}, {pm ? "V1" : "G1", 0}},
                   {{"RL", 1}, {pm ? "G1" : "V1", 0}}};
        bool conducting = run(c) && std::abs(last(c, 0, 2)) < 1.2;
        c.part("VG")->value = pm ? 5 : 0;
        check(qPrintable(kind + " switches load with correct two-BJT polarity"),
              conducting && run(c) && std::abs(last(c, 0, 2)) > 4.9);
    }
    for (QString k : {"DARL_N", "DARL_P", "OPTO", "SPEAKER", "SCR", "TRIAC"})
    {
        check(qPrintable(k + " has validated default model"),
              !spec(k).behavior.isEmpty() && validateExpanded(Part{"X1", k, spec(k).value, 0, {}}).isEmpty());
    }
    return failed ? 1 : 0;
}
