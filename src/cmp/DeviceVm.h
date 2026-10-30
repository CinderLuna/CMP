#pragma once
#include "cmp/Circuit.h"
#include "cmp/Cpu8085.h"
#include "cmp/LogicVm.h"
#include <memory>
namespace cmp
{
// 0/1 = driven logic, 2 = high impedance, 3 = unknown. Sampled CMOS 0.3/0.7 supply thresholds.
class DeviceVm
{
  public:
    explicit DeviceVm(const Part &);
    void advance(double time, const QVector<double> &volts);
    QString fault;
    Part part;
    QVector<int> states;
    int state(int pin) const;
    double analog = 0;

  private:
    enum class Op
    {
        Invalid,
        Processor,
        Logic,
        Clock,
        Probe,
        Gate,
        Sr,
        Dff,
        Tff,
        Jkff,
        Counter,
        Shift,
        Decoder,
        Rom,
        Ram,
        Adc,
        Dac
    };
    Op operation = Op::Invalid;
    QVector<int> inputPins, outputPins;
    int supplyPin = -1, groundPin = -1;
    std::unique_ptr<Cpu8085> cpu;
    LogicVm gateVm;
    bool vmGate = false;
    double previousTime = 0, processorCycles = 0;
    bool processorPowered = false;
    QVector<int> previous;
    QVector<int> memory;
    int value = -1, shift = -1, latch = -1;
    double nextSample = 0;
};
} // namespace cmp
