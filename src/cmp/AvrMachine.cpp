#include "cmp/AvrMachine.h"
#include <QJSEngine>
#include <QJSValue>
#include <cmath>
static void initAvrResources()
{
    Q_INIT_RESOURCE(cmp_avr);
}
namespace cmp
{
struct AvrMachine::Impl
{
    QThread thread;
    QObject *actor = new QObject;
    QJSEngine *engine = nullptr;
    QJSValue machine;
    Impl()
    {
        actor->moveToThread(&thread);
        thread.start();
        QMetaObject::invokeMethod(
            actor,
            [&]
            {
                engine = new QJSEngine(actor);
                initAvrResources();
                QFile f(":/runtime/avr.js");
                f.open(QIODevice::ReadOnly);
                engine->evaluate(QString::fromUtf8(f.readAll()), "avr8js-0.21.1.js");
            },
            Qt::BlockingQueuedConnection);
    }
    ~Impl()
    {
        QMetaObject::invokeMethod(
            actor,
            [&]
            {
                machine = QJSValue();
                delete engine;
                engine = nullptr;
            },
            Qt::BlockingQueuedConnection);
        QMetaObject::invokeMethod(actor, &QObject::deleteLater, Qt::QueuedConnection);
        thread.quit();
        thread.wait();
    }
};
AvrMachine::AvrMachine() : p(std::make_unique<Impl>()) {}
AvrMachine::~AvrMachine() = default;
bool AvrMachine::load(const QByteArray &image, QString &error)
{
    error.clear();
    bool ok = false;
    QMetaObject::invokeMethod(
        p->actor,
        [&]
        {
            p->machine = QJSValue();
            if (image.isEmpty() || image.size() > 32768)
            {
                error = "Uno firmware must contain 1–32768 bytes.";
                return;
            }
            auto data = p->engine->newArray(image.size());
            for (int i = 0; i < image.size(); ++i)
                data.setProperty(i, quint8(image[i]));
            p->machine = p->engine->globalObject().property("createUno").call({data});
            ok = p->machine.isObject() && !p->machine.isError();
            if (!ok)
            {
                error = p->machine.toString();
                p->machine = QJSValue();
            }
        },
        Qt::BlockingQueuedConnection);
    return ok;
}
QVector<int> AvrMachine::advance(quint64 target, const QVector<double> &inputs, double supply,
                                 double reference)
{
    QVector<int> outputs;
    const auto result = advanceTo(target, inputs, outputs, supply, reference);
    return result.reason == RunReason::Deadline ? outputs : QVector<int>();
}
ProcessorRun AvrMachine::advanceTo(quint64 target, const QVector<double> &inputs, QVector<int> &outputs,
                                   double supply, double reference, quint64 budget,
                                   const std::atomic_bool *cancel)
{
    ProcessorRun result;
    outputs.clear();
    if (target > 9007199254740991ULL)
        return result;
    QMetaObject::invokeMethod(
        p->actor,
        [&]
        {
            if (!p->machine.isObject())
                return;
            auto data = p->engine->newArray(20);
            for (int i = 0; i < 20; ++i)
                data.setProperty(i, inputs.value(i));
            const double initialCycle = p->machine.property("cycles").callWithInstance(p->machine).toNumber();
            if (!std::isfinite(initialCycle) || initialCycle < 0 || initialCycle > 9007199254740991.)
                return;
            result.reachedCycle = quint64(initialCycle);
            QJSValue pins;
            do
            {
                if (cancel && cancel->load(std::memory_order_relaxed))
                {
                    result.reason = RunReason::Cancelled;
                    return;
                }
                const auto amount = qMin(quint64(256), budget - result.instructions);
                pins = p->machine.property("advance").callWithInstance(
                    p->machine, {double(target), data, supply, reference, double(amount)});
                if (pins.isError())
                    return;
                auto report = p->machine.property("report").callWithInstance(p->machine);
                if (report.isError())
                    return;
                const double reachedValue = report.property(0).toNumber(),
                             workValue = report.property(1).toNumber();
                if (!std::isfinite(reachedValue) || reachedValue < 0 || reachedValue > 9007199254740991. ||
                    !std::isfinite(workValue) || workValue < 0 || workValue > double(amount))
                    return;
                auto reached = quint64(reachedValue);
                auto work = quint64(workValue);
                if (reached < result.reachedCycle || work > amount)
                    return;
                result.reachedCycle = reached;
                result.instructions += work;
                if (!work && reached < target)
                    break;
            } while (result.reachedCycle < target && result.instructions < budget);
            for (int i = 0; i < 20; ++i)
                outputs << pins.property(i).toInt();
            result.reason = result.reachedCycle >= target ? RunReason::Deadline : RunReason::Budget;
        },
        Qt::BlockingQueuedConnection);
    return result;
}
int AvrMachine::read(int address)
{
    int result = -1;
    if (address < 0 || address >= 2304)
        return result;
    QMetaObject::invokeMethod(
        p->actor,
        [&]
        {
            if (p->machine.isObject())
                result = p->machine.property("read").callWithInstance(p->machine, {address}).toInt();
        },
        Qt::BlockingQueuedConnection);
    return result;
}
quint64 AvrMachine::cycles()
{
    quint64 result = 0;
    QMetaObject::invokeMethod(
        p->actor,
        [&]
        {
            if (!p->machine.isObject())
                return;
            const auto value = p->machine.property("cycles").callWithInstance(p->machine).toNumber();
            if (std::isfinite(value) && value >= 0 && value <= 9007199254740991.)
                result = quint64(value);
        },
        Qt::BlockingQueuedConnection);
    return result;
}
QString AvrMachine::serial()
{
    QString result;
    QMetaObject::invokeMethod(
        p->actor,
        [&]
        {
            if (p->machine.isObject())
                result = p->machine.property("serial").callWithInstance(p->machine).toString();
        },
        Qt::BlockingQueuedConnection);
    return result;
}
} // namespace cmp
