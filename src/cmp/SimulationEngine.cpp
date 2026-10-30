#include "cmp/SimulationEngine.h"
#include "cmp/AvrMachine.h"
#include "cmp/Circuit.h"
#include "cmp/DigitalDevices.h"
#include "cmp/ExecutionPolicy.h"
#include "cmp/ExpandedLibrary.h"
#include "cmp/WorkerSandbox.h"
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <limits>
#include <mutex>
#include <ngspice/sharedspice.h>
#include <thread>
#include <vector>
namespace cmp
{
struct SimulationEngine::Impl
{
    SimulationEngine *owner;
    QThread thread;
    QObject *worker;
    QTimer *poll = nullptr;
    QLibrary lib;
    QString runtimePath;
    using Init = int (*)(SendChar *, SendStat *, ControlledExit *, SendData *, SendInitData *,
                         BGThreadRunning *, void *);
    using Command = int (*)(char *);
    using Circ = int (*)(char **);
    using Running = bool (*)();
    struct Board
    {
        Part part;
        std::unique_ptr<AvrMachine> cpu;
        QStringList nodes;
        QVector<int> state = QVector<int>(20, 2);
        double last = -1, cycles = 0;
        bool powered = false;
    };
    std::vector<Board> boards;
    struct Logic
    {
        DigitalDevice device;
        QStringList nodes;
        explicit Logic(const Part &p) : device(p) {}
    };
    std::vector<Logic> logic;

