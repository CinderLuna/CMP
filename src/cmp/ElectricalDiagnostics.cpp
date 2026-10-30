#include "cmp/ElectricalDiagnostics.h"
#include "cmp/DigitalDevices.h"
#include "cmp/ExpandedLibrary.h"
#include <cmath>
#include <limits>
namespace cmp
{
QVector<RatingDefinition> ratingDefinitions(const QString &k)
{
    if (expanded(k))
    {
        const auto b = spec(k).behavior;
        if (b == "digital")
            return {{"supply", "Supply model envelope", "V", 5.5},
                    {"current", "Per-output current envelope", "A", .02}};
        if (b == "segments" || b == "matrix")
            return {{"current", "Per-LED current envelope", "A", .02},
                    {"reverse", "Reverse LED voltage envelope", "V", 5}};
        if (b == "keypad")
            return {{"current", "Row/column contact current", "A"}, {"voltage", "Open contact voltage", "V"}};
        if (b == "avr")
            return {{"supply", "Supply model envelope", "V", 5.5},
                    {"current", "Per-pin current model envelope", "A", .02}};
        if (b == "opamp" || b == "comparator" || b == "dualTimer")
            return {{"supply", "Supply absolute limit (user selected)", "V"},
                    {"current", "Per-output current limit", "A"}};
        if (b == "regulator" || b == "adjustable")
            return {{"voltage", "Input differential voltage", "V"}, {"power", "Package dissipation", "W"}};
        if (b == "mux")
            return {{"supply", "Supply voltage", "V"}, {"current", "Channel current", "A"}};
        if (b == "mosfet")
            return {{"vds", "Drain-source voltage", "V"},
                    {"vgs", "Gate-source voltage", "V"},
                    {"current", "Drain current", "A"},
                    {"power", "Dissipation", "W"}};
        if (b == "darlington")
            return {{"vce", "Collector-emitter voltage", "V"},
                    {"current", "Collector current", "A"},
                    {"power", "Dissipation", "W"}};
        if (b == "opto")
            return {{"ledCurrent", "LED forward current", "A"},
                    {"reverse", "LED reverse voltage", "V"},
                    {"current", "Collector current", "A"},
                    {"voltage", "Collector-emitter voltage", "V"}};
        if (b == "relay")
            return {{"coilPower", "Coil copper loss", "W"},
                    {"current", "Contact current", "A"},
                    {"voltage", "Open contact voltage", "V"}};
        if (b == "transformer")
            return {{"primaryCurrent", "Primary current", "A"},
                    {"secondaryCurrent", "Secondary current", "A"},
                    {"power", "Total winding copper loss", "W"}};
        if (b == "motor")
            return {{"voltage", "Terminal voltage", "V"},
                    {"current", "Winding current", "A"},
                    {"power", "Winding copper loss", "W"}};
        if (b == "buzzer" || b == "acoustic")
            return {{"voltage", "Terminal voltage", "V"}, {"current", "Terminal current", "A"}};
        if (b == "thyristor")
            return {{"voltage", "Main terminal voltage", "V"},
                    {"current", "Main current", "A"},
                    {"gateCurrent", "Gate current", "A"},
                    {"power", "Dissipation", "W"}};
        if (b == "ldr" || b == "ntc" || b == "ptc")
            return {{"power", "Dissipation", "W"}, {"voltage", "Maximum voltage", "V"}};
        if (b == "photodiode")
            return {{"current", "Forward current (model envelope)", "A", .01},
                    {"reverse", "Reverse voltage (model envelope)", "V", 30}};
        if (b == "phototransistor")
            return {{"current", "Collector current (model envelope)", "A", .02},
                    {"voltage", "Collector voltage (model envelope)", "V", 30},
                    {"power", "Dissipation", "W"}};
        if (b == "sensor")
            return {{"supply", "Supply voltage (model envelope)", "V", 5.5},
                    {"current", "Output current (model envelope)", "A", .01},
                    {"power", "Dissipation", "W"}};
        if (b == "battery")
            return {{"discharge", "Discharge current", "A"},
                    {"chargeCurrent", "Charge current", "A"},
                    {"voltage", "Maximum terminal voltage", "V"},
                    {"power", "Internal loss", "W"}};
        if (k == "FUSE")
            return {{"power", "Power", "W"}, {"current", "Diagnostic current limit", "A", 1}};
        if (b == "resistor" || b == "pot" || b == "rnet")
            return {{"power", b == "rnet" ? "Per-element power" : "Track / element power", "W"}};
        if (b == "capacitor" || b == "polarCap")
            return {{"voltage", "Voltage", "V"}};
        if (b == "inductor")
            return {{"current", "Winding current", "A"}, {"power", "Copper loss", "W"}};
        if (b == "zener")
            return {{"power", "Dissipation", "W"}};
        if (b == "tvs")
            return {{"peakCurrent", "Peak current", "A"}, {"energy", "Captured pulse energy", "J"}};
        if (b == "led")
            return {{"current", "Forward current (generic preset)", "A", .02},
                    {"reverse", "Reverse voltage (generic preset)", "V", 5}};
        if (k == "RECT" || b == "bridge")
            return {{"averageCurrent", "Capture-average forward current", "A", 1},
                    {"reverse", "Reverse voltage", "V", 1000}};
        if (k == "SCHOTTKY")
            return {{"current", "Forward current (model envelope)", "A", 1},
                    {"reverse", "Reverse voltage (model envelope)", "V", 40}};
        if (b == "pnp")
            return {{"vce", "Emitter-collector voltage", "V", 40},
                    {"vcb", "Base-collector voltage", "V", 40},
                    {"veb", "Base-emitter reverse voltage", "V", 5},
                    {"current", "Collector current", "A", .2},
                    {"power", "Dissipation at nominal ambient", "W", .625}};
        if (b == "switch" || b == "jumper")
            return {{"current", "Contact current", "A"}, {"voltage", "Open contact voltage", "V"}};
    }
    if (k == "R")
        return {{"power", "Power", "W"}};
    if (k == "C" || k == "CP")
        return {{"voltage", "Voltage", "V"}};
    if (k == "LED")
        return {{"current", "Forward current", "A"}, {"reverse", "Reverse voltage", "V"}};
    if (k == "D")
        return {{"current", "Forward current", "A", .3}, {"reverse", "Reverse voltage", "V", 75}};
    if (k == "NPN")
        return {{"vce", "Collector-emitter voltage", "V", 40},
                {"vcb", "Collector-base voltage", "V", 60},
                {"veb", "Emitter-base reverse voltage", "V", 6},
                {"current", "Collector current", "A", .2},
                {"power", "Dissipation", "W", .625}};
    if (k == "LM339")
        return {{"supply", "Supply absolute maximum", "V", 36},
                {"current", "Output absolute current", "A", .02},
                {"outputVoltage", "Output voltage maximum", "V", 36}};
    if (k == "555")
        return {{"supply", "Supply maximum", "V", 18}, {"current", "Output current", "A", .2}};
    return {};
}
QStringList ElectricalReport::lines(bool complete) const
{
    QStringList out;
    out << (complete ? "Solver: completed." : "Solver: incomplete; checks cover available samples only.");
    out << QString("Electrical checks: %1 violation(s), %2 incomplete check(s).")
               .arg(issues.size())
               .arg(incomplete.size());
    for (const auto &i : issues)
        out << QString("%1 | %2 [%3] | first t=%4 s | %8 %5 %6 | limit %7 %6")
                   .arg(i.component, i.rule, i.operatingRange ? "operating range" : "rating violation")
                   .arg(i.firstTime, 0, 'g', 7)
                   .arg(i.peak, 0, 'g', 7)
                   .arg(i.unit)
                   .arg(i.limit, 0, 'g', 7)
                   .arg(i.lowerBound ? "minimum" : "peak");
    for (const auto &s : incomplete)
        out << "Unchecked: " + s;
    if (complete && issues.isEmpty() && incomplete.isEmpty())
        out << "No violations observed in the sampled interval for the implemented checks.";
    out << "Functional models only: no temperature, aging, destruction or permanent failure. Sampled checks "
           "do not certify hardware safety or capture every transient.";
    return out;
}
ElectricalReport diagnoseElectrical(const Circuit &c, const Samples &samples)
{
    ElectricalReport report;
    const auto nodes = c.nodes();
    const double nan = std::numeric_limits<double>::quiet_NaN();
    if (samples.isEmpty())
    {
        report.incomplete << "No simulation samples available.";
        return report;
    }
    for (const auto &p : c.parts)
    {
        auto voltage = [&](const Sample &s, int pin)
        {
            const auto node = nodes.value(Pin{p.id, pin}.key());
            return node == "0" ? 0.0 : s.values.value(node, nan);
        };
        auto current = [&](const Sample &s, const QString &prefix)
        { return s.values.value(prefix + p.id.toLower() + "#branch", nan); };
        auto limit = [&](const QString &key)
        {
            for (const auto &d : ratingDefinitions(p.kind))
                if (d.key == key)
                    return p.ratings.value(key, d.preset);
            return 0.0;
        };
        auto check = [&](QString rule, QString unit, double bound, auto measure, bool operating = false,
                         bool lower = false)
        {
            if (!std::isfinite(bound) || bound <= 0)
            {
                report.incomplete << p.id + " | " + rule +
                                         ": rating unspecified (Properties > Electrical ratings).";
                return;
            }
            ElectricalIssue issue{p.id, rule, unit, 0, 0, bound, operating, lower};
            bool found = false, missing = false;
            for (const auto &s : samples)
            {
                const double value = measure(s);
                if (!std::isfinite(value) || !std::isfinite(s.time))
                {
                    missing = true;
                    continue;
                }
                if (lower ? value < bound * (1 - 1e-6) : value > bound * (1 + 1e-6))
                {
                    if (!found)
                    {
                        issue.firstTime = s.time;
                        issue.peak = value;
                        found = true;
                    }
                    issue.peak = lower ? qMin(issue.peak, value) : qMax(issue.peak, value);
                }
            }
            if (found)
                report.issues << issue;
            if (missing)
                report.incomplete << p.id + " | " + rule + ": missing/nonfinite measurements.";
        };
        auto delta = [&](const Sample &s) { return voltage(s, 0) - voltage(s, 1); };
        if (expanded(p.kind))
        {
            const auto behavior = spec(p.kind).behavior;
            auto ip = [&](const Sample &s, int pin = 0) { return s.values.value(currentKey(p, pin), nan); };
            auto power = [&](const Sample &s)
            {
                double result = 0;
                for (int pin = 0; pin < spec(p.kind).pins.size(); ++pin)
                    result += voltage(s, pin) * ip(s, pin);
                return std::isfinite(result) ? qMax(0., result) : nan;
            };
            auto integral = [&](auto measure, bool average)
            {
                double sum = 0;
                double previous = measure(samples.front());
                if (!std::isfinite(previous))
                    return nan;
                for (int i = 1; i < samples.size(); ++i)
                {
                    double next = measure(samples[i]);
                    if (!std::isfinite(next))
                        return nan;
                    sum += (previous + next) * .5 * (samples[i].time - samples[i - 1].time);
                    previous = next;
                }
                const double duration = samples.back().time - samples.front().time;
                return average ? (duration > 0 ? sum / duration : previous) : sum;
            };
            auto aggregate = [&](QString rule, QString unit, double bound, double measured)
            {
                if (bound <= 0 || !std::isfinite(bound))
                    report.incomplete << p.id + " | " + rule + ": rating unspecified.";
                else if (!std::isfinite(measured))
                    report.incomplete << p.id + " | " + rule + ": missing/nonfinite measurement.";
                else if (measured > bound * (1 + 1e-6))
                    report.issues << ElectricalIssue{p.id, rule, unit, samples.back().time, measured, bound};
            };

            if (behavior == "digital")
            {
                auto layout = digitalLayout(p.kind);
                check("Digital supply", "V", limit("supply"),
                      [&](const Sample &s) { return voltage(s, layout.supply) - voltage(s, layout.ground); });
                check(
                    "Digital supply below model range", "V", p.kind == "CPU8085" ? 4.5 : 3,
                    [&](const Sample &s) { return voltage(s, layout.supply) - voltage(s, layout.ground); },
                    true, true);
                for (int pin : layout.inputs)
                {
                    if ((p.kind == "ADC" && pin < 2) || (p.kind == "DAC" && pin == 8))
                        continue;
                    check(
                        "Undefined sampled logic input " + QString::number(pin), "state", .5,
                        [&, pin](const Sample &s)
                        {
                            auto key = "logic_" + p.id.toLower() + "_" + QString::number(pin);
                            return s.values.contains(key) ? (s.values[key] == 3 ? 1. : 0.) : nan;
                        },
                        true);
                }
                for (int pin : layout.outputs)
                    check("Output current / possible bus contention " + QString::number(pin), "A",
                          limit("current"), [&, pin](const Sample &s) { return std::abs(ip(s, pin)); });
            }
            if (behavior == "segments")
                for (int pin = 0; pin < 8; ++pin)
                {
                    check("Segment current " + QString::number(pin), "A", limit("current"),
                          [&, pin](const Sample &s) { return std::abs(ip(s, pin)); });
                    check("Segment reverse voltage " + QString::number(pin), "V", limit("reverse"),
                          [&, pin](const Sample &s)
                          {
                              return p.kind == "SEG_CC" ? voltage(s, 8) - voltage(s, pin)
                                                        : voltage(s, pin) - voltage(s, 8);
                          });
                }
            if (behavior == "matrix")
                for (int r = 0; r < 4; ++r)
                    for (int col = 0; col < 4; ++col)
                    {
                        auto cell = "cell" + QString::number(r) + QString::number(col);
                        check("LED current " + cell, "A", limit("current"), [&, cell](const Sample &s)
                              { return std::abs(s.values.value(internalKey(p, cell), nan)); });
                        check("LED reverse voltage " + cell, "V", limit("reverse"),
                              [&, r, col](const Sample &s) { return voltage(s, 4 + col) - voltage(s, r); });
                    }
            if (behavior == "avr")
            {
                auto supply = [&](const Sample &s) { return voltage(s, 20) - voltage(s, 21); };
                check("Supply voltage", "V", limit("supply"), supply);
                check("Supply below supported 16 MHz model range", "V", 4.5, supply, true, true);
                for (int pin = 0; pin < 20; ++pin)
                {
                    check("GPIO current " + QString::number(pin), "A", limit("current"),
                          [&, pin](const Sample &s) { return std::abs(ip(s, pin)); });
                    check(
                        "GPIO above supply " + QString::number(pin), "V", .5,
                        [&, pin](const Sample &s) { return voltage(s, pin) - voltage(s, 20); }, true);
                    check(
                        "GPIO below ground " + QString::number(pin), "V", .5,
                        [&, pin](const Sample &s) { return voltage(s, 21) - voltage(s, pin); }, true);
                }
            }
            if (behavior == "opamp" || behavior == "comparator" || behavior == "dualTimer")
            {
                int ground = behavior == "dualTimer" ? 6 : p.kind == "COMP_OC" ? 4 : 3;
                int supply = behavior == "dualTimer" ? 13 : p.kind == "COMP_OC" ? 3 : 7;
                auto rail = [&](const Sample &s) { return voltage(s, supply) - voltage(s, ground); };
                check("Supply voltage", "V", limit("supply"), rail);
                check("Supply below model range", "V",
                      behavior == "dualTimer" ? 4.5
                      : behavior == "opamp"   ? 3
                                              : 2,
                      rail, true, true);
                QVector<int> inputs = behavior == "dualTimer" ? QVector<int>{1, 5, 7, 11}
                                      : p.kind == "COMP_OC"   ? QVector<int>{1, 2}
                                                              : QVector<int>{1, 2, 4, 5};
                for (int pin : inputs)
                {
                    check(
                        "Input below ground " + QString::number(pin + 1), "V", .001,
                        [&, pin](const Sample &s) { return voltage(s, ground) - voltage(s, pin); }, true);
                    check(
                        "Input above common-mode ceiling " + QString::number(pin + 1), "V", .001,
                        [&, pin](const Sample &s)
                        {
                            return voltage(s, pin) - voltage(s, supply) + (behavior == "dualTimer" ? 0 : 1.5);
                        },
                        true);
                }
                QVector<int> outputs = behavior == "dualTimer" ? QVector<int>{4, 8}
                                       : p.kind == "COMP_OC"   ? QVector<int>{0}
                                                               : QVector<int>{0, 6};
                for (int pin : outputs)
                    check("Output current " + QString::number(pin + 1), "A", limit("current"),
                          [&, pin](const Sample &s) { return std::abs(ip(s, pin)); });
            }
            if (behavior == "regulator" || behavior == "adjustable")
            {
                if (p.kind == "LDO")
                    report.incomplete << p.id + ": LDO output-capacitor stability is not modeled.";
                int input = behavior == "adjustable" ? 2 : 0, ground = behavior == "adjustable" ? 0 : 1,
                    output = behavior == "adjustable" ? 1 : 2;
                check("Input differential voltage", "V", limit("voltage"),
                      [&](const Sample &s) { return std::abs(voltage(s, input) - voltage(s, ground)); });
                check("Package dissipation", "W", limit("power"), power);
                check(
                    "Insufficient regulating headroom", "V", .001, [&](const Sample &s)
                    { return p.value + parameter(p, "dropout") - (voltage(s, input) - voltage(s, ground)); },
                    true);
                check(
                    "Current limit reached", "A", parameter(p, "current") * .999,
                    [&](const Sample &s) { return qMax(0., -ip(s, output)); }, true);
            }
            if (behavior == "mux")
            {
                check("Supply voltage", "V", limit("supply"),
                      [&](const Sample &s) { return voltage(s, 5) - voltage(s, 6); });
                check("Channel current", "A", limit("current"),
                      [&](const Sample &s) { return std::abs(ip(s, 2)); });
                for (int pin = 0; pin < 5; ++pin)
                {
                    check(
                        "MUX input above supply " + QString::number(pin), "V", .001,
                        [&, pin](const Sample &s) { return voltage(s, pin) - voltage(s, 5); }, true);
                    check(
                        "MUX input below ground " + QString::number(pin), "V", .001,
                        [&, pin](const Sample &s) { return voltage(s, 6) - voltage(s, pin); }, true);
                }
            }
            if (behavior == "mosfet" || behavior == "darlington")
            {
                check("Terminal voltage", "V", limit(behavior == "mosfet" ? "vds" : "vce"),
                      [&](const Sample &s) { return std::abs(voltage(s, 0) - voltage(s, 2)); });
                if (behavior == "mosfet")
                    check("Gate-source voltage", "V", limit("vgs"),
                          [&](const Sample &s) { return std::abs(voltage(s, 1) - voltage(s, 2)); });
                check("Main current", "A", limit("current"),
                      [&](const Sample &s) { return std::abs(ip(s)); });
                check("Dissipation", "W", limit("power"), power);
            }
            if (behavior == "opto")
            {
                check("LED current", "A", limit("ledCurrent"),
                      [&](const Sample &s) { return qMax(0., ip(s)); });
                check("LED reverse voltage", "V", limit("reverse"),
                      [&](const Sample &s) { return voltage(s, 1) - voltage(s, 0); });
                check("Collector current", "A", limit("current"),
                      [&](const Sample &s) { return std::abs(ip(s, 2)); });
                check("Collector voltage", "V", limit("voltage"),
                      [&](const Sample &s) { return std::abs(voltage(s, 2) - voltage(s, 3)); });
            }
            if (behavior == "relay")
            {
                check("Coil copper loss", "W", limit("coilPower"),
                      [&](const Sample &s) { return ip(s) * ip(s) * p.value; });
                for (int pin : {3, 4})
                {
                    check("Contact current " + QString::number(pin), "A", limit("current"),
                          [&, pin](const Sample &s) { return std::abs(ip(s, pin)); });
                    check("Contact voltage " + QString::number(pin), "V", limit("voltage"),
                          [&, pin](const Sample &s) { return std::abs(voltage(s, 2) - voltage(s, pin)); });
                }
            }
            if (behavior == "transformer")
            {
                check("Primary current", "A", limit("primaryCurrent"),
                      [&](const Sample &s) { return std::abs(ip(s)); });
                check("Secondary current", "A", limit("secondaryCurrent"),
                      [&](const Sample &s) { return std::abs(ip(s, 2)); });
                check("Winding copper loss", "W", limit("power"),
                      [&](const Sample &s)
                      {
                          return ip(s) * ip(s) * parameter(p, "primaryResistance") +
                                 ip(s, 2) * ip(s, 2) * parameter(p, "secondaryResistance");
                      });
            }
            if (behavior == "motor" || behavior == "buzzer" || behavior == "acoustic" ||
                behavior == "thyristor")
            {
                int terminal = behavior == "thyristor" ? 2 : 1;
                check("Terminal voltage", "V", limit("voltage"), [&, terminal](const Sample &s)
                      { return std::abs(voltage(s, 0) - voltage(s, terminal)); });
                check("Terminal current", "A", limit("current"),
                      [&](const Sample &s) { return std::abs(ip(s)); });
                if (behavior == "motor")
                    check("Winding copper loss", "W", limit("power"),
                          [&](const Sample &s) { return ip(s) * ip(s) * p.value; });
                if (behavior == "thyristor")
                {
                    check("Gate current", "A", limit("gateCurrent"),
                          [&](const Sample &s) { return std::abs(ip(s, 1)); });
                    check("Dissipation", "W", limit("power"), power);
                }
            }
            if (behavior == "ldr" || behavior == "ntc" || behavior == "ptc")
            {
                check("Sensor dissipation", "W", limit("power"), power);
                check("Sensor voltage", "V", limit("voltage"),
                      [&](const Sample &s) { return std::abs(delta(s)); });
            }
            if (behavior == "photodiode")
            {
                check("Photodiode forward current", "A", limit("current"),
                      [&](const Sample &s) { return ip(s); });
                check("Photodiode reverse voltage", "V", limit("reverse"),
                      [&](const Sample &s) { return -delta(s); });
            }
            if (behavior == "phototransistor")
            {
                check("Phototransistor current", "A", limit("current"),
                      [&](const Sample &s) { return std::abs(ip(s)); });
                check("Collector voltage", "V", limit("voltage"), [&](const Sample &s) { return delta(s); });
                check("Phototransistor dissipation", "W", limit("power"), power);
            }
            if (behavior == "sensor")
            {
                check("Sensor supply maximum", "V", limit("supply"), delta);
                check("Sensor supply below operating range", "V", 2.5, delta, true, true);
                check("Sensor output current", "A", limit("current"),
                      [&](const Sample &s) { return std::abs(ip(s, 2)); });
                check("Sensor dissipation", "W", limit("power"), power);
                double low = parameter(p, "inputLow"), high = parameter(p, "inputHigh");
                if (!p.data.isEmpty())
                {
                    QVector<QPointF> curve;
                    QString error;
                    if (parseCurve(p.data, curve, error, false))
                    {
                        low = curve.front().x();
                        high = curve.back().x();
                    }
                }
                if (parameter(p, "stimulus") < low || parameter(p, "stimulus") > high)
                    report.incomplete
                        << p.id + ": stimulus lies outside the declared transfer curve; output is clamped.";
            }
            if (behavior == "battery")
            {
                check("Battery discharge current", "A", limit("discharge"),
                      [&](const Sample &s)
                      {
                          const auto v = ip(s);
                          return std::isfinite(v) ? qMax(0., -v) : nan;
                      });
                check("Battery charge current", "A", limit("chargeCurrent"),
                      [&](const Sample &s)
                      {
                          const auto v = ip(s);
                          return std::isfinite(v) ? qMax(0., v) : nan;
                      });
                check("Battery terminal voltage", "V", limit("voltage"),
                      [&](const Sample &s) { return std::abs(delta(s)); });
                check("Battery internal loss", "W", limit("power"),
                      [&](const Sample &s) { return ip(s) * ip(s) * parameter(p, "resistance"); });
                bool charging = false;
                for (const auto &s : samples)
                    charging |= ip(s) > 1e-9;
                if (charging)
                    report.incomplete << p.id + ": battery charge chemistry/protection is not modeled.";
            }
            if (behavior == "resistor")
                check("Element dissipation", "W", limit("power"), power);
            if (behavior == "pot")
                for (int pin : {0, 2})
                    check(pin == 0 ? "Track segment A dissipation" : "Track segment B dissipation", "W",
                          limit("power") *
                              qMax(1e-12, pin == 0 ? 1 - parameter(p, "wiper") : parameter(p, "wiper")),
                          [&](const Sample &s)
                          {
                              return ip(s, pin) * ip(s, pin) *
                                     qMax(1e-6, p.value * (pin == 0 ? parameter(p, "wiper")
                                                                    : 1 - parameter(p, "wiper")));
                          });
            if (behavior == "rnet")
                for (int i = 0; i < 4; ++i)
                {
                    int a = p.kind == "RNET_B" ? 0 : i * 2, b = p.kind == "RNET_B" ? i + 1 : i * 2 + 1;
                    check("Array element " + QString::number(i + 1) + " power", "W", limit("power"),
                          [&](const Sample &s)
                          { return std::abs((voltage(s, a) - voltage(s, b)) * ip(s, b)); });
                }
            if (behavior == "capacitor" || behavior == "polarCap")
                check("Capacitor voltage", "V", limit("voltage"),
                      [&](const Sample &s) { return std::abs(delta(s)); });
            if (behavior == "polarCap")
                check("Polarized capacitor reverse bias", "V", .05,
                      [&](const Sample &s) { return -delta(s); });
            if (behavior == "inductor")
            {
                check("Winding current", "A", limit("current"),
                      [&](const Sample &s) { return std::abs(ip(s)); });
                check("Copper dissipation", "W", limit("power"),
                      [&](const Sample &s) { return ip(s) * ip(s) * parameter(p, "winding"); });
            }
            if (behavior == "zener")
                check("Zener dissipation", "W", limit("power"), power);
            if (behavior == "tvs")
            {
                check("TVS peak current", "A", limit("peakCurrent"),
                      [&](const Sample &s) { return std::abs(ip(s)); });
                aggregate("Captured TVS energy", "J", limit("energy"), integral(power, false));
            }
            if (behavior == "led" || p.kind == "SCHOTTKY")
            {
                check("Forward current", "A", limit("current"), [&](const Sample &s) { return ip(s); });
                check("Reverse voltage", "V", limit("reverse"), [&](const Sample &s) { return -delta(s); });
            }
            if (p.kind == "RECT")
            {
                aggregate("Capture-average forward current", "A", limit("averageCurrent"),
                          integral(
                              [&](const Sample &s)
                              {
                                  const double v = ip(s);
                                  return std::isfinite(v) ? qMax(0., v) : nan;
                              },
                              true));
                check("Reverse voltage", "V", limit("reverse"), [&](const Sample &s) { return -delta(s); });
            }
            if (behavior == "bridge")
            {
                aggregate("Capture-average output current", "A", limit("averageCurrent"),
                          integral(
                              [&](const Sample &s)
                              {
                                  const double v = ip(s, 2);
                                  return std::isfinite(v) ? qMax(0., -v) : nan;
                              },
                              true));
                check("Bridge reverse voltage", "V", limit("reverse"),
                      [&](const Sample &s)
                      {
                          return qMax(qMax(voltage(s, 2) - voltage(s, 0), voltage(s, 2) - voltage(s, 1)),
                                      qMax(voltage(s, 0) - voltage(s, 3), voltage(s, 1) - voltage(s, 3)));
                      });
            }
            if (behavior == "pnp")
            {
                check("Emitter-collector voltage", "V", limit("vce"),
                      [&](const Sample &s) { return voltage(s, 2) - voltage(s, 0); });
                check("Base-collector voltage", "V", limit("vcb"),
                      [&](const Sample &s) { return voltage(s, 1) - voltage(s, 0); });
                check("Base-emitter reverse voltage", "V", limit("veb"),
                      [&](const Sample &s) { return voltage(s, 1) - voltage(s, 2); });
                check("Collector current", "A", limit("current"),
                      [&](const Sample &s) { return std::abs(ip(s)); });
                check("Transistor dissipation", "W", limit("power"), power);
            }
            if (behavior == "switch" || behavior == "jumper" || p.kind == "FUSE")
                check("Contact / fuse current", "A", limit("current"),
                      [&](const Sample &s)
                      {
                          double maximum = 0;
                          for (int pin = 0; pin < spec(p.kind).pins.size(); ++pin)
                          {
                              double v = ip(s, pin);
                              if (!std::isfinite(v))
                                  return nan;
                              maximum = qMax(maximum, std::abs(v));
                          }
                          return maximum;
                      });
            if (behavior == "switch" || behavior == "jumper")
                check("Contact voltage", "V", limit("voltage"),
                      [&](const Sample &s)
                      {
                          double maximum = 0;
                          for (const auto &group : contactGroups(p.kind))
                              for (int i = 1; i < group.size(); ++i)
                                  maximum =
                                      qMax(maximum, std::abs(voltage(s, group[0]) - voltage(s, group[i])));
                          return maximum;
                      });
        }
        if (p.kind == "R")
            check("Resistor power", "W", limit("power"),
                  [&](const Sample &s) { return std::abs(delta(s) * current(s, "vi_")); });
        if (p.kind == "C" || p.kind == "CP")
        {
            check("Capacitor voltage", "V", limit("voltage"),
                  [&](const Sample &s) { return std::abs(delta(s)); });
            if (p.kind == "CP")
                check("Polarized capacitor reverse bias", "V", .05,
                      [&](const Sample &s) { return -delta(s); });
        }
        if (p.kind == "LED" || p.kind == "D")
        {
            check("Forward current", "A", limit("current"),
                  [&](const Sample &s) { return current(s, "vi_"); });
            check("Reverse voltage", "V", limit("reverse"), [&](const Sample &s) { return -delta(s); });
        }
        if (p.kind == "NPN")
        {
            check("Collector-emitter voltage", "V", limit("vce"),
                  [&](const Sample &s) { return voltage(s, 0) - voltage(s, 2); });
            check("Collector-base voltage", "V", limit("vcb"), delta);
            check("Emitter-base reverse voltage", "V", limit("veb"),
                  [&](const Sample &s) { return voltage(s, 2) - voltage(s, 1); });
            check("Collector current", "A", limit("current"),
                  [&](const Sample &s) { return std::abs(current(s, "vic_")); });
            check("Transistor dissipation", "W", limit("power"),
                  [&](const Sample &s)
                  {
                      return std::abs((voltage(s, 0) - voltage(s, 2)) * current(s, "vic_") +
                                      (voltage(s, 1) - voltage(s, 2)) * current(s, "vib_"));
                  });
        }
        if (p.kind == "LM339")
        {
            auto supply = [&](const Sample &s) { return voltage(s, 2) - voltage(s, 11); };
            check("Supply maximum", "V", limit("supply"), supply);
            check("Supply above recommended range", "V", 30, supply, true);
            check("Supply below recommended range", "V", 2, supply, true, true);
            int channel = 0;
            for (const auto &ch : spec(p.kind).comparators)
            {
                const auto name = "Channel " + QString::number(++channel);
                const auto branch =
                    "voc_" + p.id.toLower() + "_" + QString::number(ch.output + 1) + "#branch";
                check(name + " output current", "A", limit("current"),
                      [&](const Sample &s) { return std::abs(s.values.value(branch, nan)); });
                check(name + " output voltage", "V", limit("outputVoltage"),
                      [&](const Sample &s) { return voltage(s, ch.output) - voltage(s, 11); });
                check(
                    name + " output below ground", "V", .001,
                    [&](const Sample &s) { return voltage(s, 11) - voltage(s, ch.output); }, true);
                check(name + " differential input voltage", "V", 36, [&](const Sample &s)
                      { return std::abs(voltage(s, ch.positive) - voltage(s, ch.negative)); });
                for (const int pin : {ch.negative, ch.positive})
                {
                    const auto label = name + " pin " + QString::number(pin + 1);
                    check(label + " input maximum", "V", 36,
                          [&](const Sample &s) { return voltage(s, pin) - voltage(s, 11); });
                    check(label + " input below absolute minimum", "V", .3,
                          [&](const Sample &s) { return voltage(s, 11) - voltage(s, pin); });
                    check(
                        label + " input below operating ground", "V", .001,
                        [&](const Sample &s) { return voltage(s, 11) - voltage(s, pin); }, true);
                    check(
                        label + " input common-mode headroom", "V", 1.5,
                        [&](const Sample &s) { return voltage(s, 2) - voltage(s, pin); }, true, true);
                }
                const Pin output{p.id, ch.output};
                bool observed = c.probes.values().contains(output);
                for (const auto &wire : c.wires)
                    observed |= wire.a == output || wire.b == output;
                for (const auto &label : c.labels)
                    observed |= label.pin == output;
                if (observed && !c.hasExternalBias(output))
                    report.incomplete
                        << p.id + " | " + name +
                               ": open-collector output has no recognized external resistive/source bias "
                               "path; released output is not a valid high.";
            }
        }
        if (p.kind == "555")
        {
            auto supply = [&](const Sample &s) { return voltage(s, 7) - voltage(s, 0); };
            check("Supply maximum", "V", limit("supply"), supply);
            check("Supply above recommended range", "V", 16, supply, true);
            check("Supply below recommended range", "V", 4.5, supply, true, true);
            check(
                "Output current", "A", limit("current"),
                [&](const Sample &s) { return std::abs(current(s, "vio_")); }, true);
        }
    }
    return report;
}
} // namespace cmp
