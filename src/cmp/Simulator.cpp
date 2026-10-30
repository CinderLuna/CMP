#include "cmp/Simulator.h"
#include "cmp/Circuit.h"
#include "cmp/ElectricalDiagnostics.h"
#include "cmp/WorkerProtocol.h"
#include "cmp/WorkerSandbox.h"
#include <cmath>
#include <limits>
namespace cmp
{
struct Simulator::Impl
{
    Simulator *owner;
    std::unique_ptr<WorkerSandbox> sandbox;
    std::unique_ptr<QProcess> process;
    ipc::Decoder decoder;
    QTimer watchdog;
    QString runtimePath;
    bool done = true, paused = false;
    int valueCount = 0, sampleCount = 0, messages = 0;
    double lastTime = -1;
    quint64 generation = 0;
    qsizetype receivedBytes = 0, stderrBytes = 0;
    int frames = 0;
    bool hasReport = false;
    ElectricalReport report;
    Samples deferredSamples;
    QElapsedTimer elapsed;
    QJsonObject metrics;
    double startupMs = -1, wallMs = 0;
    explicit Impl(Simulator *o, QString path) : owner(o), runtimePath(std::move(path))
    {
        watchdog.setSingleShot(true);
        QObject::connect(&watchdog, &QTimer::timeout, owner,
                         [this] { fail("Simulation worker exceeded wall time limit."); });
    }
    void dispose()
    {
        watchdog.stop();
        if (process)
        {
            process->disconnect(owner);
            if (process->state() != QProcess::NotRunning)
            {
                process->kill();
                process->waitForFinished(2000);
            }
            process.reset();
        }
        sandbox.reset();
    }
    void finish(bool ok)
    {
        if (done)
            return;
        const auto finishingGeneration = generation;
        if (!deferredSamples.isEmpty())
        {
            Samples finalSamples;
            finalSamples.swap(deferredSamples);
            paused = false;
            emit owner->samplesReady(finalSamples);
            if (generation != finishingGeneration || done)
                return;
        }
        done = true;
        wallMs = elapsed.elapsed();
        watchdog.stop();
        // The terminal frame is the last permitted worker action. Never wait for third-party
        // DLL teardown or allow a worker to keep executing after reporting completion.
        if (process && process->state() != QProcess::NotRunning)
            process->kill();
        auto epoch = generation;
        emit owner->stateChanged(ok ? "Complete" : "Failed");
        if (epoch == generation)
            emit owner->completed(ok);
    }
    void fail(const QString &why)
    {
        if (done)
            return;
        if (process)
            process->kill();
        auto epoch = generation;
        emit owner->message(why);
        if (epoch == generation)
            finish(false);
    }
    bool send(QJsonObject object)
    {
        auto bytes = ipc::frame(object);
        if (!process || bytes.isEmpty() || process->bytesToWrite() + bytes.size() > ipc::MaximumFrame + 8)
            return false;
        return process->write(bytes) == bytes.size();
    }
    void read()
    {
        if (done || !process)
            return;
        const auto epoch = generation;
        QVector<QJsonObject> batch;
        QString error;
        while (process->bytesAvailable() && !done)
        {
            const auto bytes = process->read(65536);
            receivedBytes += bytes.size();
            if (receivedBytes > 96 * 1024 * 1024)
            {
                fail("Worker IPC output quota exceeded");
                return;
            }
            if (!decoder.feed(bytes, batch, error))
            {
                fail(error);
                return;
            }
            for (const auto &object : batch)
            {
                if (++frames > 20000)
                {
                    fail("Worker message quota exceeded");
                    return;
                }
                auto type = object["type"].toString();
                const int expectedFields = type == "diagnostics" ? 4 : 3;
                if (object.size() != expectedFields)
                {
                    fail("Invalid worker response fields");
                    return;
                }
                if (type == "samples")
                {
                    if (!object["samples"].isArray())
                    {
                        fail("Invalid sample message");
                        return;
                    }
                    const auto array = object["samples"].toArray();
                    if (array.isEmpty() || array.size() > 128)
                    {
                        fail("Sample batch limit exceeded");
                        return;
                    }
                    Samples samples;
                    for (const auto &item : array)
                    {
                        if (!item.isObject())
                        {
                            fail("Invalid sample object");
                            return;
                        }
                        const auto row = item.toObject();
                        if (row.size() != 2 || !row["time"].isDouble() || !row["values"].isObject())
                        {
                            fail("Invalid sample fields");
                            return;
                        }
                        Sample sample;
                        sample.time = row["time"].toDouble();
                        if (!std::isfinite(sample.time) || sample.time < lastTime || sample.time < 0 ||
                            sample.time > 60.01)
                        {
                            fail("Invalid sample timestamp");
                            return;
                        }
                        lastTime = sample.time;
                        auto values = row["values"].toObject();
                        if (++sampleCount > 250000 || values.size() > 32768 ||
                            (valueCount += values.size() + 1) > 1000000)
                        {
                            fail("Worker output buffer limit exceeded");
                            return;
                        }
                        for (auto it = values.begin(); it != values.end(); ++it)
                        {
                            if (it.key().size() > 256)
                            {
                                fail("Invalid sample name");
                                return;
                            }
                            double value;
                            if (it.value().isDouble())
                                value = it.value().toDouble();
                            else if (it.value() == "nan")
                                value = std::numeric_limits<double>::quiet_NaN();
                            else if (it.value() == "+inf")
                                value = std::numeric_limits<double>::infinity();
                            else if (it.value() == "-inf")
                                value = -std::numeric_limits<double>::infinity();
                            else
                            {
                                fail("Invalid sample value");
                                return;
                            }
                            sample.values[it.key()] = value;
                        }
                        samples.append(std::move(sample));
                    }
                    if (paused)
                        deferredSamples += samples;
                    else
                        emit owner->samplesReady(samples);
                }
                else if (type == "message" && object["text"].isString() &&
                         object["text"].toString().size() <= 8192 && ++messages <= 10000)
                    emit owner->message(object["text"].toString());
                else if (type == "state" &&
                         QStringList{"Running", "Paused", "Stopped", "Failed", "Complete"}.contains(
                             object["state"].toString()))
                {
                    if (object["state"] == "Running" && startupMs < 0)
                        startupMs = elapsed.elapsed();
                    if (object["state"] == "Running" && paused)
                        send({{"type", "pause"}});
                    else
                        emit owner->stateChanged(object["state"].toString());
                }
                else if (type == "statistics" && object["metrics"].isObject())
                {
                    const auto incoming = object["metrics"].toObject();
                    if (incoming.size() > 32)
                    {
                        fail("Invalid metric count");
                        return;
                    }
                    for (auto it = incoming.begin(); it != incoming.end(); ++it)
                        if (it.key().size() > 64 || !it.value().isDouble())
                        {
                            fail("Invalid worker metric");
                            return;
                        }
                    metrics = incoming;
                }
                else if (type == "diagnostics" && object["issues"].isArray() &&
                         object["incomplete"].isArray())
                {
                    if (object["issues"].toArray().size() > 10000 ||
                        object["incomplete"].toArray().size() > 10000)
                    {
                        fail("Diagnostic quota exceeded");
                        return;
                    }
                    ElectricalReport decoded;
                    for (const auto &value : object["issues"].toArray())
                    {
                        auto item = value.toObject();
                        if (item.size() != 8 || !item["component"].isString() || !item["rule"].isString() ||
                            !item["unit"].isString() || !item["time"].isDouble() ||
                            !item["peak"].isDouble() || !item["limit"].isDouble() ||
                            !item["operatingRange"].isBool() || !item["lowerBound"].isBool() ||
                            item["component"].toString().size() > 256 ||
                            item["rule"].toString().size() > 1024 || item["unit"].toString().size() > 32)
                        {
                            fail("Invalid worker diagnostic");
                            return;
                        }
                        decoded.issues.append({item["component"].toString(), item["rule"].toString(),
                                               item["unit"].toString(), item["time"].toDouble(),
                                               item["peak"].toDouble(), item["limit"].toDouble(),
                                               item["operatingRange"].toBool(), item["lowerBound"].toBool()});
                    }
                    for (const auto &value : object["incomplete"].toArray())
                    {
                        if (!value.isString() || value.toString().size() > 8192)
                        {
                            fail("Invalid incomplete diagnostic");
                            return;
                        }
                        decoded.incomplete.append(value.toString());
                    }
                    report = std::move(decoded);
                    hasReport = true;
                }
                else if (type == "complete" && object["ok"].isBool())
                    finish(object["ok"].toBool());
                else
                {
                    fail("Invalid worker response");
                    return;
                }
                if (done || epoch != generation)
                    return;
            }
            batch.clear();
        }
    }
};
Simulator::Simulator(QObject *parent, const QString &runtimePath)
    : QObject(parent), p(std::make_unique<Impl>(this, runtimePath))
{
    qRegisterMetaType<Samples>();
}
Simulator::~Simulator()
{
    p->done = true;
    p->dispose();
}
void Simulator::start(const QString &netlist, bool paced)
{
    startPrepared(netlist, paced, Circuit{});
}
void Simulator::start(const Circuit &c, bool paced)
{
    startPrepared({}, paced, c);
}
void Simulator::startPrepared(const QString &netlist, bool paced, const Circuit &document)
{
    ++p->generation;
    p->done = true;
    p->dispose();
    p->decoder = {};
    p->done = false;
    p->paused = false;
    p->elapsed.start();
    p->metrics = {};
    p->startupMs = -1;
    p->wallMs = 0;
    p->deferredSamples.clear();
    p->hasReport = false;
    p->report = {};
    p->receivedBytes = p->stderrBytes = 0;
    p->frames = 0;
    p->lastTime = -1;
    p->valueCount = p->sampleCount = p->messages = 0;
    // Defer startup so failure/completion obey the asynchronous API, including invalid input.
    auto *process = new QProcess(this);
    p->process.reset(process);
    QTimer::singleShot(
        0, process,
        [this, process, netlist, paced, document]
        {
            if (p->done || p->process.get() != process)
                return;
            const auto executable = QCoreApplication::applicationDirPath() + "/cmp_worker.exe";
            QString error;
            p->sandbox = std::make_unique<WorkerSandbox>();
            if (!p->sandbox->prepare(*process, executable, error))
            {
                p->fail(error);
                return;
            }
            if (document.parts.size() > 500 || document.wires.size() > 3000 ||
                document.probes.size() > 1024 || document.labels.size() > 3000 ||
                netlist.size() > ipc::MaximumFrame / 2)
            {
                p->fail("Simulation input quota exceeded");
                return;
            }
            qsizetype payload = 0;
            for (const auto &part : document.parts)
            {
                payload += part.data.size();
                if (payload > 4 * 1024 * 1024 || part.parameters.size() > 64 || part.ratings.size() > 32)
                {
                    p->fail("Simulation component payload quota exceeded");
                    return;
                }
            }
            QJsonObject request{{"type", "start"}, {"paced", paced}};
            if (netlist.isEmpty())
                request["circuit"] = document.json();
            else
                request["netlist"] = netlist;
            if (ipc::frame(request).isEmpty())
            {
                p->fail("Simulation request exceeds IPC limit");
                return;
            }
            QObject::connect(process, &QProcess::started, this,
                             [this, request]
                             {
                                 if (!p->send(request))
                                     p->fail("Cannot send worker request");
                             });
            QObject::connect(process, &QProcess::readyReadStandardOutput, this, [this] { p->read(); });
            QObject::connect(process, &QProcess::readyReadStandardError, this,
                             [this]
                             {
                                 auto bytes = p->process->readAllStandardError();
                                 p->stderrBytes += bytes.size();
                                 if (p->stderrBytes > 256 * 1024)
                                     p->fail("Worker stderr quota exceeded");
                             });
            QObject::connect(process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError)
                             { p->fail("Simulation worker failed: " + p->process->errorString()); });
            QObject::connect(process, &QProcess::finished, this,
                             [this](int, QProcess::ExitStatus)
                             {
                                 p->read();
                                 if (!p->done)
                                     p->fail(p->decoder.atBoundary()
                                                 ? "Simulation worker exited before completion"
                                                 : "Truncated worker response");
                             });

            QProcessEnvironment environment;
            for (const auto &key :
                 {"SystemRoot", "WINDIR", "TEMP", "TMP", "USERPROFILE", "LOCALAPPDATA", "APPDATA", "USERNAME",
                  "USERDOMAIN", "HOMEDRIVE", "HOMEPATH", "ProgramData"})
                environment.insert(key, qEnvironmentVariable(key));
            environment.insert("PATH", QCoreApplication::applicationDirPath() + ";" +
                                           qEnvironmentVariable("SystemRoot") + "/System32");
            process->setProcessEnvironment(environment);
            QStringList args;
            if (qEnvironmentVariableIsSet("QV4_FORCE_INTERPRETER"))
                args << "--js-interpreter";
            if (qEnvironmentVariableIsSet("CMP_VM_INTERPRETER"))
                args << "--interpreter";
            if (!p->runtimePath.isEmpty())
                args << "--runtime" << p->runtimePath;
            process->start(executable, args);
            if (!p->paused)
                p->watchdog.start(120000);
        });
}
void Simulator::pause()
{
    if (p->done)
        return;
    p->paused = true;
    p->watchdog.stop();
    p->send({{"type", "pause"}});
    emit stateChanged("Paused");
}
void Simulator::resume()
{
    if (p->done)
        return;
    const auto epoch = p->generation;
    p->paused = false;
    Samples deferred;
    deferred.swap(p->deferredSamples);
    if (!deferred.isEmpty())
        emit samplesReady(deferred);
    if (epoch != p->generation || p->done)
        return;
    p->watchdog.start(120000);
    p->send({{"type", "resume"}});
    emit stateChanged("Running");
}
QJsonObject Simulator::statistics() const
{
    auto result = p->metrics;
    result["startup_ms"] = p->startupMs;
    result["wall_ms"] = p->done ? p->wallMs : double(p->elapsed.elapsed());
    result["ipc_bytes"] = double(p->receivedBytes);
    result["sample_count"] = p->sampleCount;
    return result;
}
bool Simulator::hasElectricalReport() const
{
    return p->hasReport;
}
ElectricalReport Simulator::electricalReport() const
{
    return p->report;
}
qint64 Simulator::workerProcessId() const
{
    return p->process ? p->process->processId() : 0;
}
void Simulator::stop()
{
    if (p->done)
        return;
    ++p->generation;
    p->done = true;
    p->watchdog.stop();
    if (p->process)
        p->process->kill();
    emit stateChanged("Stopped");
    emit completed(false);
}
} // namespace cmp
