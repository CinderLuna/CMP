#include "cmp/Circuit.h"
#include "cmp/Simulator.h"
#include <QProcess>
#include <iostream>
#include <memory>
using namespace cmp;
static QString simple(double duration = .02, double step = .00001)
{
    return QString(
               "Reliability RC\nV1 in 0 5\nR1 in out 1000\nC1 out 0 1u\n.save v(out)\n.tran %1 %2 uic\n.end")
        .arg(step, 0, 'g', 12)
        .arg(duration, 0, 'g', 12);
}
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const auto mode = app.arguments().value(1);
    if (mode.isEmpty())
    {
        bool pass = true;
        for (const auto &scenario :
             {"controls", "invalid", "convergence", "missing-runtime", "dense", "large", "shutdown-running",
              "shutdown-paused", "window-running", "window-paused", "window-recovery"})
        {
            QProcess process;
            QString executable = app.applicationFilePath();
            QStringList arguments{scenario};
            if (QString(scenario).startsWith("window-"))
            {
                executable = QCoreApplication::applicationDirPath() + "/cmp.exe";
                arguments = {QString(scenario) == "window-running"  ? "--close-running"
                             : QString(scenario) == "window-paused" ? "--close-paused"
                                                                    : "--recovery-smoke"};
            }
            auto env = QProcessEnvironment::systemEnvironment();
            env.insert("QT_QPA_PLATFORM", "windows");
            process.setProcessEnvironment(env);
            process.start(executable, arguments);
            const bool finished = process.waitForFinished(20000);
            const bool ok =
                finished && process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
            if (!finished)
            {
                process.kill();
                process.waitForFinished(2000);
            }
            std::cout << (ok ? "PASS " : "FAIL ") << scenario << "\n";
            if (!ok)
                std::cerr << process.readAllStandardOutput().toStdString()
                          << process.readAllStandardError().toStdString();
            pass &= ok;
        }
        return pass ? 0 : 1;
    }
    Simulator engine(nullptr, mode == "missing-runtime"
                                  ? QDir::tempPath() + "/cmp-nonexistent-runtime/ngspice.dll"
                                  : QString());
    int completions = 0, count = 0;
    bool success = false;
    QStringList messages;
    double lastTime = -1;
    bool ordered = true;
    QString lastState;
    QObject::connect(&engine, &Simulator::samplesReady, &app,
                     [&](Samples s)
                     {
                         count += s.size();
                         for (const auto &sample : s)
                         {
                             ordered &= sample.time >= lastTime;
                             lastTime = sample.time;
                         }
                     });
    QObject::connect(&engine, &Simulator::stateChanged, &app, [&](QString s) { lastState = s; });
    QObject::connect(&engine, &Simulator::message, &app, [&](QString s) { messages << s; });
    auto run = [&](QString netlist, bool paced = false, int stopMs = -1)
    {
        count = 0;
        success = false;
        messages.clear();
        lastTime = -1;
        ordered = true;
        QEventLoop loop;
        QTimer watchdog;
        watchdog.setSingleShot(true);
        auto connection = QObject::connect(&engine, &Simulator::completed, &loop,
                                           [&](bool ok)
                                           {
                                               ++completions;
                                               success = ok;
                                               loop.quit();
                                           });
        QObject::connect(&watchdog, &QTimer::timeout, &loop, &QEventLoop::quit);
        watchdog.start(12000);
        engine.start(netlist, paced);
        if (stopMs >= 0)
            QTimer::singleShot(stopMs, &loop, [&] { engine.stop(); });
        loop.exec();
        QObject::disconnect(connection);
        return success;
    };
    if (mode == "shutdown-running" || mode == "shutdown-paused")
    {
        engine.start(simple(10), true);
        QEventLoop loop;
        QTimer poll;
        QObject::connect(&poll, &QTimer::timeout, &loop,
                         [&]
                         {
                             if (count > 0 && lastState == "Running")
                                 loop.quit();
                         });
        poll.start(10);
        QTimer::singleShot(5000, &loop, &QEventLoop::quit);
        loop.exec();
        const bool started = count > 0 && lastState == "Running";
        if (mode == "shutdown-paused")
            engine.pause();
        return started ? 0 : 2; // Destructor must halt native solver, release pause gate and join worker.
    }
    if (mode == "missing-runtime")
    {
        bool ok = !run(simple()) && messages.join(" ").contains("Cannot load");
        ok &= !run(simple()) && completions == 2;
        return ok ? 0 : 1;
    }
    if (mode == "invalid" || mode == "convergence")
    {
        const QString netlist =
            mode == "invalid" ? "Broken circuit\nZbad 1 0 invalid\n.op\n.end"
                              : "Contradictory ideal supplies\nV1 n 0 1\nV2 n 0 2\nR1 n 0 1000\n.op\n.end";
        bool ok = !run(netlist) && !messages.isEmpty();
        if (!ok)
            std::cerr << messages.join("\n").toStdString();
        ok &= run(simple()) && count > 100 && ordered;
        return ok ? 0 : 1;
    }
    if (mode == "dense")
    {
        QStringList netlist{"Dense RC bank", "V1 supply 0 5"}, saves;
        for (int i = 0; i < 160; ++i)
        {
            netlist << QString("R%1 supply n%1 %2").arg(i).arg(1000 + i) << QString("C%1 n%1 0 1u").arg(i);
            saves << QString("v(n%1)").arg(i);
        }
        netlist << ".save " + saves.join(' ') << ".tran 10u 10m uic" << ".end";
        return run(netlist.join('\n')) && count > 900 && ordered ? 0 : 1;
    }
    if (mode == "large")
    {
        bool ok = !run(simple(.5, .000001)) && count <= 250000 && count > 100000 &&
                  messages.filter("buffer limit").size() == 1 && lastState == "Failed";
        ok &= run(simple()) && count > 100 && ordered;
        return ok ? 0 : 1;
    }
    if (mode == "controls")
    {
        bool ok = true;
        for (int i = 0; i < 20; ++i)
        {
            engine.start(simple(1), true);
            engine.pause();
            engine.resume();
            engine.stop();
            engine.start(simple(1), true);
            engine.stop();
            ok &= run(simple(.03), true, 10) == false;
            const int stoppedCount = count, completedCount = completions;
            QEventLoop settle;
            QTimer::singleShot(20, &settle, &QEventLoop::quit);
            settle.exec();
            ok &= count == stoppedCount && completions == completedCount;

            ok &= run(simple()) && ordered && count > 100;
            engine.pause();
            engine.resume();
            ok &= lastState == "Complete";
        }
        return ok ? 0 : 1;
    }
    return 2;
}
