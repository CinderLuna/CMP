#include "cmp/DigitalDevices.h"
#include "cmp/ExecutionPolicy.h"
#include "cmp/LogicVm.h"
#include "cmp/SimulationEngine.h"
#include "cmp/WorkerSandbox.h"
#include <iostream>
#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
using namespace cmp;
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    int failures = 0;
    auto check = [&](bool value, const char *name)
    {
        if (!value)
        {
            ++failures;
            std::cerr << name << "\n";
        }
    };
    QString error;
    check(validateExecutionCatalog(error), "complete reviewed catalog policy");
    check(!executionPolicy("UNREVIEWED_COMPONENT"), "unknown component must not fall back to analog");
    auto counterfeit = spec("AND");
    counterfeit.behavior = "resistor";
    check(!validateExecutionSpec(counterfeit, error), "approved kind with changed backend rejected");
    Circuit unapproved;
    unapproved.parts.append({"X1", "UNREVIEWED_COMPONENT", 0, 0, {}});
    check(!validateExecutionCircuit(unapproved, error), "unapproved circuit rejected");
    int nativeGates = 0, processors = 0, analog = 0, topology = 0;
    for (const auto &definition : catalog())
    {
        const auto *policy = executionPolicy(definition.kind);
        check(policy && validateExecutionSpec(definition, error), "every component is explicitly registered");
        if (!policy)
            continue;
        if (policy->backend == ExecutionBackend::DeviceVm)
        {
            Part part{"U1", definition.kind, definition.value, 0, {}};
            if (definition.kind == "CPU8085")
            {
                part.data = QByteArray::fromHex("76");
                part.dataType = "firmware";
            }
            DigitalDevice device(part);
            check(device.fault.isEmpty(), "registered digital component compiles to a VM kernel");
            auto layout = digitalLayout(definition.kind);
            check(layout.supply >= 0 && layout.ground >= 0 && layout.supply < definition.pins.size() &&
                      layout.ground < definition.pins.size(),
                  "valid VM electrical adapter");
            if (policy->lowering == NativeLowering::LogicGates)
            {
                ++nativeGates;
                LogicVm::Op op;
                check(LogicVm::gate(definition.kind, op), "advertised gate JIT has a lowering");
                LogicVm vm;
                vm.enableNative(true);
                check(vm.load(3, {{op, 2, 0, 1}}, error), "native gate program verifies");
#if defined(Q_OS_WIN) && defined(Q_PROCESSOR_X86_64)
                check(vm.hasNativeCode(), "native gate backend compiles");
                check(vm.nativeCodeIsProtected(), "native gate pages are read/execute and not writable");
#endif
            }
            if (policy->lowering == NativeLowering::Rom8085)
                ++processors;
        }
        else
            check(policy->lowering == NativeLowering::None, "no false claim of a native lowering");
        analog += policy->backend == ExecutionBackend::AnalogSolver;
        topology += policy->backend == ExecutionBackend::Topology;
    }
    check(nativeGates == 9 && processors == 1 && analog == 82 && topology == 8,
          "reviewed backend coverage counts");
    // Direct use outside CMP's exact restricted token/job must never load ngspice.
    check(!WorkerSandbox::isContained(), "test process is not the CMP worker");
    SimulationEngine direct;
    bool complete = false, success = true;
    QStringList messages;
    QEventLoop loop;
    QObject::connect(&direct, &SimulationEngine::message, &loop,
                     [&](QString value) { messages.append(value); });
    QObject::connect(&direct, &SimulationEngine::completed, &loop,
                     [&](bool ok)
                     {
                         complete = true;
                         success = ok;
                         loop.quit();
                     });
    QTimer::singleShot(5000, &loop, &QEventLoop::quit);
    direct.start("Policy test\nV1 n 0 5\nR1 n 0 1000\n.op\n.end", false);
    loop.exec();
    check(complete && !success && messages.join(' ').contains("restricted CMP worker"),
          "direct uncontained engine rejected");
#ifdef Q_OS_WIN
    check(GetModuleHandleW(L"ngspice.dll") == nullptr, "uncontained engine must not load solver DLL");
#endif
    std::cout << executionPolicies().size() << " reviewed component policies; " << failures << " failures\n";
    return failures ? 1 : 0;
}
