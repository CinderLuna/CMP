#include "cmp/DigitalDevices.h"
#include "cmp/LogicVm.h"
#include <iostream>
#include <random>
using namespace cmp;
int main()
{
    int failures = 0;
    auto check = [&](bool ok)
    {
        if (!ok)
            ++failures;
    };
    QString error;
    LogicVm vm;
    using Op = LogicVm::Op;
    using Stop = LogicVm::Stop;
    // Independent explicit four-state truth tables; rows A, columns B.
    const int expected[][16] = {
        {0, 0, 0, 0, 1, 1, 1, 1, 3, 3, 3, 3, 3, 3, 3, 3}, {1, 1, 1, 1, 0, 0, 0, 0, 3, 3, 3, 3, 3, 3, 3, 3},
        {0, 0, 0, 0, 0, 1, 3, 3, 0, 3, 3, 3, 0, 3, 3, 3}, {0, 1, 3, 3, 1, 1, 1, 1, 3, 1, 3, 3, 3, 1, 3, 3},
        {1, 1, 1, 1, 1, 0, 3, 3, 1, 3, 3, 3, 1, 3, 3, 3}, {1, 0, 3, 3, 0, 0, 0, 0, 3, 0, 3, 3, 3, 0, 3, 3},
        {0, 1, 3, 3, 1, 0, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3}, {1, 0, 3, 3, 0, 1, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3},
        {2, 0, 3, 3, 2, 1, 3, 3, 2, 2, 3, 3, 2, 3, 3, 3}};
    for (int op = 0; op < 9; ++op)
        for (int a = 0; a < 4; ++a)
            for (int b = 0; b < 4; ++b)
            {
                check(vm.load(3, {{Op(op), 2, 0, 1}}, error));
                check(vm.set(0, a) && vm.set(1, b));
                check(vm.run(1).reason == Stop::Complete);
                check(vm.get(2) == expected[op][a * 4 + b]);
            }
    check(!vm.load(0, {}, error));
    check(!vm.load(LogicVm::MaximumSlots + 1, {}, error));
    check(!vm.load(3, {{Op(255), 2, 0, 1}}, error));
    check(!vm.load(3, {{Op::And, 3, 0, 1}}, error));
    check(!vm.load(3, {{Op::And, 2, 3, 1}}, error));
    check(!vm.load(3, {{Op::And, 2, 0, 3}}, error));
    check(!vm.load(3, QVector<LogicVm::Instruction>(LogicVm::MaximumInstructions + 1), error));
    check(vm.run(1).reason == Stop::InvalidState);
    check(vm.load(3, {{Op::Not, 1, 0, 0}, {Op::Not, 2, 1, 0}}, error));
    check(!vm.set(3, 0) && !vm.set(0, -1) && !vm.set(0, 4));
    vm.set(0, 0);
    check(vm.run(0).reason == Stop::Budget && vm.get(1) == 3);
    auto result = vm.run(1);
    check(result.reason == Stop::Budget && result.executed == 1 && vm.get(1) == 1 && vm.get(2) == 3);
    std::atomic_bool cancelled{true};
    check(vm.run(1, &cancelled).reason == Stop::Cancelled && vm.get(2) == 3);
    cancelled = false;
    check(vm.run(1, &cancelled).reason == Stop::Complete && vm.get(2) == 0);
    check(vm.run(1).executed == 0);
    vm.rewind();
    vm.set(0, 1);
    check(vm.run(2).reason == Stop::Complete && vm.get(2) == 1);
    // Exercise all component adapters, supply loss and threshold-to-unknown conversion.
    const QStringList kinds{"BUF", "NOT", "AND", "OR", "NAND", "NOR", "XOR", "XNOR", "TRIBUF"};
    for (int op = 0; op < kinds.size(); ++op)
        for (int a : {0, 1, 3})
            for (int b : {0, 1, 3})
            {
                Part part;
                part.id = "U1";
                part.kind = kinds[op];
                DigitalDevice gate(part);
                auto layout = digitalLayout(part.kind);
                QVector<double> volts(spec(part.kind).pins.size(), 0);
                volts[0] = a == 3 ? 2.5 : a * 5.;
                if (layout.inputs.size() > 1)
                    volts[1] = b == 3 ? 2.5 : b * 5.;
                volts[layout.supply] = 5;
                gate.advance(0, volts);
                check(gate.state(layout.outputs[0]) == expected[op][a * 4 + b]);
                check(gate.fault.isEmpty());
                volts[layout.supply] = 0;
                gate.advance(1e-6, volts);
                check(gate.state(layout.outputs[0]) == 2);
            }
    check(vm.load(LogicVm::MaximumSlots, {{Op::Not, LogicVm::MaximumSlots - 1, 0, 0}}, error));
    vm.set(0, 0);
    check(vm.run(1).reason == Stop::Complete && vm.get(LogicVm::MaximumSlots - 1) == 1);
    check(!vm.load(1, {{Op(255), 0, 0, 0}}, error));
    check(vm.run(1).reason == Stop::InvalidState && !error.isEmpty());
    // Native/interpreter differential programs include aliasing, partial blocks, yield and resume.
    std::mt19937 random(0x434d50);
    for (int round = 0; round < 100; ++round)
    {
        QVector<LogicVm::Instruction> code;
        for (int i = 0; i < 129; ++i)
            code.append(
                {Op(random() % 9), quint32(random() % 32), quint32(random() % 32), quint32(random() % 32)});
        LogicVm interpreted, compiled;
        compiled.enableNative(true);
        check(interpreted.load(32, code, error) && compiled.load(32, code, error));
        interpreted.enableNative(false);
#ifdef Q_OS_WIN
#ifdef Q_PROCESSOR_X86_64
        check(compiled.hasNativeCode());
#endif
#endif
        for (int i = 0; i < 32; ++i)
        {
            int v = int(random() % 4);
            interpreted.set(i, v);
            compiled.set(i, v);
        }
        for (quint64 budget : {0, 1, 63, 64, 200})
        {
            auto a = interpreted.run(budget), b = compiled.run(budget);
            check(a.reason == b.reason && a.executed == b.executed);
            for (int i = 0; i < 32; ++i)
                check(interpreted.get(i) == compiled.get(i));
        }
    }
    std::cout << failures << " VM failures\n";
    return failures ? 1 : 0;
}
