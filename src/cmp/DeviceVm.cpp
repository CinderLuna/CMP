#include "cmp/DeviceVm.h"
#include "cmp/DigitalDevices.h"
#include "cmp/ExpandedLibrary.h"
#include <cmath>
namespace cmp
{
DeviceVm::DeviceVm(const Part &p)
    : part(p), states(spec(p.kind).pins.size(), 3), previous(states.size(), 3), memory(256, -1)
{
    const auto layout = digitalLayout(p.kind);
    inputPins = layout.inputs;
    outputPins = layout.outputs;
    supplyPin = layout.supply;
    groundPin = layout.ground;
    static const QMap<QString, Op> operations{
        {"CPU8085", Op::Processor}, {"LOGIC", Op::Logic},     {"CLOCK", Op::Clock},
        {"LPROBE", Op::Probe},      {"SR", Op::Sr},           {"DFF", Op::Dff},
        {"TFF", Op::Tff},           {"JKFF", Op::Jkff},       {"COUNTER", Op::Counter},
        {"SHIFT", Op::Shift},       {"DECODER", Op::Decoder}, {"ROM", Op::Rom},
        {"RAM", Op::Ram},           {"ADC", Op::Adc},         {"DAC", Op::Dac}};
    operation = operations.value(p.kind, Op::Invalid);
    LogicVm::Op gateOperation;
    if (LogicVm::gate(p.kind, gateOperation))
        vmGate = gateVm.load(3, {{gateOperation, 2, 0, 1}}, fault);
    if (vmGate)
        operation = Op::Gate;
    if (operation == Op::Invalid)
        fault = "Unsupported device VM operation";
    if (p.kind == "CPU8085")
    {
        cpu = std::make_unique<Cpu8085>();
        cpu->load(p.data, fault);
    }
    if (p.kind == "COUNTER" || p.kind == "SHIFT")
        value = int(parameter(p, "initial"));
    else if (parameter(p, "initial") < 2)
        value = int(parameter(p, "initial"));
    shift = latch = value;
    for (int i = 0; i < qMin(256, int(p.data.size())); ++i)
        memory[i] = quint8(p.data[i]);
    for (int pin : digitalLayout(p.kind).outputs)
        states[pin] = 2;
}
int DeviceVm::state(int pin) const
{
    return states.value(pin, 3);
}
void DeviceVm::advance(double time, const QVector<double> &volts)
{
    if (!fault.isEmpty())
        return;
    if (operation == Op::Invalid || !std::isfinite(time) || time < 0)
    {
        fault = "Invalid device VM operation/time";
        return;
    }
    const DigitalLayout d{inputPins, outputPins, supplyPin, groundPin};
    double rail = volts.value(d.supply) - volts.value(d.ground);
    QVector<int> in(states.size(), 3);
    for (int i = 0; i < in.size(); ++i)
    {
        double v = volts.value(i) - volts.value(d.ground);
        if (std::isfinite(v) && rail >= 3 && rail <= 5.5)
        {
            if (v <= .3 * rail && v >= -.001)
                in[i] = 0;
            else if (v >= .7 * rail && v <= rail + .001)
                in[i] = 1;
        }
    }
    for (int pin : d.inputs)
        states[pin] = in[pin];
    auto edge = [&](int pin) { return previous[pin] == 0 && in[pin] == 1; };
    auto bits = [&](int first, int count)
    {
        int result = 0;
        for (int i = 0; i < count; ++i)
        {
            if (in[first + i] > 1)
                return -1;
            result |= in[first + i] << i;
        }
        return result;
    };
    auto outputs = [&](int number)
    {
        for (int i = 0; i < d.outputs.size(); ++i)
            states[d.outputs[i]] = number < 0 ? 3 : ((number >> i) & 1);
    };
    const auto k = operation;
    if (k == Op::Processor)
    {
        double elapsed = qMax(0., time - previousTime);
        previousTime = time;
        if (rail < 4.5 || rail > 5.5 || in[8] == 1)
        {
            if (processorPowered)
            {
                cpu->load(part.data, fault);
                processorCycles = 0;
                processorPowered = false;
            }
            for (int pin : d.outputs)
                states[pin] = 2;
            previous = in;
            return;
        }
        if (in[8] > 1)
        {
            outputs(-1);
            previous = in;
            return;
        }
        int byte = bits(0, 8);
        if (byte < 0)
        {
            outputs(-1);
            previous = in;
            return;
        }
        cpu->input[1] = byte;
        processorPowered = true;
        processorCycles += elapsed * part.value;
        if (!std::isfinite(processorCycles) || processorCycles < 0 || processorCycles > 9007199254740991.)
        {
            fault = "8085 clock/time range exceeded";
            outputs(-1);
            return;
        }
        const auto run = cpu->advanceTo(quint64(processorCycles), 100000);
        fault = cpu->fault;
        if (run.reason == RunReason::Budget)
            fault = "8085 instruction budget exceeded at cycle " + QString::number(run.reachedCycle);
        outputs(fault.isEmpty() ? cpu->output[0] : -1);
        previous = in;
        return;
    }
    if (rail < 3 || rail > 5.5)
    {
        for (int pin : d.outputs)
            states[pin] = 2;
        previous = in;
        return;
    }
    if (k == Op::Logic)
        states[0] = int(parameter(part, "level"));
    else if (k == Op::Clock)
        states[0] = time < parameter(part, "phase")
                        ? 0
                        : std::fmod((time - parameter(part, "phase")) * parameter(part, "frequency"), 1) <
                              parameter(part, "duty");
    else if (k == Op::Probe)
        states = in;
    else if (vmGate)
    {
        gateVm.set(0, in[0]);
        gateVm.set(1, in[1]);
        gateVm.rewind();
        if (gateVm.run(1).reason != LogicVm::Stop::Complete)
            fault = "Logic VM execution failed";
        states[d.outputs[0]] = gateVm.get(2);
    }
    else if (k == Op::Sr || k == Op::Dff || k == Op::Jkff || k == Op::Tff)
    {
        int reset = k == Op::Sr     ? 1
                    : k == Op::Jkff ? 3
                                    : 2,
            set = k == Op::Sr     ? 0
                  : k == Op::Jkff ? 4
                                  : 3,
            clock = k == Op::Jkff ? 2 : 1;
        if (in[reset] == 1 && in[set] == 1)
            value = -1;
        else if (in[reset] == 1)
            value = 0;
        else if (in[set] == 1)
            value = 1;
        else if (in[reset] > 1 || in[set] > 1)
            value = -1;
        else if (k != Op::Sr && edge(clock))
        {
            if (k == Op::Dff)
                value = in[0] < 2 && previous[0] == in[0] ? in[0] : -1;
            else if (k == Op::Tff)
            {
                if (in[0] == 1 && value >= 0)
                    value ^= 1;
                else if (in[0] > 1)
                    value = -1;
            }
            else if (in[0] > 1 || in[1] > 1)
                value = -1;
            else if (in[0] && in[1])
                value = value < 0 ? -1 : 1 - value;
            else if (in[0])
                value = 1;
            else if (in[1])
                value = 0;
        }
        states[d.outputs[0]] = value < 0 ? 3 : value;
        states[d.outputs[1]] = value < 0 ? 3 : 1 - value;
    }
    else if (k == Op::Counter)
    {
        if (in[1] == 1)
            value = 0;
        else if (in[1] > 1)
            value = -1;
        else if (edge(0))
        {
            if (in[2] == 1)
                value = bits(3, 4);
            else if (in[2] > 1)
                value = -1;
            else if (value >= 0)
                value = (value + 1) & 15;
        }
        outputs(value);
    }
    else if (k == Op::Shift)
    {
        if (in[3] == 1)
            shift = latch = 0;
        else if (in[3] > 1)
            shift = latch = -1;
        else
        {
            if (edge(1))
                shift = shift < 0 || in[0] > 1 ? -1 : ((shift << 1) | in[0]) & 255;
            if (edge(2))
                latch = shift;
        }
        outputs(latch);
        if (in[4] != 1)
            for (int pin : d.outputs)
                states[pin] = in[4] == 0 ? 2 : 3;
    }
    else if (k == Op::Decoder)
    {
        static const int digits[] = {0x3f, 6, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 7, 0x7f, 0x6f};
        int code = bits(0, 4);
        outputs(in[4] == 1 ? 0 : in[4] > 1 || code < 0 ? -1 : code < 10 ? digits[code] : 0);
    }
    else if (k == Op::Rom || k == Op::Ram)
    {
        int address = bits(0, 8);
        if (k == Op::Ram && in[16] == 1 && previous[18] == 1 && in[18] == 0 && address >= 0)
            memory[address] = bits(8, 8);
        outputs(address < 0 ? -1 : memory[address]);
        if (in[16] == 0 || in[17] == 0 || in[18] == 1)
            for (int pin : d.outputs)
                states[pin] = 2;
        else if (in[16] > 1 || in[17] > 1 || in[18] > 1)
            for (int pin : d.outputs)
                states[pin] = 3;
    }
    else if (k == Op::Adc)
    {
        double reference = volts.value(1) - volts.value(d.ground);
        if (edge(2))
        {
            double input = volts.value(0) - volts.value(d.ground);
            value = reference > 0 && reference <= rail && std::isfinite(input)
                        ? qBound(0, int(std::floor(256 * qBound(0., input / reference, 1.))), 255)
                        : -1;
        }
        outputs(value);
    }
    else if (k == Op::Dac)
    {
        int code = bits(0, 8);
        analog = code < 0 ? .5 : code / 255.;
        states[9] = code < 0 ? 3 : 1;
    }
    previous = in;
}
} // namespace cmp
