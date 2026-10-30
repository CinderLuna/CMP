#include "cmp/ExecutionPolicy.h"
#include "cmp/Circuit.h"
namespace cmp
{
const QVector<ComponentExecution> &executionPolicies()
{
    // Explicit allowlist: adding a catalog entry requires a reviewed execution policy.
    // No unknown component or behavior is implicitly treated as an analog model.
    static const QVector<ComponentExecution> policies{
        {"R", "", ExecutionBackend::AnalogSolver, NativeLowering::None},
        {"C", "", ExecutionBackend::AnalogSolver, NativeLowering::None},
        {"CP", "", ExecutionBackend::AnalogSolver, NativeLowering::None},
        {"GND", "", ExecutionBackend::Topology, NativeLowering::None},
        {"V", "", ExecutionBackend::AnalogSolver, NativeLowering::None},
        {"PULSE", "", ExecutionBackend::AnalogSolver, NativeLowering::None},
        {"D", "", ExecutionBackend::AnalogSolver, NativeLowering::None},
        {"LED", "", ExecutionBackend::AnalogSolver, NativeLowering::None},
        {"NPN", "", ExecutionBackend::AnalogSolver, NativeLowering::None},
        {"555", "", ExecutionBackend::AnalogSolver, NativeLowering::None},
        {"LM339", "", ExecutionBackend::AnalogSolver, NativeLowering::None},
        {"POT", "pot", ExecutionBackend::AnalogSolver, NativeLowering::None},
        {"SW", "switch", ExecutionBackend::AnalogSolver, NativeLowering::None},
        {"PUSH", "switch", ExecutionBackend::AnalogSolver, NativeLowering::None},
        {"PNP", "pnp", ExecutionBackend::AnalogSolver, NativeLowering::None},
        {"SINE", "sine", ExecutionBackend::AnalogSolver, NativeLowering::None},
        {"I", "current", ExecutionBackend::AnalogSolver, NativeLowering::None},
        {"RECT", "diode", ExecutionBackend::AnalogSolver, NativeLowering::None},
        {"ZENER", "zener", ExecutionBackend::AnalogSolver, NativeLowering::None},
        {"SCHOTTKY", "diode", ExecutionBackend::AnalogSolver, NativeLowering::None},
        {"L", "inductor", ExecutionBackend::AnalogSolver, NativeLowering::None},
        {"LED_AMBER", "led", ExecutionBackend::AnalogSolver, NativeLowering::None},
        {"LED_GREEN", "led", ExecutionBackend::AnalogSolver, NativeLowering::None},
        {"LED_BLUE", "led", ExecutionBackend::AnalogSolver, NativeLowering::None},
        {"LED_WHITE", "led", ExecutionBackend::AnalogSolver, NativeLowering::None},
        {"RHEO", "resistor", ExecutionBackend::AnalogSolver, NativeLowering::None},
        {"CERAMIC", "capacitor", ExecutionBackend::AnalogSolver, NativeLowering::None},
        {"FILM", "capacitor", ExecutionBackend::AnalogSolver, NativeLowering::None},
        {"ELECTRO", "polarCap", ExecutionBackend::AnalogSolver, NativeLowering::None},
        {"TANTALUM", "polarCap", ExecutionBackend::AnalogSolver, NativeLowering::None},
        {"RPOWER", "resistor", ExecutionBackend::AnalogSolver, NativeLowering::None},
        {"RNET_B", "rnet", ExecutionBackend::AnalogSolver, NativeLowering::None},
        {"RNET_I", "rnet", ExecutionBackend::AnalogSolver, NativeLowering::None},
        {"ESRC", "capacitor", ExecutionBackend::AnalogSolver, NativeLowering::None},
        {"TVS", "tvs", ExecutionBackend::AnalogSolver, NativeLowering::None},
        {"BRIDGE", "bridge", ExecutionBackend::AnalogSolver, NativeLowering::None},
        {"FUSE", "resistor", ExecutionBackend::AnalogSolver, NativeLowering::None},
        {"JUMPER", "jumper", ExecutionBackend::AnalogSolver, NativeLowering::None},
        {"SPDT", "switch", ExecutionBackend::AnalogSolver, NativeLowering::None},
        {"DPST", "switch", ExecutionBackend::AnalogSolver, NativeLowering::None},
        {"DPDT", "switch", ExecutionBackend::AnalogSolver, NativeLowering::None},
        {"PUSH_NC", "switch", ExecutionBackend::AnalogSolver, NativeLowering::None},
        {"DIP8", "switch", ExecutionBackend::AnalogSolver, NativeLowering::None},
        {"REED", "switch", ExecutionBackend::AnalogSolver, NativeLowering::None},
        {"HEADER", "connector", ExecutionBackend::Topology, NativeLowering::None},
        {"TERMINAL", "connector", ExecutionBackend::Topology, NativeLowering::None},
        {"TP", "testpoint", ExecutionBackend::Topology, NativeLowering::None},
        {"JPLUG", "connector", ExecutionBackend::Topology, NativeLowering::None},
        {"JSOCKET", "connector", ExecutionBackend::Topology, NativeLowering::None},
        {"RAIL", "rail", ExecutionBackend::Topology, NativeLowering::None},
        {"BOARD", "board", ExecutionBackend::Topology, NativeLowering::None},
        {"LDR", "ldr", ExecutionBackend::AnalogSolver, NativeLowering::None},
        {"NTC", "ntc", ExecutionBackend::AnalogSolver, NativeLowering::None},
        {"PTC", "ptc", ExecutionBackend::AnalogSolver, NativeLowering::None},
        {"PHOTODIODE", "photodiode", ExecutionBackend::AnalogSolver, NativeLowering::None},
        {"PHOTOTRANS", "phototransistor", ExecutionBackend::AnalogSolver, NativeLowering::None},
        {"SENSOR", "sensor", ExecutionBackend::AnalogSolver, NativeLowering::None},
        {"BATTERY", "battery", ExecutionBackend::AnalogSolver, NativeLowering::None},
        {"IPULSE", "ipulse", ExecutionBackend::AnalogSolver, NativeLowering::None},
        {"RAMP", "ramp", ExecutionBackend::AnalogSolver, NativeLowering::None},
        {"TRIANGLE", "triangle", ExecutionBackend::AnalogSolver, NativeLowering::None},
        {"SQUARE", "square", ExecutionBackend::AnalogSolver, NativeLowering::None},
        {"PWL", "pwl", ExecutionBackend::AnalogSolver, NativeLowering::None},
        {"NOISE", "noise", ExecutionBackend::AnalogSolver, NativeLowering::None},
        {"NMOS", "mosfet", ExecutionBackend::AnalogSolver, NativeLowering::None},
        {"PMOS", "mosfet", ExecutionBackend::AnalogSolver, NativeLowering::None},
        {"DARL_N", "darlington", ExecutionBackend::AnalogSolver, NativeLowering::None},
        {"DARL_P", "darlington", ExecutionBackend::AnalogSolver, NativeLowering::None},
        {"OPTO", "opto", ExecutionBackend::AnalogSolver, NativeLowering::None},
        {"RELAY", "relay", ExecutionBackend::AnalogSolver, NativeLowering::None},
        {"TRANSFORMER", "transformer", ExecutionBackend::AnalogSolver, NativeLowering::None},
        {"MOTOR", "motor", ExecutionBackend::AnalogSolver, NativeLowering::None},
        {"BUZZER", "buzzer", ExecutionBackend::AnalogSolver, NativeLowering::None},
        {"PIEZO", "acoustic", ExecutionBackend::AnalogSolver, NativeLowering::None},
        {"SPEAKER", "acoustic", ExecutionBackend::AnalogSolver, NativeLowering::None},
        {"SCR", "thyristor", ExecutionBackend::AnalogSolver, NativeLowering::None},
        {"TRIAC", "thyristor", ExecutionBackend::AnalogSolver, NativeLowering::None},
        {"UNO", "avr", ExecutionBackend::AvrRuntime, NativeLowering::None},
        {"LM358", "opamp", ExecutionBackend::AnalogSolver, NativeLowering::None},
        {"LM393", "comparator", ExecutionBackend::AnalogSolver, NativeLowering::None},
        {"COMP_OC", "comparator", ExecutionBackend::AnalogSolver, NativeLowering::None},
        {"REG5", "regulator", ExecutionBackend::AnalogSolver, NativeLowering::None},
        {"LDO", "regulator", ExecutionBackend::AnalogSolver, NativeLowering::None},
        {"VREF", "regulator", ExecutionBackend::AnalogSolver, NativeLowering::None},
        {"LM317", "adjustable", ExecutionBackend::AnalogSolver, NativeLowering::None},
        {"MUX2", "mux", ExecutionBackend::AnalogSolver, NativeLowering::None},
        {"NE556", "dualTimer", ExecutionBackend::AnalogSolver, NativeLowering::None},
        {"CPU8085", "digital", ExecutionBackend::DeviceVm, NativeLowering::Rom8085},
        {"LOGIC", "digital", ExecutionBackend::DeviceVm, NativeLowering::None},
        {"CLOCK", "digital", ExecutionBackend::DeviceVm, NativeLowering::None},
        {"LPROBE", "digital", ExecutionBackend::DeviceVm, NativeLowering::None},
        {"NOT", "digital", ExecutionBackend::DeviceVm, NativeLowering::LogicGates},
        {"BUF", "digital", ExecutionBackend::DeviceVm, NativeLowering::LogicGates},
        {"AND", "digital", ExecutionBackend::DeviceVm, NativeLowering::LogicGates},
        {"OR", "digital", ExecutionBackend::DeviceVm, NativeLowering::LogicGates},
        {"NAND", "digital", ExecutionBackend::DeviceVm, NativeLowering::LogicGates},
        {"NOR", "digital", ExecutionBackend::DeviceVm, NativeLowering::LogicGates},
        {"XOR", "digital", ExecutionBackend::DeviceVm, NativeLowering::LogicGates},
        {"XNOR", "digital", ExecutionBackend::DeviceVm, NativeLowering::LogicGates},
        {"TRIBUF", "digital", ExecutionBackend::DeviceVm, NativeLowering::LogicGates},
        {"SR", "digital", ExecutionBackend::DeviceVm, NativeLowering::None},
        {"DFF", "digital", ExecutionBackend::DeviceVm, NativeLowering::None},
        {"TFF", "digital", ExecutionBackend::DeviceVm, NativeLowering::None},
        {"JKFF", "digital", ExecutionBackend::DeviceVm, NativeLowering::None},
        {"COUNTER", "digital", ExecutionBackend::DeviceVm, NativeLowering::None},
        {"SHIFT", "digital", ExecutionBackend::DeviceVm, NativeLowering::None},
        {"DECODER", "digital", ExecutionBackend::DeviceVm, NativeLowering::None},
        {"ROM", "digital", ExecutionBackend::DeviceVm, NativeLowering::None},
        {"RAM", "digital", ExecutionBackend::DeviceVm, NativeLowering::None},
        {"ADC", "digital", ExecutionBackend::DeviceVm, NativeLowering::None},
        {"DAC", "digital", ExecutionBackend::DeviceVm, NativeLowering::None},
        {"SEG_CC", "segments", ExecutionBackend::AnalogSolver, NativeLowering::None},
        {"SEG_CA", "segments", ExecutionBackend::AnalogSolver, NativeLowering::None},
        {"MATRIX", "matrix", ExecutionBackend::AnalogSolver, NativeLowering::None},
        {"KEYPAD", "keypad", ExecutionBackend::AnalogSolver, NativeLowering::None},
    };
    return policies;
}
const ComponentExecution *executionPolicy(const QString &kind)
{
    static const QHash<QString, qsizetype> indices = []
    {
        QHash<QString, qsizetype> result;
        const auto &policies = executionPolicies();
        for (qsizetype i = 0; i < policies.size(); ++i)
            result.insert(policies[i].kind, i);
        return result;
    }();
    auto it = indices.constFind(kind);
    return it == indices.cend() ? nullptr : &executionPolicies()[it.value()];
}
QString executionBackendName(ExecutionBackend backend)
{
    switch (backend)
    {
    case ExecutionBackend::AnalogSolver:
        return "ngspice";
    case ExecutionBackend::Topology:
        return "compiled topology";
    case ExecutionBackend::DeviceVm:
        return "DeviceVm";
    case ExecutionBackend::AvrRuntime:
        return "AVR8js processor runtime";
    }
    return "rejected";
}
bool validateExecutionSpec(const PartSpec &part, QString &error)
{
    const auto *policy = executionPolicy(part.kind);
    if (!policy || policy->behavior != part.behavior)
    {
        error = "Component has no matching approved execution policy: " + part.kind;
        return false;
    }
    if ((part.behavior == "digital") != (policy->backend == ExecutionBackend::DeviceVm) ||
        (part.behavior == "avr") != (policy->backend == ExecutionBackend::AvrRuntime))
    {
        error = "Component backend conflicts with its execution model: " + part.kind;
        return false;
    }
    return true;
}
bool validateExecutionCatalog(QString &error)
{
    error.clear();
    QSet<QString> kinds;
    for (const auto &policy : executionPolicies())
    {
        if (kinds.contains(policy.kind))
        {
            error = "Duplicate execution policy: " + policy.kind;
            return false;
        }
        kinds.insert(policy.kind);
    }
    if (executionPolicies().size() != catalog().size())
    {
        error = "Catalog and approved execution-policy inventory differ";
        return false;
    }
    QSet<QString> registered;
    for (const auto &part : catalog())
    {
        if (registered.contains(part.kind))
        {
            error = "Duplicate catalog component: " + part.kind;
            return false;
        }
        registered.insert(part.kind);
        if (!validateExecutionSpec(part, error))
            return false;
    }
    if (registered != kinds)
    {
        error = "Catalog and execution-policy coverage differ";
        return false;
    }
    return true;
}
bool validateExecutionCircuit(const Circuit &circuit, QString &error)
{
    if (!validateExecutionCatalog(error))
        return false;
    for (const auto &part : circuit.parts)
    {
        if (!executionPolicy(part.kind))
        {
            error = "Unapproved circuit component: " + part.id;
            return false;
        }
        if (!validateExecutionSpec(spec(part.kind), error))
            return false;
    }
    return true;
}
} // namespace cmp
