#ifdef _WIN32
#include <winsock2.h>
#endif
#include "cmp/Circuit.h"
#include "cmp/ElectricalDiagnostics.h"
#include "cmp/ExecutionPolicy.h"
#include "cmp/SimulationEngine.h"
#include "cmp/WorkerProtocol.h"
#include "cmp/WorkerSandbox.h"
#include <cmath>
#include <cstdio>
#include <thread>
#ifdef Q_OS_WIN
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#endif
using namespace cmp;
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (!WorkerSandbox::isContained())
        return 70;
#ifdef Q_OS_WIN
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
    int channel = _dup(_fileno(stdout));
    // ngspice/third-party printf must never corrupt protocol stdout.
    freopen("NUL", "wb", stdout);
    QFile output;
    if (!output.open(channel, QIODevice::WriteOnly, QFileDevice::AutoCloseHandle))
        return 71;
#endif
    // Do not let ngspice discover host startup scripts. The launcher verifies this directory
    // contains only a fixed minimal spinit and grants the AppContainer read/execute access.
    const auto startupDirectory = QDir::toNativeSeparators(QDir::currentPath()).toLocal8Bit();
    for (const auto &key : {"SPICE_SCRIPTS", "SPICE_USERINIT_DIR", "SPICE_LIB_DIR", "HOME", "USERPROFILE"})
        qputenv(key, startupDirectory);
    bool terminated = false, started = false;
    auto send = [&](QJsonObject object)
    {
        if (terminated)
            return;
        auto bytes = ipc::frame(object);
        if (bytes.isEmpty() || output.write(bytes) != bytes.size() || !output.flush())
        {
            terminated = true;
            app.exit(72);
        }
    };
    const auto args = app.arguments();
    if (args.contains("--sandbox-check"))
    {
        send({{"type", "probe"}, {"contained", WorkerSandbox::isContained()}});
        output.flush();
        std::_Exit(0);
    }
    if (args.contains("--js-interpreter"))
        qputenv("QV4_FORCE_INTERPRETER", "1");
    if (args.contains("--interpreter"))
        qputenv("CMP_VM_INTERPRETER", "1");
    QString runtime;
    auto index = args.indexOf("--runtime");
    if (index >= 0)
        runtime = args.value(index + 1);
    auto probe = args.indexOf("--sandbox-probe");
    if (probe >= 0)
    {
        const auto path = args.value(probe + 1);
        QFile read(path), write(path + ".write");
        const bool readDenied = !read.open(QIODevice::ReadOnly);
        const bool writeDenied = !write.open(QIODevice::WriteOnly);
        WSADATA winsock{};
        bool networkDenied = false;
        int networkError = 0;
        if (WSAStartup(MAKEWORD(2, 2), &winsock) == 0)
        {
            auto socketHandle = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            if (socketHandle == INVALID_SOCKET)
                networkDenied = true;
            else
            {
                sockaddr_in address{};
                address.sin_family = AF_INET;
                auto portIndex = args.indexOf("--port");
                address.sin_port = htons(quint16(args.value(portIndex + 1).toUInt()));
                address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
                u_long nonblocking = 1;
                ioctlsocket(socketHandle, FIONBIO, &nonblocking);
                int connection =
                    connect(socketHandle, reinterpret_cast<sockaddr *>(&address), sizeof(address));
                networkError = connection == 0 ? 0 : WSAGetLastError();
                if (networkError == WSAEWOULDBLOCK)
                {
                    fd_set writable, exceptional;
                    FD_ZERO(&writable);
                    FD_ZERO(&exceptional);
                    FD_SET(socketHandle, &writable);
                    FD_SET(socketHandle, &exceptional);
                    timeval timeout{2, 0};
                    if (select(0, nullptr, &writable, &exceptional, &timeout) > 0)
                    {
                        int size = sizeof(networkError);
                        getsockopt(socketHandle, SOL_SOCKET, SO_ERROR,
                                   reinterpret_cast<char *>(&networkError), &size);
                    }
                    else
                        networkError = WSAETIMEDOUT;
                }
                networkDenied = networkError != 0;
                closesocket(socketHandle);
            }
            WSACleanup();
        }
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION child{};
        auto binary = QDir::toNativeSeparators(app.applicationFilePath()).toStdWString();
        bool childDenied = !CreateProcessW(binary.c_str(), nullptr, nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                                           nullptr, nullptr, &startup, &child);
        if (!childDenied)
        {
            TerminateProcess(child.hProcess, 99);
            CloseHandle(child.hProcess);
            CloseHandle(child.hThread);
        }
        send({{"type", "probe"},
              {"contained", WorkerSandbox::isContained()},
              {"readDenied", readDenied},
              {"writeDenied", writeDenied},
              {"networkDenied", networkDenied},
              {"networkError", networkError},
              {"childDenied", childDenied}});
        output.flush();
        std::_Exit(0);
    }
    Circuit document;
    bool hasDocument = false;
    bool engineRunning = false, pauseRequested = false;
    Samples capture;
    SimulationEngine engine(nullptr, runtime);
    QObject::connect(&engine, &SimulationEngine::samplesReady, &app,
                     [&](const Samples &samples)
                     {
                         if (hasDocument)
                             capture += samples;
                         QJsonArray rows;
                         for (const auto &sample : samples)
                         {
                             QJsonObject values;
                             for (auto it = sample.values.begin(); it != sample.values.end(); ++it)
                                 values[it.key()] = std::isfinite(it.value())
                                                        ? QJsonValue(it.value())
                                                        : QJsonValue(std::isnan(it.value()) ? "nan"
                                                                     : it.value() > 0       ? "+inf"
                                                                                            : "-inf");
                             rows.append(QJsonObject{{"time", sample.time}, {"values", values}});
                         }
                         QString error;
                         if (!ipc::writeSampleRows(
                                 rows,
                                 [&](QJsonObject frame)
                                 {
                                     send(frame);
                                     return !terminated;
                                 },
                                 error))
                         {
                             send({{"type", "message"}, {"text", error}});
                             send({{"type", "complete"}, {"ok", false}});
                             terminated = true;
                             app.exit(72);
                         }
                     });
    QObject::connect(&engine, &SimulationEngine::message, &app,
                     [&](QString text) { send({{"type", "message"}, {"text", text.left(8192)}}); });
    QObject::connect(&engine, &SimulationEngine::stateChanged, &app,
                     [&](QString state)
                     {
                         if (state == "Running")
                         {
                             engineRunning = true;
                             if (pauseRequested)
                             {
                                 engine.pause();
                                 return;
                             }
                         }
                         if (state == "Complete" || state == "Failed" || state == "Stopped")
                             engineRunning = false;
                         send({{"type", "state"}, {"state", state}});
                     });
    QObject::connect(&engine, &SimulationEngine::completed, &app,
                     [&](bool ok)
                     {
                         if (hasDocument)
                         {
                             const auto report = diagnoseElectrical(document, capture);
                             QJsonArray issues, incomplete;
                             for (const auto &issue : report.issues)
                                 issues.append(QJsonObject{{"component", issue.component},
                                                           {"rule", issue.rule},
                                                           {"unit", issue.unit},
                                                           {"time", issue.firstTime},
                                                           {"peak", issue.peak},
                                                           {"limit", issue.limit},
                                                           {"operatingRange", issue.operatingRange},
                                                           {"lowerBound", issue.lowerBound}});
                             for (const auto &line : report.incomplete)
                                 incomplete.append(line);
                             send({{"type", "diagnostics"}, {"issues", issues}, {"incomplete", incomplete}});
                         }
                         send({{"type", "statistics"}, {"metrics", engine.statistics()}});
                         send({{"type", "complete"}, {"ok", ok}});
                         terminated = true;
                         app.quit();
                     });
    auto reject = [&](const QString &why)
    {
        send({{"type", "message"}, {"text", why}});
        send({{"type", "complete"}, {"ok", false}});
        terminated = true;
        app.exit(73);
    };
    // Blocking pipe reads stay off the event loop so pause and streamed results remain responsive.
    // The process exits without joining this read if its parent still holds the pipe open.
    std::thread(
        [&app, &engine, &started, &reject, &document, &hasDocument, &engineRunning, &pauseRequested]
        {
            ipc::Decoder decoder;
            QFile input;
            input.open(stdin, QIODevice::ReadOnly);
            while (true)
            {
                char bytes[65536];
#ifdef Q_OS_WIN
                auto count = _read(_fileno(stdin), bytes, sizeof(bytes));
#else
                auto count = input.read(bytes, sizeof(bytes));
#endif
                if (count <= 0)
                {
                    QMetaObject::invokeMethod(&app, [&app] { app.exit(74); });
                    return;
                }
                QVector<QJsonObject> messages;
                QString error;
                if (!decoder.feed(QByteArray(bytes, count), messages, error))
                {
                    QMetaObject::invokeMethod(&app, [&reject, error] { reject(error); });
                    return;
                }
                for (const auto &object : messages)
                    QMetaObject::invokeMethod(
                        &app,
                        [&engine, &started, &reject, &document, &hasDocument, &engineRunning, &pauseRequested,
                         object]
                        {
                            const auto type = object["type"].toString();
                            if (type == "start" && !started && object["paced"].isBool() && object.size() == 4)
                            {
                                started = true;
                                QString error;
                                if (object.contains("circuit") && !object.contains("netlist") &&
                                    object["circuit"].isObject())
                                {
                                    Circuit circuit;
                                    if (!Circuit::parse(object["circuit"].toObject(), circuit, error))
                                    {
                                        reject(error);
                                        return;
                                    }
                                    // Validation may deliberately be bypassed by diagnostic fixtures; parsing
                                    // still enforces structure and parameter limits. Model generation is
                                    // trusted code.
                                    if (!validateExecutionCircuit(circuit, error))
                                    {
                                        reject(error);
                                        return;
                                    }
                                    auto netlist = circuit.netlist();
                                    if (!ipc::safeNetlist(netlist, error))
                                    {
                                        reject(error);
                                        return;
                                    }
                                    document = circuit;
                                    hasDocument = true;
                                    engine.start(circuit, object["paced"].toBool());
                                }
                                else if (object["netlist"].isString() && !object.contains("circuit"))
                                {
                                    const auto netlist = object["netlist"].toString();
                                    if (!ipc::safeNetlist(netlist, error))
                                    {
                                        reject(error);
                                        return;
                                    }
                                    engine.start(netlist, object["paced"].toBool());
                                }
                                else
                                    reject("Invalid start request");
                            }
                            else if (started && type == "pause" && object.size() == 2)
                            {
                                pauseRequested = true;
                                if (engineRunning)
                                    engine.pause();
                            }
                            else if (started && type == "resume" && object.size() == 2)
                            {
                                pauseRequested = false;
                                if (engineRunning)
                                    engine.resume();
                            }
                            else
                                reject("Invalid worker command/state");
                        });
            }
        })
        .detach();
    app.exec();
    output.flush();
    // Bound shutdown even if a native solver is stuck, and end the detached pipe reader atomically.
    std::_Exit(0);
}
