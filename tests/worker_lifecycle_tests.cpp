#include "cmp/Circuit.h"
#include "cmp/ElectricalDiagnostics.h"
#include "cmp/Simulator.h"
#include <iostream>
#include <memory>
#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
using namespace cmp;
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QString netlist =
        "Worker lifetime\nV1 in 0 5\nR1 in out 1000\nC1 out 0 1u\n.save v(out)\n.tran 10u 1 uic\n.end";
    auto engine = std::make_unique<Simulator>();
    bool killed = false, failed = false, recovered = false;
    QEventLoop loop;
    QObject::connect(engine.get(), &Simulator::stateChanged, &loop,
                     [&](QString state)
                     {
                         if (state == "Running" && !killed)
                         {
                             killed = true;
                             HANDLE process =
                                 OpenProcess(PROCESS_TERMINATE, FALSE, DWORD(engine->workerProcessId()));
                             if (process)
                             {
                                 TerminateProcess(process, 99);
                                 CloseHandle(process);
                             }
                         }
                     });
    QObject::connect(engine.get(), &Simulator::completed, &loop,
                     [&](bool ok)
                     {
                         if (!failed)
                         {
                             failed = !ok;
                             QTimer::singleShot(0, &loop, [&] { engine->start(Circuit::rc(), false); });
                         }
                         else
                         {
                             recovered = ok && engine->hasElectricalReport() &&
                                         engine->electricalReport().incomplete.isEmpty();
                             loop.quit();
                         }
                     });
    QTimer::singleShot(15000, &loop, &QEventLoop::quit);
    engine->start(netlist, true);
    loop.exec();
    if (!killed || !failed || !recovered)
        return 1;
    // Destroying a paused facade must kill its worker and close the Job Object promptly.
    QObject::disconnect(engine.get(), nullptr, &loop, nullptr);
    HANDLE child = nullptr;
    QEventLoop started;
    QObject::connect(engine.get(), &Simulator::stateChanged, &started,
                     [&](QString state)
                     {
                         if (state == "Running")
                         {
                             child = OpenProcess(SYNCHRONIZE, FALSE, DWORD(engine->workerProcessId()));
                             engine->pause();
                             started.quit();
                         }
                     });
    QTimer::singleShot(5000, &started, &QEventLoop::quit);
    engine->start(netlist, true);
    started.exec();
    engine.reset();
    bool exited = child && WaitForSingleObject(child, 2000) == WAIT_OBJECT_0;
    if (child)
        CloseHandle(child);
    std::cout << "crash recovery " << recovered << " paused shutdown " << exited << "\n";
    return exited ? 0 : 2;
}
