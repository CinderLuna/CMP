#include "cmp/ExpandedLibrary.h"
namespace cmp
{
static QString num(double v)
{
    return QString::number(v, 'g', 14);
}
static ParameterSpec field(QString key, QString label, QString unit, double value, double lo = 0,
                           double hi = 1e9)
{
    return {key, label, unit, value, lo, hi};
}
void appendAnalogCatalog(QVector<PartSpec> &out)
{
    auto add = [&](QString kind, QString name, QStringList pins, QString behavior, double value = 0,
                   QString unit = QString(),
                   QVector<ParameterSpec> settings = QVector<ParameterSpec>()) -> PartSpec &
    {
        PartSpec s{kind, name, "U", pins, value, -2};
        s.category = "Analog ICs";
        s.behavior = behavior;
        s.unit = unit;
        s.settings = settings;
        s.package = pins.size() > 3 ? "PDIP functional package" : "Functional three-pin package";
        s.model = "CMP_" + kind;
        s.limitations = "Original MIT functional model. Parameters are model assumptions, not guaranteed "
                        "manufacturer specifications. No thermal, noise or stability certification.";
        out << s;
        return out.last();
    };
    QStringList unoPins;
    for (int i = 0; i < 14; ++i)
        unoPins << "D" + QString::number(i);
    for (int i = 0; i < 6; ++i)
        unoPins << "A" + QString::number(i);
    unoPins << "5V" << "GND" << "AREF" << "RESET";
    auto &uno = add("UNO", "Arduino Uno / ATmega328P", unoPins, "avr", 16000000, "Hz",
                    {field("quantum", "Circuit interaction quantum", "s", 1e-6, 6.25e-8, .001)});
    uno.category = "Processors";
    uno.dataKind = "firmware";
    uno.package = "Uno GPIO module; external regulated 5 V supply";
    uno.limitations = "AVR8js 0.21.1, MIT. 16 MHz ATmega328P, GPIO, timers 0/1/2, ADC; USART register model only (no serial pin waveform or console). Circuit "
                      "pins sampled at accepted solver points; outputs held until next point. No USB, "
                      "bootloader, fuse, RF or thermal model; SPI/TWI/EEPROM/watchdog not attached.";
    auto &op =
        add("LM358", "LM358 dual op-amp",
            {"1 OUT1", "2 IN1−", "3 IN1+", "4 V−", "5 IN2+", "6 IN2−", "7 OUT2", "8 V+"}, "opamp", 100000, "",
            {field("pole", "Dominant pole", "Hz", 10, 1e-6),
             field("headroom", "Positive output headroom", "V", 1.5),
             field("rout", "Output resistance", "Ω", 50, 1e-6),
             field("current", "Output current limit", "A", .02, 1e-9)});
    op.package = "Classic LM358 PDIP-8 functional model";
    auto &dual =
        add("LM393", "LM393 dual comparator",
            {"1 OUT1", "2 IN1−", "3 IN1+", "4 GND", "5 IN2+", "6 IN2−", "7 OUT2", "8 VCC"}, "comparator");
    dual.comparators = {{0, 1, 2}, {6, 5, 4}};
    add("COMP_OC", "Comparator open collector", {"OUT", "IN−", "IN+", "VCC", "GND"}, "comparator")
        .comparators = {{0, 1, 2}};
    for (QString kind : {"REG5", "LDO", "VREF"})
    {
        bool reference = kind == "VREF", ldo = kind == "LDO";
        add(kind,
            reference ? "Reference 2.5 V"
            : ldo     ? "Generic LDO 3.3 V"
                      : "Fixed regulator 5 V",
            {"IN", "GND", "OUT"}, "regulator",
            reference ? 2.5
            : ldo     ? 3.3
                      : 5,
            "V",
            {field("dropout", "Dropout assumption", "V",
                   reference ? .5
                   : ldo     ? .3
                             : 2),
             field("current", "Source current limit", "A", reference ? .01 : 1, 1e-9),
             field("rout", "Output resistance", "Ω", reference ? 1 : .05, 1e-6),
             field("quiescent", "Quiescent current", "A", reference ? .0001 : .005)});
    }
    add("LM317", "LM317 adjustable regulator", {"1 ADJ", "2 OUT", "3 IN"}, "adjustable", 1.25, "V",
        {field("dropout", "Dropout assumption", "V", 3),
         field("current", "Source current limit", "A", 1.5, 1e-9),
         field("rout", "Output resistance", "Ω", .05, 1e-6),
         field("adjustCurrent", "Adjust pin current", "A", 50e-6)})
        .package = "LM317 legacy TO-220 ADJ/OUT/IN functional model";
    add("MUX2", "Analog multiplexer 2:1", {"A", "B", "COM", "SEL", "EN", "VCC", "GND"}, "mux", 50, "Ω",
        {field("roff", "Off resistance", "Ω", 1e12, 1, 1e15),
         field("threshold", "Control threshold / supply", "", .5, 0, 1)});
    add("NE556", "NE556 dual timer",
        {"1 DIS1", "2 THR1", "3 CTRL1", "4 RESET1", "5 OUT1", "6 TRIG1", "7 GND", "8 TRIG2", "9 OUT2",
         "10 RESET2", "11 CTRL2", "12 THR2", "13 DIS2", "14 VCC"},
        "dualTimer")
        .package = "NE556 PDIP-14";
}
QStringList analogDeviceNetlist(const Part &p, const QStringList &n)
{
    QStringList l;
    auto b = spec(p.kind).behavior;
    auto v = [&](int a, int c) { return "V(" + n[a] + "," + n[c] + ")"; };
    if (b == "comparator")
        for (auto &ch : spec(p.kind).comparators)
        {
            int supply = p.kind == "LM393" ? 7 : 3, ground = p.kind == "LM393" ? 3 : 4;
            l << "Xch" + QString::number(ch.output) + " " + n[ch.output] + " " + n[ch.negative] + " " +
                     n[ch.positive] + " " + n[supply] + " " + n[ground] + " CMP_LM339_CH";
        }
    else if (b == "dualTimer")
        l << "Xfirst " + n[6] + " " + n[5] + " " + n[4] + " " + n[3] + " " + n[2] + " " + n[1] + " " + n[0] +
                 " " + n[13] + " CMP_555"
          << "Xsecond " + n[6] + " " + n[7] + " " + n[8] + " " + n[9] + " " + n[10] + " " + n[11] + " " +
                 n[12] + " " + n[13] + " CMP_555";
    else if (b == "opamp")
        for (int ch = 0; ch < 2; ++ch)
        {
            int out = ch ? 6 : 0, minus = ch ? 5 : 1, plus = ch ? 4 : 2;
            QString x = QString::number(ch);
            auto hi = "max(0," + v(7, 3) + "-" + num(parameter(p, "headroom")) + ")";
            l << "Bgain" + x + " drive" + x + " " + n[3] + " V=max(0,min(" + hi + "," + num(p.value) + "*" +
                     v(plus, minus) + "))";
            l << "Rdominant" + x + " drive" + x + " pole" + x + " 1000"
              << "Cdominant" + x + " pole" + x + " " + n[3] + " " +
                     num(1 / (6283.185307179586 * parameter(p, "pole")));
            l << "Boutput" + x + " " + n[3] + " " + n[out] + " I=max(-" + num(parameter(p, "current")) +
                     ",min(" + num(parameter(p, "current")) + ",(V(pole" + x + "," + n[3] + ")-" + v(out, 3) +
                     ")/" + num(parameter(p, "rout")) + "))";
            l << "RinN" + x + " " + n[minus] + " " + n[3] + " 1e12"
              << "RinP" + x + " " + n[plus] + " " + n[3] + " 1e12";
        }
    else if (b == "regulator" || b == "adjustable")
    {
        bool adj = b == "adjustable";
        int input = adj ? 2 : 0, ground = adj ? 0 : 1, output = adj ? 1 : 2;
        auto target =
            "max(0,min(" + num(p.value) + "," + v(input, ground) + "-" + num(parameter(p, "dropout")) + "))";
        l << "Bdrive drive " + n[ground] + " V=" + target + "-1e6*max(0,I(Vsense)-" +
                 num(parameter(p, "current")) + ")+1e6*max(0,-I(Vsense))";
        l << "Vsense drive series 0" << "Rout series " + n[output] + " " + num(parameter(p, "rout"));
        l << "Binput " + n[input] + " " + n[ground] + " I=max(0,I(Vsense))+" +
                 num(parameter(p, adj ? "adjustCurrent" : "quiescent"));
    }
    else if (b == "mux")
    {
        l << ".model muxsw SW(Ron=" + num(p.value) + " Roff=" + num(parameter(p, "roff")) + " Vt=.5 Vh=0)";
        for (int ch = 0; ch < 2; ++ch)
        {
            QString x = QString::number(ch);
            l << "Bcontrol" + x + " ctl" + x + " " + n[6] + " V=(" + v(5, 6) + ">=2 && " + v(4, 6) + ">" +
                     num(parameter(p, "threshold")) + "*" + v(5, 6) + " && " + v(3, 6) + (ch ? ">" : "<=") +
                     num(parameter(p, "threshold")) + "*" + v(5, 6) + ") ? 1 : 0";
            l << "Spath" + x + " " + n[ch] + " " + n[2] + " ctl" + x + " " + n[6] + " muxsw";
        }
    }
    return l;
}
} // namespace cmp
