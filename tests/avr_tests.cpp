#include "cmp/AvrMachine.h"
#include "cmp/Circuit.h"
#include "cmp/ExpandedLibrary.h"
#include "cmp/Simulator.h"
#include <QtCore>
#include <iostream>
using namespace cmp;
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    int failures = 0;
    auto check = [&](const char *n, bool v)
    {
        std::cout << (v ? "PASS " : "FAIL ") << n << "\n";
        failures += !v;
    };
    QByteArray image;
    QString error;
    check("Intel HEX parses checksum and little-endian program",
          parseFirmware(":0600000020E024B925B93F\n:00000001FF\n", true, image, error) &&
              image.toHex() == "20e024b925b9");
    check("corrupt HEX is rejected",
          !parseFirmware(":0600000020E024B925B954\n:00000001FF\n", true, image, error));
    check("out-of-range program is rejected",
          !parseFirmware(":020000040001F9\n:0100000000FF\n:00000001FF\n", true, image, error));
    // LDI r16,0x20; OUT DDRB,r16; OUT PORTB,r16; RJMP -1.
    AvrMachine cpu;
    check("Uno program loads", cpu.load(QByteArray::fromHex("00e204b905b9ffcf"), error));
    if (!error.isEmpty())
        std::cerr << error.toStdString() << "\n";
    auto state = cpu.advance(100, QVector<double>(20, 0));
    check("AVR instructions drive Uno D13",
          state.size() == 20 && state[13] == 1 && cpu.read(0x24) == 32 && cpu.read(0x25) == 32);
    check("AVR GPIO input reads externally sampled voltage",
          cpu.load(QByteArray::fromHex("09b1ffcf"), error));
    QVector<double> input(20, 0);
    input[0] = 5;
    cpu.advance(10, input);
    check("digital input reaches PIN register", cpu.read(16) == 1);
    // LDI r16,0x40; OUT DDRD,r16; configure timer0 CTC toggle OC0A, no prescale, OCR0A=9.
    auto timerProgram = QByteArray::fromHex("00e40ab902e404bd09e007bd01e005bdffcf");
    check("timer firmware loads", cpu.load(timerProgram, error));
    auto a = cpu.advance(40, QVector<double>(20, 0));
    auto b = cpu.advance(50, QVector<double>(20, 0));
    check("timer0 peripheral toggles mapped OC0A / D6", a.size() == 20 && b.size() == 20 && a[6] != b[6]);

    Circuit c;
    c.duration = .0001;
    c.step = 1e-6;
    c.parts = {{"U1", "UNO", 16000000, 0, {}},
               {"V1", "V", 5, 0, {}},
               {"R1", "R", 1000, 0, {}},
               {"G1", "GND", 0, 0, {}}};
    c.part("U1")->data = QByteArray::fromHex("00e204b905b9ffcf");
    c.part("U1")->dataType = "firmware";
    c.wires = {{{"V1", 0}, {"U1", 20}}, {{"V1", 1}, {"G1", 0}}, {{"U1", 21}, {"G1", 0}},
               {{"U1", 13}, {"R1", 0}}, {{"R1", 1}, {"G1", 0}}, {{"U1", 22}, {"V1", 0}}};
    Simulator simulator;
    Samples samples;
    QStringList messages;
    QObject::connect(&simulator, &Simulator::samplesReady, &app, [&](Samples s) { samples += s; });
    QObject::connect(&simulator, &Simulator::message, &app, [&](QString s) { messages << s; });
    bool ok = false;
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop,
                     [&]
                     {
                         simulator.stop();
                         loop.quit();
                     });
    QObject::connect(&simulator, &Simulator::completed, &loop,
                     [&](bool success)
                     {
                         ok = success;
                         loop.quit();
                     });
    timer.start(15000);
    simulator.start(c, false);
    loop.exec();
    auto node = c.nodes().value(Pin{"U1", 13}.key());
    double output = samples.isEmpty() ? 0 : samples.back().values.value(node);
    if (!ok)
        std::cerr << messages.join("\n").toStdString() << "\n";
    check("Uno firmware drives actual ngspice load with finite output impedance",
          ok && output > 4.85 && output < 4.90);
    auto saved = c.json();
    Circuit loaded;
    QString why;
    check("firmware persists in project",
          Circuit::parse(saved, loaded, why) && loaded.part("U1")->data == c.part("U1")->data);
    QVector<int> runtimePins;
    const auto beforeCycles = cpu.cycles();
    auto report = cpu.advanceTo(beforeCycles + 1000000, QVector<double>(20, 0), runtimePins, 5, 5, 0);
    check("AVR zero budget reports incomplete deadline",
          report.reason == RunReason::Budget && report.reachedCycle == beforeCycles);
    std::atomic_bool cancel{true};
    report = cpu.advanceTo(beforeCycles + 1000000, QVector<double>(20, 0), runtimePins, 5, 5, 100, &cancel);
    check("AVR cancellation is explicit",
          report.reason == RunReason::Cancelled && report.reachedCycle == beforeCycles);
    check("invalid AVR reload clears executable state", !cpu.load({}, error));
    report = cpu.advanceTo(100, {}, runtimePins);
    check("failed AVR runtime inspection and advance are safe",
          report.reason == RunReason::Fault && runtimePins.isEmpty() && cpu.cycles() == 0 &&
              cpu.read(0) == -1 && cpu.serial().isEmpty());
    return failures ? 1 : 0;
}
