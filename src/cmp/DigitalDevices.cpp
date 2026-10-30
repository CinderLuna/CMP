#include "cmp/DigitalDevices.h"
#include "cmp/ExpandedLibrary.h"
#include <cmath>
namespace cmp
{
DigitalLayout digitalLayout(const QString &kind)
{
    DigitalLayout d;
    const auto &s = spec(kind);
    if (s.behavior != "digital")
        return d;
    d.supply = s.pins.size() - 2;
    d.ground = s.pins.size() - 1;
    auto range = [](int first, int count)
    {
        QVector<int> r;
        for (int i = 0; i < count; ++i)
            r << first + i;
        return r;
    };
    if (kind == "CPU8085")
    {
        d.inputs = range(0, 9);
        d.outputs = range(9, 8);
    }
    else if (kind == "LOGIC" || kind == "CLOCK")
        d.outputs = {0};
    else if (kind == "LPROBE")
        d.inputs = range(0, 8);
    else if (kind == "NOT" || kind == "BUF")
    {
        d.inputs = {0};
        d.outputs = {1};
    }
    else if (QStringList{"AND", "OR", "NAND", "NOR", "XOR", "XNOR", "TRIBUF"}.contains(kind))
    {
        d.inputs = {0, 1};
        d.outputs = {2};
    }
    else if (kind == "SR")
    {
        d.inputs = {0, 1};
        d.outputs = {2, 3};
    }
    else if (kind == "DFF" || kind == "TFF")
    {
        d.inputs = {0, 1, 2, 3};
        d.outputs = {4, 5};
    }
    else if (kind == "JKFF")
    {
        d.inputs = {0, 1, 2, 3, 4};
        d.outputs = {5, 6};
    }
    else if (kind == "COUNTER")
    {
        d.inputs = range(0, 7);
        d.outputs = range(7, 4);
    }
    else if (kind == "SHIFT")
    {
        d.inputs = range(0, 5);
        d.outputs = range(5, 8);
    }
    else if (kind == "DECODER")
    {
        d.inputs = range(0, 5);
        d.outputs = range(5, 7);
    }
    else if (kind == "ROM" || kind == "RAM")
    {
        d.inputs = range(0, 19);
        d.outputs = range(8, 8);
    }
    else if (kind == "ADC")
    {
        d.inputs = {0, 1, 2};
        d.outputs = range(3, 8);
    }
    else if (kind == "DAC")
    {
        d.inputs = range(0, 9);
        d.outputs = {9};
    }
    return d;
}
bool digitalDevice(const QString &k)
{
    return spec(k).behavior == "digital";
}
void appendDigitalCatalog(QVector<PartSpec> &out)
{
    auto add = [&](QString k, QString name, QStringList pins,
                   QVector<ParameterSpec> settings = QVector<ParameterSpec>(), QString data = QString())
    {
        pins << "VCC" << "GND";
        PartSpec s{k, name, "U", pins, 0, -2};
        s.category = "Digital logic";
        s.behavior = "digital";
        s.model = "CMP_" + k;
        s.package = "CMP sampled CMOS functional module";
        s.settings = {{"quantum", "Maximum interaction interval", "s", 1e-6, 1e-9, .01}};
        s.settings += settings;
        s.dataKind = data;
        s.limitations =
            "Original MIT sampled CMOS model: 3–5.5 V supply, LOW <=0.3 VCC, HIGH >=0.7 VCC, intermediate "
            "unknown, output resistance 25 ohm. Changes apply after an accepted solver point. No sub-quantum "
            "timing claims. Unknown outputs drive half supply; disabled outputs have 1 TΩ leakage.";
        out << s;
    };
    QStringList processor;
    for (int i = 0; i < 8; ++i)
        processor << "IN1." + QString::number(i);
    processor << "RESET";
    for (int i = 0; i < 8; ++i)
        processor << "OUT0." + QString::number(i);
    add("CPU8085", "8085 trainer system", processor, {}, "firmware");
    out.last().value = 3072000;
    out.last().unit = "Hz";
    out.last().category = "Processors";
    out.last().package = "Functional 8085 trainer module";
    out.last().limitations =
        "Original MIT 8085 functional trainer. 0x0000–7fff ROM, 0x8000–ffff RAM, input port 1 and output "
        "port 0 exposed. Registers, arithmetic, branches, stack, RAM and I/O are interpreted. No physical "
        "multiplexed bus, wait states, external interrupts or serial I/O. Undocumented opcodes fail "
        "explicitly. Instruction timing is modeled, not bus timing.";
    auto f = [](QString key, QString label, double value, double lo = 0, double hi = 1)
    { return ParameterSpec{key, label, "", value, lo, hi}; };
    add("LOGIC", "Logic input", {"Q"}, {f("level", "Logic level", 0)});
    add("CLOCK", "Logic clock", {"Q"},
        {f("frequency", "Frequency (Hz)", 1000, 1e-6, 1e9), f("duty", "Duty fraction", .5, .001, .999),
         f("phase", "Phase (seconds)", 0, 0, 60)});
    QStringList probes;
    for (int i = 0; i < 8; ++i)
        probes << "IN" + QString::number(i);
    add("LPROBE", "Logic analyzer 8-channel", probes);
    for (QString k : {"NOT", "BUF"})
        add(k, k == "BUF" ? "Logic buffer" : "NOT gate", {"A", "Q"});
    for (QString k : {"AND", "OR", "NAND", "NOR", "XOR", "XNOR"})
        add(k, k + " gate", {"A", "B", "Q"});
    add("TRIBUF", "Tri-state buffer", {"A", "ENABLE", "Q"});
    add("SR", "SR latch", {"SET", "RESET", "Q", "Q̅"}, {f("initial", "Initial state (2=unknown)", 2, 0, 2)});
    for (QString k : {"DFF", "TFF"})
        add(k, k == "DFF" ? "D flip-flop" : "T flip-flop",
            {k == "DFF" ? "D" : "T", "CLK", "RESET", "SET", "Q", "Q̅"},
            {f("initial", "Initial state (2=unknown)", 2, 0, 2)});
    add("JKFF", "JK flip-flop", {"J", "K", "CLK", "RESET", "SET", "Q", "Q̅"},
        {f("initial", "Initial state (2=unknown)", 2, 0, 2)});
    add("COUNTER", "Counter 4-bit", {"CLK", "RESET", "LOAD", "D0", "D1", "D2", "D3", "Q0", "Q1", "Q2", "Q3"},
        {f("initial", "Initial count", 0, 0, 15)});
    QStringList shift{"SER", "CLK", "LATCH", "RESET", "ENABLE"};
    for (int i = 0; i < 8; ++i)
        shift << "Q" + QString::number(i);
    add("SHIFT", "Shift register 8-bit", shift, {f("initial", "Initial byte", 0, 0, 255)});
    QStringList decoder{"A", "B", "C", "D", "BLANK"};
    for (auto x : {"a", "b", "c", "d", "e", "f", "g"})
        decoder << x;
    add("DECODER", "BCD to seven-segment", decoder);
    for (QString k : {"ROM", "RAM"})
    {
        QStringList pins;
        for (int i = 0; i < 8; ++i)
            pins << "A" + QString::number(i);
        for (int i = 0; i < 8; ++i)
            pins << "D" + QString::number(i);
        pins << "CS" << "OE" << "WE";
        add(k, k + " 256 × 8", pins, {}, "memory");
    }
    QStringList adc{"VIN", "VREF", "CLK"};
    for (int i = 0; i < 8; ++i)
        adc << "D" + QString::number(i);
    add("ADC", "ADC 8-bit parallel", adc);
    QStringList dac;
    for (int i = 0; i < 8; ++i)
        dac << "D" + QString::number(i);
    dac << "VREF" << "OUT";
    add("DAC", "DAC 8-bit parallel", dac);
}
QStringList digitalNetlist(const Part &p, const QStringList &n)
{
    auto d = digitalLayout(p.kind);
    QStringList l;
    QString id = p.id.toLower();
    auto supply = "V(" + n[d.supply] + "," + n[d.ground] + ")";
    for (int pin : d.inputs)
        if (!d.outputs.contains(pin))
            l << "R__dig_in_" + id + "_" + QString::number(pin) + " " + n[pin] + " " + n[d.ground] + " 1e12";
    for (int pin : d.outputs)
    {
        auto suffix = id + "_" + QString::number(pin);
        auto control = "__dig_ctl_" + suffix;
        auto voltage = "V(" + n[pin] + "," + n[d.ground] + ")";
        l << "V__dig_" + suffix + " " + control + " 0 external";
        QString target = p.kind == "DAC" ? "V(" + control + ")*V(" + n[8] + "," + n[d.ground] + ")"
                                         : "(V(" + control + ")<.5 ? 0 : (V(" + control + ")<1.5 ? " +
                                               supply + " : .5*" + supply + "))";
        auto current = (p.kind == "DAC" ? QString()
                                        : "(V(" + control + ")>1.5 && V(" + control + ")<2.5) ? " + voltage +
                                              "/1e12 : ") +
                       "(" + voltage + "-(" + target + "))/25";
        l << "B__dig_" + suffix + " " + n[pin] + " " + n[d.ground] + " I=" + current;
        l << "B__dig_power_" + suffix + " " + n[d.supply] + " " + n[d.ground] + " I=max(0,-(" + current +
                 "))";
    }
    return l;
}
} // namespace cmp
