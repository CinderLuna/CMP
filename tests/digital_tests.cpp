#include "cmp/DigitalDevices.h"
#include "cmp/ExpandedLibrary.h"
#include "cmp/Simulator.h"
#include <QtCore>
#include <iostream>
using namespace cmp;
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    int failures = 0;
    auto check = [&](QString n, bool ok)
    {
        std::cout << (ok ? "PASS " : "FAIL ") << n.toStdString() << "\n";
        failures += !ok;
    };
    auto make = [](QString k) { return DigitalDevice(Part{"U1", k, spec(k).value, 0, {}}); };
    auto inputs = [](const DigitalDevice &d)
    {
        QVector<double> v(spec(d.part.kind).pins.size(), 0);
        v[digitalLayout(d.part.kind).supply] = 5;
        return v;
    };
    for (QString k : {"AND", "OR", "NAND", "NOR", "XOR", "XNOR"})
    {
        bool valid = true;
        for (int a = 0; a < 2; ++a)
            for (int b = 0; b < 2; ++b)
            {
                auto d = make(k);
                auto v = inputs(d);
                v[0] = a * 5;
                v[1] = b * 5;
                d.advance(0, v);
                int expected = (k == "AND" || k == "NAND") ? (a & b)
                               : (k == "OR" || k == "NOR") ? (a | b)
                                                           : (a ^ b);
                if (k == "NAND" || k == "NOR" || k == "XNOR")
                    expected ^= 1;
                valid &= d.state(2) == expected;
            }
        check(k + " full truth table", valid);
    }
    auto d = make("XOR");
    auto v = inputs(d);
    v[0] = 2.5;
    d.advance(0, v);
    check("undefined input propagates explicitly", d.state(2) == 3);
    d = make("TRIBUF");
    v = inputs(d);
    v[0] = 5;
    d.advance(0, v);
    bool highz = d.state(2) == 2;
    v[1] = 5;
    d.advance(1, v);
    check("tri-state enable controls high impedance", highz && d.state(2) == 1);
    d = make("DFF");
    v = inputs(d);
    v[2] = 5;
    d.advance(0, v);
    v[2] = 0;
    v[0] = 5;
    d.advance(1, v);
    v[1] = 5;
    d.advance(2, v);
    bool latch = d.state(4) == 1;
    v[0] = 0;
    d.advance(3, v);
    check("D flip-flop captures rising edge and holds", latch && d.state(4) == 1);
    v[2] = 5;
    v[3] = 5;
    d.advance(4, v);
    check("conflicting asynchronous controls yield unknown", d.state(4) == 3 && d.state(5) == 3);
    d = make("SR");
    v = inputs(d);
    v[0] = 5;
    d.advance(0, v);
    bool sr = d.state(2) == 1;
    v[0] = 0;
    d.advance(1, v);
    check("SR latch holds after set release", sr && d.state(2) == 1);
    for (QString k : {"JKFF", "TFF"})
    {
        d = make(k);
        v = inputs(d);
        int clock = k == "JKFF" ? 2 : 1, reset = k == "JKFF" ? 3 : 2;
        v[reset] = 5;
        d.advance(0, v);
        v[reset] = 0;
        v[0] = 5;
        if (k == "JKFF")
            v[1] = 5;
        d.advance(1, v);
        v[clock] = 5;
        d.advance(2, v);
        bool toggle = d.state(digitalLayout(k).outputs[0]) == 1;
        v[clock] = 0;
        d.advance(3, v);
        v[clock] = 5;
        d.advance(4, v);
        check(k + " toggles on consecutive clock edges", toggle && d.state(digitalLayout(k).outputs[0]) == 0);
    }
    d = make("COUNTER");
    v = inputs(d);
    bool count = true;
    for (int n = 1; n <= 17; ++n)
    {
        v[0] = 0;
        d.advance(n * 2, v);
        v[0] = 5;
        d.advance(n * 2 + 1, v);
        for (int bit = 0; bit < 4; ++bit)
            count &= d.state(7 + bit) == ((n >> bit) & 1);
    }
    check("counter sequence wraps modulo sixteen", count);
    d = make("SHIFT");
    v = inputs(d);
    v[4] = 5;
    for (int bit = 7; bit >= 0; --bit)
    {
        v[0] = ((0xa5 >> bit) & 1) * 5;
        v[1] = 0;
        d.advance(0, v);
        v[1] = 5;
        d.advance(0, v);
    }
    v[2] = 5;
    d.advance(0, v);
    bool shifted = true;
    for (int i = 0; i < 8; ++i)
        shifted &= d.state(5 + i) == ((0xa5 >> i) & 1);
    check("shift and latch preserve known byte", shifted);
    d = make("DECODER");
    v = inputs(d);
    v[0] = 5;
    d.advance(0, v);
    check("decoder one drives only b and c segments",
          d.state(5) == 0 && d.state(6) == 1 && d.state(7) == 1 && d.state(8) == 0);
    d = make("RAM");
    v = inputs(d);
    v[16] = 5;
    v[17] = 5;
    d.advance(0, v);
    bool unknown = d.state(8) == 3;
    v[18] = 5;
    for (int i = 0; i < 8; ++i)
        v[8 + i] = ((0x96 >> i) & 1) * 5;
    d.advance(1, v);
    v[18] = 0;
    d.advance(2, v);
    bool mem = true;
    for (int i = 0; i < 8; ++i)
        mem &= d.state(8 + i) == ((0x96 >> i) & 1);
    check("RAM starts unknown and writes on WE falling edge", unknown && mem);
    Part rom{"U1", "ROM", 0, 0, {}};
    rom.data = QByteArray(1, char(0x5a));
    rom.dataType = "memory";
    d = DigitalDevice(rom);
    v = inputs(d);
    v[16] = v[17] = 5;
    v[18] = 0;
    d.advance(0, v);
    bool read = true;
    for (int i = 0; i < 8; ++i)
        read &= d.state(8 + i) == ((0x5a >> i) & 1);
    check("ROM image drives data bus", read);
    d = make("ADC");
    v = inputs(d);
    v[0] = 2.5;
    v[1] = 5;
    d.advance(0, v);
    v[2] = 5;
    d.advance(1, v);
    bool adc = true;
    for (int i = 0; i < 8; ++i)
        adc &= d.state(3 + i) == ((128 >> i) & 1);
    check("8-bit ADC samples midscale at clock edge", adc);
    d = make("DAC");
    v = inputs(d);
    for (int i = 0; i < 8; ++i)
        v[i] = ((128 >> i) & 1) * 5;
    d.advance(0, v);
    check("8-bit DAC computes reference fraction", std::abs(d.analog - 128. / 255) < 1e-12);
    Circuit c;
    c.duration = .0001;
    c.step = .000001;
    c.parts = {{"V1", "V", 5, 0, {}},
               {"U1", "LOGIC", 0, 0, {}},
               {"U2", "NOT", 0, 0, {}},
               {"R1", "R", 1000, 0, {}},
               {"G1", "GND", 0, 0, {}}};
    c.wires = {{{"V1", 1}, {"G1", 0}}, {{"V1", 0}, {"U1", 1}}, {{"G1", 0}, {"U1", 2}},
               {{"V1", 0}, {"U2", 2}}, {{"G1", 0}, {"U2", 3}}, {{"U1", 0}, {"U2", 0}},
               {{"U2", 1}, {"R1", 0}}, {{"R1", 1}, {"G1", 0}}};
    Simulator engine;
    Samples samples;
    QStringList errors;
    bool ok = false;
    QEventLoop loop;
    QObject::connect(&engine, &Simulator::samplesReady, &loop, [&](Samples s) { samples += s; });
    QObject::connect(&engine, &Simulator::message, &loop, [&](QString s) { errors << s; });
    QObject::connect(&engine, &Simulator::completed, &loop,
                     [&](bool result)
                     {
                         ok = result;
                         loop.quit();
                     });
    QTimer watchdog;
    watchdog.setSingleShot(true);
    QObject::connect(&watchdog, &QTimer::timeout, &loop,
                     [&]
                     {
                         engine.stop();
                         loop.quit();
                     });
    watchdog.start(10000);
    engine.start(c, false);
    loop.exec();
    auto node = c.nodes().value(Pin{"U2", 1}.key());
    double out = samples.isEmpty() ? 0 : samples.back().values.value(node);
    if (!ok)
        std::cerr << errors.join("\n").toStdString();
    check("powered inverter drives actual resistor through finite impedance", ok && out > 4.85 && out < 4.9);
    return failures ? 1 : 0;
}
