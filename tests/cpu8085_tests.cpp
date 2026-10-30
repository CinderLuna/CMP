#include "cmp/Cpu8085.h"
#include "cmp/DigitalDevices.h"
#include <QtCore>
#include <iostream>
#include <random>
using namespace cmp;
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    int failures = 0;
    auto check = [&](const char *n, bool ok)
    {
        std::cout << (ok ? "PASS " : "FAIL ") << n << "\n";
        failures += !ok;
    };
    QByteArray image = QByteArray::fromHex("31feff3e05060380320080cd2000d300db0132018076");
    image.resize(32);
    image += QByteArray::fromHex("3cc9");
    Cpu8085 cpu;
    QString error;
    check("8085 program loads", cpu.load(image, error));
    cpu.input[1] = 0xa5;
    cpu.advance(1000);
    check("8085 arithmetic, RAM, call/return and port I/O execute",
          cpu.halted && cpu.fault.isEmpty() && cpu.read(0x8000) == 8 && cpu.read(0x8001) == 0xa5 &&
              cpu.output[0] == 9 && cpu.sp == 0xfffe);
    cpu.load(QByteArray::fromHex("06033e003c05c20400d30076"), error);
    cpu.advance(1000);
    check("conditional branch loop observes zero flag", cpu.halted && cpu.output[0] == 3);
    cpu.load(QByteArray::fromHex("3e09c60927d30076"), error);
    cpu.advance(1000);
    check("decimal adjustment produces packed BCD", cpu.halted && cpu.output[0] == 0x18);
    cpu.load(QByteArray::fromHex("3e003dca0b00d30ac30d00d30176"), error);
    cpu.advance(1000);
    check("decrement wraps and zero branch is not taken", cpu.fault.isEmpty() && cpu.output[10] == 255);
    cpu.load(QByteArray::fromHex("3e0132000076"), error);
    cpu.advance(100);
    check("trainer ROM is write protected", !cpu.fault.isEmpty() && cpu.read(0) == 0x3e);
    cpu.load(QByteArray::fromHex("08"), error);
    cpu.advance(100);
    check("undocumented opcode fails explicitly", cpu.fault.contains("Unsupported"));
    cpu.load(QByteArray::fromHex("00c30000"), error);
    auto run = cpu.advanceTo(1000, 0);
    check("zero budget does not advance guest time",
          run.reason == RunReason::Budget && run.reachedCycle == 0);
    run = cpu.advanceTo(1, 1);
    check("atomic instruction overshoot is reported",
          run.reason == RunReason::Deadline && run.reachedCycle == 4 && run.instructions == 1);
    run = cpu.advanceTo(1000, 2);
    check("budget yield reports actual reached cycle",
          run.reason == RunReason::Budget && run.reachedCycle < 1000 && run.instructions == 2);
    std::atomic_bool cancel{true};
    auto before = cpu.cycles;
    run = cpu.advanceTo(1000, 100, &cancel);
    check("cancel leaves processor state untouched",
          run.reason == RunReason::Cancelled && cpu.cycles == before);
    cancel = false;
    check("yield resumes to deadline", cpu.advanceTo(1000, 1000, &cancel).reason == RunReason::Deadline);
    std::mt19937 random(0x8085);
    for (int round = 0; round < 20; ++round)
    {
        QByteArray rom;
        const int regs[]{0, 1, 2, 3, 4, 5, 7};
        for (int i = 0; i < 64; ++i)
        {
            int dst = regs[random() % 7], src = regs[random() % 7];
            if (random() % 3 == 0)
            {
                rom.append(char(0x06 + (dst << 3)));
                rom.append(char(random()));
            }
            else
                rom.append(char(0x40 + (dst << 3) + src));
        }
        rom += QByteArray::fromHex("d300320080c30000"); // I/O, RAM write and control flow must side-exit.
        Cpu8085 reference, optimized;
        optimized.enableNative(true);
        reference.enableNative(false);
        reference.load(rom, error);
        optimized.load(rom, error);
        quint64 target = 0;
        for (int batch = 0; batch < 100; ++batch)
        {
            target += random() % 100 + 1;
            quint64 budget = random() % 40;
            auto a = reference.advanceTo(target, budget), b = optimized.advanceTo(target, budget);
            check("ROM native block matches interpreter state/cycles/side exits",
                  a.reason == b.reason && a.instructions == b.instructions &&
                      reference.architecturalState() == optimized.architecturalState());
        }
#if defined(Q_OS_WIN) && defined(Q_PROCESSOR_X86_64)
        check("hot ROM blocks compile", optimized.nativeBlocks() > 0);
        check("native ROM pages are not writable", optimized.nativeCodeIsProtected());
#endif
        optimized.load(QByteArray::fromHex("76"), error);
        check("reset clears compiled ROM cache", optimized.nativeBlocks() == 0);
    }
    Cpu8085 writable;
    writable.load(QByteArray::fromHex("3e76320080c30080"), error);
    check("RAM execution stays interpreted and observes new bytes",
          writable.advanceTo(100).reason == RunReason::Halt && writable.read(0x8000) == 0x76);
    cpu.load(QByteArray::fromHex("00c30000"), error);
    check("invalid reload closes executable processor",
          !cpu.load({}, error) && cpu.advanceTo(100).reason == RunReason::Fault);
    Part part{"U1", "CPU8085", 3072000, 0, {}};
    part.data = QByteArray::fromHex("db01d30076");
    part.dataType = "firmware";
    DigitalDevice device(part);
    QVector<double> v(19, 0);
    v[17] = 5;
    v[0] = v[7] = 5;
    device.advance(0, v);
    device.advance(.001, v);
    check("trainer module maps input port one to output port zero",
          device.state(9) == 1 && device.state(16) == 1 && device.state(10) == 0 && device.fault.isEmpty());
    return failures ? 1 : 0;
}
