#pragma once
#include <QtCore>
#include "cmp/Firmware.h"
#include <memory>
#include "cmp/ProcessorRuntime.h"
namespace cmp
{
// Thread-confined QJSEngine with no host objects or file/network API exposed to guest firmware.
class AvrMachine
{
  public:
    AvrMachine();
    ~AvrMachine();
    bool load(const QByteArray &image, QString &error);
    QVector<int> advance(quint64 cycles, const QVector<double> &inputs, double supply = 5,
                         double reference = 5);
    ProcessorRun advanceTo(quint64 cycles, const QVector<double> &inputs, QVector<int> &outputs,
                           double supply = 5, double reference = 5, quint64 budget = 1000000,
                           const std::atomic_bool *cancel = nullptr);
    int read(int address);
    quint64 cycles();
    QString serial();

  private:
    struct Impl;
    std::unique_ptr<Impl> p;
};
} // namespace cmp