    struct Source
    {
        bool board;
        size_t index;
        int pin;
    };
    QHash<QString, Source> sources;
    double lastAcceptedTime = -1;
    std::atomic<quint64> digitalNs{0}, avrNs{0};
    std::atomic_bool backgroundFinished{false};
    static int externalVoltage(double *value, double, char *name, int, void *context)
    {
        auto *p = static_cast<Impl *>(context);
        *value = 2;
        const auto it = p->sources.constFind(QString::fromLatin1(name).toLower());
        if (it == p->sources.cend())
            return 0;
        const auto &source = it.value();
        if (source.board)
            *value = p->boards[source.index].state.value(source.pin, 2);
        else
        {
            const auto &device = p->logic[source.index].device;
            *value = device.part.kind == "DAC" ? device.analog : device.state(source.pin);
        }
        return 0;
    }
    Command command = nullptr;
    Circ circuit = nullptr;
    Running running = nullptr;
    std::atomic<bool> paused{false}, cancelled{false}, fatal{false}, overflow{false}, completedOnce{true};
    std::atomic<quint64> generation{0}, stopGeneration{0};
    quint64 runGeneration = 0;
    std::atomic<int> total{0};
    std::mutex gateMutex, dataMutex;
    std::condition_variable gate;
    Samples pending;
    QStringList logs;
    bool active = false, initialized = false, paced = false, overflowReported = false;
    std::chrono::steady_clock::time_point wallBase;
    explicit Impl(SimulationEngine *o, QString path)
        : owner(o), worker(new QObject), runtimePath(std::move(path))
    {
        worker->moveToThread(&thread);
        thread.start();
    }
    void state(QString name)
    {
        auto gen = runGeneration;
        QMetaObject::invokeMethod(
            owner,
            [this, gen, name]
            {
                if (gen == generation && (name != "Running" || stopGeneration < gen))
                    emit owner->stateChanged(name);
            },
            Qt::QueuedConnection);
    }
    void finish(bool ok)
    {
        auto gen = runGeneration;
        QMetaObject::invokeMethod(
            owner,
            [this, gen, ok]
            {
                if (gen == generation && !completedOnce.exchange(true))
                    emit owner->completed(ok);
            },
            Qt::QueuedConnection);
    }
    void flushSamples()
    {
        // Queue while holding the buffer mutex so batches posted by two threads retain sample order.
        std::lock_guard<std::mutex> lock(dataMutex);
        if (pending.isEmpty())
            return;
        Samples batch;
        batch.swap(pending);
        auto gen = runGeneration;
        QMetaObject::invokeMethod(
            owner,
            [this, gen, batch = std::move(batch)]
            {
                if (gen == generation && stopGeneration < gen)
                    emit owner->samplesReady(batch);
            },
            Qt::QueuedConnection);
    }
    static int text(char *text, int, void *ctx)
    {
        auto *p = static_cast<Impl *>(ctx);
        QString s = QString::fromUtf8(text);
        std::lock_guard<std::mutex> lock(p->dataMutex);
        if (p->logs.size() < 500)
            p->logs << s;
        if (s.contains("Error:", Qt::CaseInsensitive) ||
            s.contains("Timestep too small", Qt::CaseInsensitive) ||
            s.contains("singular matrix", Qt::CaseInsensitive))
            p->fatal = true;
        return 0;
    }
    static int exit(int, bool, bool, int, void *ctx)
    {
        static_cast<Impl *>(ctx)->fatal = true;
        return 0;
    }
    static int backgroundState(bool exited, int, void *context)
    {
        // ngspice passes fl_exited: false on entry, true after the worker exits.
        static_cast<Impl *>(context)->backgroundFinished = exited;
        return 0;
    }
    static int initData(pvecinfoall, int, void *)
    {
        return 0;
    }
    static int data(pvecvaluesall v, int, int, void *ctx)
    {
        auto *p = static_cast<Impl *>(ctx);
        {
            std::unique_lock<std::mutex> lock(p->gateMutex);
            auto before = std::chrono::steady_clock::now();
            bool wasPaused = p->paused;
            p->gate.wait(lock, [&] { return !p->paused || p->cancelled; });
            if (wasPaused)
                p->wallBase += std::chrono::steady_clock::now() - before;
        }
        if (p->cancelled)
            return 0;
        if (!v || v->veccount < 0 || v->veccount > 32768)
        {
            auto error = QByteArray("Error: solver vector limit exceeded");
            text(error.data(), 0, p);
            return 0;
        }
        Sample sample;
        for (int i = 0; i < v->veccount; i++)
        {
            auto *val = v->vecsa[i];
            QString name = QString::fromUtf8(val->name).toLower();
            if (name == "time")
                sample.time = val->creal;
            else
                sample.values[name] = val->creal;
        }
        if (!std::isfinite(sample.time) || sample.time < p->lastAcceptedTime || sample.time > 60.01)
        {
            p->fatal = true;
            return 0;
        }
        p->lastAcceptedTime = sample.time;
        // One bounded device pass per accepted analog sample. Never settle feedback in an unbounded loop.
        auto modelStart = std::chrono::steady_clock::now();
        for (auto &entry : p->logic)
        {
            QVector<double> inputs;
            for (const auto &node : entry.nodes)
                inputs << (node == "0" ? 0.
                                       : sample.values.value(node, std::numeric_limits<double>::quiet_NaN()));
            entry.device.advance(sample.time, inputs);
            if (!entry.device.fault.isEmpty() && !p->fatal.exchange(true))
            {
                auto message = ("Error: " + entry.device.part.id + ": " + entry.device.fault).toUtf8();
                text(message.data(), 0, p);
            }
            for (int pin = 0; pin < entry.device.states.size(); ++pin)
                sample.values["logic_" + entry.device.part.id.toLower() + "_" + QString::number(pin)] =
                    entry.device.state(pin);
        }
        auto digitalEnd = std::chrono::steady_clock::now();
        p->digitalNs += std::chrono::duration_cast<std::chrono::nanoseconds>(digitalEnd - modelStart).count();
        for (auto &board : p->boards)
            if (sample.time > board.last)
            {
                auto voltage = [&](int pin)
                {
                    auto node = board.nodes[pin];
                    return node == "0" ? 0. : sample.values.value(node);
                };
                double supply = voltage(20) - voltage(21);
                QVector<double> input;
                for (int i = 0; i < 20; ++i)
                    input << voltage(i) - voltage(21);
                if (supply >= 4.5 && supply <= 5.5 && voltage(23) - voltage(21) > .6 * supply)
                {
                    board.cycles += qMax(0., sample.time - qMax(0., board.last)) * 16000000;
                    board.powered = true;
                    QVector<int> result;
                    auto run = board.cpu->advanceTo(quint64(board.cycles), input, result, supply,
                                                    voltage(22) - voltage(21), 100000, &p->cancelled);
                    if (run.reason == RunReason::Deadline && result.size() == 20)
                        board.state = result;
                    else if (run.reason != RunReason::Cancelled)
                    {
                        p->fatal = true;
                        auto error = ("Error: " + board.part.id + ": AVR execution limit/fault at cycle " +
                                      QString::number(run.reachedCycle))
                                         .toUtf8();
                        text(error.data(), 0, p);
                    }
                }
                else
                {
                    if (board.powered)
                    {
                        QString error;
                        board.cpu->load(board.part.data, error);
                        board.powered = false;
                        board.cycles = 0;
                    }
                    board.state.fill(2);
                }
                board.last = sample.time;
            }
        p->avrNs += std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() -
                                                                         digitalEnd)
                        .count();
        if (p->paced)
            while (!p->cancelled && !p->paused &&
                   sample.time - std::chrono::duration<double>(std::chrono::steady_clock::now() - p->wallBase)
                                     .count() >
                       .002)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        bool deliver = false;
        {
            std::lock_guard<std::mutex> lock(p->dataMutex);
            if (++p->total <= qMin(250000, 1000000 / qMax(1, int(sample.values.size()) + 1)))
            {
                p->pending.append(std::move(sample));
                deliver = p->pending.size() >= 128;
            }
            else
                p->overflow = true;
        }
        // ngSpice_Command(bg_run) may briefly block its caller; streaming cannot depend on that thread's
        // timer.
        if (deliver)
            p->flushSamples();
        return 0;
    }
    bool load()
    {
        if (!WorkerSandbox::isContained())
        {
            emit owner->message("Simulation execution requires the restricted CMP worker");
            return false;
        }
        if (initialized)
            return true;
        lib.setFileName(runtimePath.isEmpty() ? QCoreApplication::applicationDirPath() + "/ngspice.dll"
                                              : runtimePath);
        lib.setLoadHints(QLibrary::PreventUnloadHint);
        if (!lib.load())
        {
            emit owner->message("Cannot load bundled ngspice: " + lib.errorString() +
                                " Restore ngspice.dll and its dependency DLLs beside CMP, then retry.");
            return false;
        }
        auto init = reinterpret_cast<Init>(lib.resolve("ngSpice_Init"));
        command = reinterpret_cast<Command>(lib.resolve("ngSpice_Command"));
        circuit = reinterpret_cast<Circ>(lib.resolve("ngSpice_Circ"));
        running = reinterpret_cast<Running>(lib.resolve("ngSpice_running"));
        if (!init || !command || !circuit || !running)
        {
            emit owner->message("ngspice shared-library API is incomplete.");
            return false;
        }
        // This bundled DLL crashes when its no-init APIs are called before Init.
        // The launcher instead confines startup search to a verified read-only spinit directory.
        init(text, nullptr, exit, data, initData, backgroundState, this);
        using InitSync = int (*)(GetVSRCData *, GetISRCData *, GetSyncData *, int *, void *);
        auto initializeSync = reinterpret_cast<InitSync>(lib.resolve("ngSpice_Init_Sync"));
        if (!initializeSync)
        {
            emit owner->message("ngspice synchronization API unavailable.");
            return false;
        }
        initializeSync(externalVoltage, nullptr, nullptr, nullptr, this);
        initialized = true;
        cmd("set nomoremode");
        cmd("set noaskquit");
        cmd("set num_threads=1");
        poll = new QTimer(worker);
        poll->setInterval(30);
        QObject::connect(
            poll, &QTimer::timeout, worker,
            [this]
            {
                flush();
                if (active && fatal && running())
                    cmd("bg_halt");
                if (active && backgroundFinished && !running())
                {
                    active = false;
                    poll->stop();
                    flush();
                    bool ok = !cancelled && !fatal && !overflow && total > 0;
                    state(overflow || fatal ? "Failed" : cancelled ? "Stopped" : ok ? "Complete" : "Failed");
                    finish(ok);
                }
            });
        return true;
    }
    int cmd(QByteArray s)
    {
        return command(s.data());
    }
    void flush()
    {
        flushSamples();
        QStringList lines;
        {
            std::lock_guard<std::mutex> lock(dataMutex);
            lines.swap(logs);
        }
        for (auto &s : lines)
            emit owner->message(s);
        if (overflow && !overflowReported)
        {
            overflowReported = true;
            emit owner->message(
                "Simulation exceeded its sample/value buffer limit; reduce duration or increase step.");
            halt();
        }
    }
    void halt()
    {
        cancelled = true;
        paused = false;
        gate.notify_all();
        if (initialized && running())
            cmd("bg_halt");
    }
};
SimulationEngine::SimulationEngine(QObject *parent, const QString &runtimePath)
    : QObject(parent), p(new Impl(this, runtimePath))
{
    qRegisterMetaType<Samples>();
}
SimulationEngine::~SimulationEngine()
{
    auto *d = p.get();
    d->cancelled = true;
    d->paused = false;
    d->gate.notify_all();
    QMetaObject::invokeMethod(
        d->worker,
        [d]
        {
            d->halt();
            if (d->poll)
                d->poll->stop();
        },
        Qt::BlockingQueuedConnection);
    d->thread.quit();
    d->thread.wait();
    delete d->worker;
}
void SimulationEngine::start(const QString &netlist, bool paced)
{
    startPrepared(netlist, paced, Circuit{});
}
void SimulationEngine::start(const Circuit &c, bool paced)
{
    startPrepared(c.netlist(), paced, c);
}
void SimulationEngine::startPrepared(const QString &netlist, bool paced, const Circuit &document)
{
    auto *d = p.get();
    quint64 gen = ++d->generation;
    d->completedOnce = false;
    QMetaObject::invokeMethod(
        d->worker,
        [d, gen, netlist, paced, document]
        {
            if (gen != d->generation || d->stopGeneration >= gen)
                return;
            d->halt();
            d->active = false;
            if (d->poll)
                d->poll->stop();
            d->runGeneration = gen;
            QString policyError;
            if (!validateExecutionCircuit(document, policyError))
            {
                emit d->owner->message(policyError);
                d->state("Failed");
                d->finish(false);
                return;
            }
            if (!d->load())
            {
                d->state("Failed");
                d->finish(false);
                return;
            }
            d->cmd("destroy all");
            d->paused = false;
            d->cancelled = false;
            d->fatal = false;
            d->overflow = false;
            d->overflowReported = false;
            d->paced = paced;
            d->total = 0;
            d->digitalNs = 0;
            d->avrNs = 0;
            {
                std::lock_guard<std::mutex> lock(d->dataMutex);
                d->pending.clear();
                d->logs.clear();
            }
            d->logic.clear();
            auto logicNodes = document.nodes();
            for (const auto &part : document.parts)
                if (executionPolicy(part.kind)->backend == ExecutionBackend::DeviceVm)
                {
                    Impl::Logic entry(part);
                    for (int pin = 0; pin < spec(part.kind).pins.size(); ++pin)
                        entry.nodes << logicNodes.value(Pin{part.id, pin}.key());
                    d->logic.push_back(std::move(entry));
                }
            d->boards.clear();
            for (const auto &part : document.parts)
                if (executionPolicy(part.kind)->backend == ExecutionBackend::AvrRuntime)
                {
                    Impl::Board board;
                    board.part = part;
                    board.cpu = std::make_unique<AvrMachine>();
                    QString error;
                    if (!board.cpu->load(part.data, error))
                    {
                        emit d->owner->message(error);
                        d->finish(false);
                        return;
                    }
                    auto map = document.nodes();
                    for (int i = 0; i < 24; ++i)
                        board.nodes << map.value(Pin{part.id, i}.key());
                    d->boards.push_back(std::move(board));
                }
            d->sources.clear();
            d->lastAcceptedTime = -1;
            for (size_t i = 0; i < d->boards.size(); ++i)
                for (int pin = 0; pin < 20; ++pin)
                    d->sources.insert("v__uno_" + d->boards[i].part.id.toLower() + "_" + QString::number(pin),
                                      {true, i, pin});
            for (size_t i = 0; i < d->logic.size(); ++i)
                for (int pin : digitalLayout(d->logic[i].device.part.kind).outputs)
                    d->sources.insert("v__dig_" + d->logic[i].device.part.id.toLower() + "_" +
                                          QString::number(pin),
                                      {false, i, pin});
            QList<QByteArray> storage;
            for (auto &s : netlist.split('\n'))
                storage.append(s.toUtf8());
            QVector<char *> ptrs;
            for (auto &s : storage)
                ptrs.append(s.data());
            ptrs.append(nullptr);
            int result = d->circuit(ptrs.data());
            d->flush();
            if (result || d->fatal)
            {
                d->state("Failed");
                d->finish(false);
                return;
            }
            if (gen != d->generation || d->stopGeneration >= gen)
                return;
            d->active = true;
            d->wallBase = std::chrono::steady_clock::now();
            d->state("Running");
            d->backgroundFinished = false;
            if (d->cmd("bg_run"))
            {
                d->state("Failed");
                d->finish(false);
                return;
            }
            d->poll->start();
        },
        Qt::QueuedConnection);
}
QJsonObject SimulationEngine::statistics() const
{
    return {{"digital_ms", double(p->digitalNs.load()) / 1000000.},
            {"avr_bridge_and_cpu_ms", double(p->avrNs.load()) / 1000000.},
            {"accepted_points", p->total.load()}};
}
void SimulationEngine::pause()
{
    if (p->completedOnce)
        return;
    p->paused = true;
    emit stateChanged("Paused");
}
void SimulationEngine::resume()
{
    if (p->completedOnce)
        return;
    p->paused = false;
    p->gate.notify_all();
    emit stateChanged("Running");
}
void SimulationEngine::stop()
{
    auto *d = p.get();
    auto gen = d->generation.load();
    d->stopGeneration = gen;
    d->cancelled = true;
    d->paused = false;
    d->gate.notify_all();
    emit stateChanged("Stopped");
    if (!d->completedOnce.exchange(true))
        emit completed(false);
    QMetaObject::invokeMethod(
        d->worker,
        [d, gen]
        {
            if (d->runGeneration != gen)
                return;
            d->halt();
            d->flush();
            d->active = false;
            if (d->poll)
                d->poll->stop();
        },
        Qt::QueuedConnection);
}
} // namespace cmp
