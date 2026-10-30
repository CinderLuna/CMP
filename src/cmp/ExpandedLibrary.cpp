#include "cmp/ExpandedLibrary.h"
#include "cmp/DigitalDevices.h"
#include "cmp/DisplayDevices.h"
#include "cmp/PackageArtwork.h"
#include "cmp/Theme.h"
#include <cmath>
static void initExpandedResources()
{
    Q_INIT_RESOURCE(cmp_models);
}
namespace cmp
{
static ParameterSpec setting(QString key, QString label, QString unit, double value, double lo = -1e12,
                             double hi = 1e12)
{
    return {key, label, unit, value, lo, hi};
}
void appendExpandedCatalog(QVector<PartSpec> &out)
{
    auto add = [&](QString id, QString name, QString category, QStringList pins, double value,
                   QString behavior, QString unit = {}, QVector<ParameterSpec> settings = {}) -> PartSpec &
    {
        PartSpec s{id, name, "X", pins, value, -2};
        s.category = category;
        s.behavior = behavior;
        s.unit = unit;
        s.settings = settings;
        s.package = pins.size() > 3 ? "CMP functional package" : "Discrete";
        s.model = "CMP_" + id;
        s.limitations = "Original MIT educational model; no thermal destruction or manufacturer signoff.";
        if (behavior == "switch")
            s.prefix = "S";
        if (behavior == "resistor" || behavior == "pot" || behavior == "rnet")
            s.prefix = "R";
        else if (behavior == "capacitor" || behavior == "polarCap")
            s.prefix = "C";
        else if (behavior == "diode" || behavior == "zener" || behavior == "led" || behavior == "tvs")
            s.prefix = "D";
        else if (behavior == "inductor")
            s.prefix = "L";
        else if (behavior == "pnp" || behavior == "mosfet" || behavior == "darlington")
            s.prefix = "Q";
        else if (category == "Sources")
            s.prefix = "V";
        out << s;
        return out.last();
    };
    add("POT", "Potentiometer", "Passives", {"A", "W", "B"}, 10000, "pot", "Ω",
        {setting("wiper", "Wiper fraction", "", .5, 0, 1)});
    add("SW", "Toggle switch", "Switches", {"1", "2"}, 0, "switch", "",
        {setting("state", "Initial closed (0/1)", "", 0, 0, 1), setting("on", "Close time", "s", .1, 0, 60),
         setting("off", "Open time", "s", .2, 0, 60),
         setting("scheduled", "Replay events (0/1)", "", 0, 0, 1),
         setting("ron", "On resistance", "Ω", .01, 1e-6, 1e6),
         setting("roff", "Off resistance", "Ω", 1e12, 1, 1e15)});
    auto sw = out.last();
    sw.kind = "PUSH";
    sw.name = "Pushbutton NO";
    sw.behavior = "switch";
    sw.settings[3].initial = 1;
    out << sw;
    add("PNP", "2N3906 PNP", "Transistors", {"C", "B", "E"}, 0, "pnp");
    add("SINE", "Sine source", "Sources", {"+", "−"}, 5, "sine", "V",
        {setting("offset", "DC offset", "V", 0), setting("frequency", "Frequency", "Hz", 50, 1e-6, 1e9),
         setting("phase", "Phase", "deg", 0, -360, 360)});
    auto &current = add("I", "Current source", "Sources", {"+", "−"}, .001, "current", "A");
    current.prefix = "I";
    add("RECT", "1N4007 diode", "Diodes & protection", {"A", "K"}, 0, "diode");
    add("ZENER", "Zener diode", "Diodes & protection", {"A", "K"}, 5.1, "zener", "V",
        {setting("testCurrent", "Breakdown reference current", "A", .005, 1e-9, 10),
         setting("series", "Series resistance", "Ω", 2, 0, 1e6)});
    add("SCHOTTKY", "Schottky diode", "Diodes & protection", {"A", "K"}, 0, "diode");
    add("L", "Inductor", "Passives", {"1", "2"}, .01, "inductor", "H",
        {setting("winding", "Winding resistance", "Ω", 1, 1e-6, 1e9)});
    for (auto color : {"Amber", "Green", "Blue", "White"})
        add("LED_" + QString(color).toUpper(), QString(color) + " LED", "Diodes & protection", {"A", "K"}, 0,
            "led");
    add("RHEO", "Rheostat", "Passives", {"1", "2"}, 1000, "resistor", "Ω");
    const auto capSettings =
        QVector<ParameterSpec>{setting("esr", "Series resistance", "Ω", 0, 0, 1e9),
                               setting("leakage", "Leakage resistance (0=off)", "Ω", 0, 0, 1e15)};
    add("CERAMIC", "Ceramic capacitor", "Passives", {"1", "2"}, 1e-7, "capacitor", "F", capSettings);
    add("FILM", "Film capacitor", "Passives", {"1", "2"}, 1e-7, "capacitor", "F", capSettings);
    add("ELECTRO", "Electrolytic", "Passives", {"+", "−"}, 1e-5, "polarCap", "F", capSettings);
    add("TANTALUM", "Tantalum capacitor", "Passives", {"+", "−"}, 1e-6, "polarCap", "F", capSettings);
    add("RPOWER", "Power resistor", "Passives", {"1", "2"}, 100, "resistor", "Ω",
        {setting("tolerance", "Visual tolerance", "%", 5, .01, 20)});
    add("RNET_B", "Bussed resistor array", "Passives", {"COM", "R1", "R2", "R3", "R4"}, 1000, "rnet", "Ω");
    add("RNET_I", "Isolated resistor array", "Passives", {"A1", "B1", "A2", "B2", "A3", "B3", "A4", "B4"},
        1000, "rnet", "Ω");
    add("ESRC", "Capacitor ESR/leak", "Passives", {"1", "2"}, 1e-5, "capacitor", "F",
        {setting("esr", "Series resistance", "Ω", .1, 0, 1e9),
         setting("leakage", "Leakage resistance (0=off)", "Ω", 1e6, 0, 1e15)});
    add("TVS", "TVS diode", "Diodes & protection", {"A", "K"}, 12, "tvs", "V",
        {setting("bidirectional", "Bidirectional (0/1)", "", 0, 0, 1),
         setting("series", "Dynamic resistance", "Ω", 1, .001, 1e6)});
    add("BRIDGE", "Bridge rectifier", "Diodes & protection", {"AC1", "AC2", "+", "−"}, 0, "bridge");
    add("FUSE", "Fuse", "Diodes & protection", {"1", "2"}, .05, "resistor", "Ω");
    add("JUMPER", "Jumper", "Connectors", {"1", "2"}, 0, "jumper", "",
        {setting("closed", "Closed (0/1)", "", 1, 0, 1)});
    for (const auto &entry : QVector<QPair<QString, QString>>{{"SPDT", "SPDT switch"},
                                                              {"DPST", "DPST switch"},
                                                              {"DPDT", "DPDT switch"},
                                                              {"PUSH_NC", "Pushbutton NC"},
                                                              {"DIP8", "DIP switch bank"},
                                                              {"REED", "Reed contact"}})
    {
        auto s = sw;
        s.kind = entry.first;
        s.name = entry.second;
        s.behavior = "switch";
        s.model = "CMP_" + s.kind;
        s.prefix = "S";
        s.settings[3].initial = 0;
        if (s.kind == "SPDT")
            s.pins = {"COM", "NC", "NO"};
        if (s.kind == "DPST")
            s.pins = {"A1", "A2", "B1", "B2"};
        if (s.kind == "DPDT")
            s.pins = {"COM1", "NC1", "NO1", "COM2", "NC2", "NO2"};
        if (s.kind == "PUSH_NC")
        {
            s.settings[0].initial = 1;
            s.settings[3].initial = 1;
        }
        if (s.kind == "SPDT" || s.kind == "DPDT")
            s.settings << setting("deadtime", "Break-before-make interval", "s", 1e-6, 0, 1);
        if (s.kind == "DIP8")
        {
            s.pins.clear();
            s.settings.clear();
            for (int i = 0; i < 8; ++i)
            {
                s.pins << QString::number(i + 1) + "A" << QString::number(i + 1) + "B";
                s.settings << setting("bit" + QString::number(i),
                                      "Switch " + QString::number(i + 1) + " closed (0/1)", "", 0, 0, 1);
            }
            s.settings << setting("ron", "On resistance", "Ω", .01, 1e-6, 1e6)
                       << setting("roff", "Off resistance", "Ω", 1e12, 1, 1e15);
        }
        out << s;
    }
    add("HEADER", "Header 4-pin", "Connectors", {"1", "2", "3", "4"}, 0, "connector").prefix = "J";
    add("TERMINAL", "Terminal block", "Connectors", {"1", "2", "3", "4"}, 0, "connector").prefix = "J";
    add("TP", "Test point", "Connectors", {"TP"}, 0, "testpoint").prefix = "TP";
    for (QString kind : {"JPLUG", "JSOCKET"})
        add(kind, kind == "JPLUG" ? "Connector plug" : "Connector socket", "Connectors", {"1", "2", "3", "4"},
            0, "connector", "",
            {setting("link", "Mating identity", "", 1, 1, 1e6), setting("mated", "Mated (0/1)", "", 0, 0, 1)})
            .prefix = "J";
    add("RAIL", "Rail alias", "Sources", {"RAIL"}, 0, "rail", "",
        {setting("rail", "Rail identity", "", 1, 1, 1e6)})
        .prefix = "P";
    QStringList holes;
    for (int row = 1; row <= 20; ++row)
        for (int col = 0; col < 10; ++col)
            holes << QString(QChar('a' + col)) + QString::number(row);
    for (int strip = 0; strip < 4; ++strip)
        for (int row = 1; row <= 20; ++row)
            holes << "rail" + QString::number(strip + 1) + "." + QString::number(row);
    add("BOARD", "Breadboard", "Connectors", holes, 0, "board").prefix = "BB";
    const auto temperatureSettings =
        QVector<ParameterSpec>{setting("temperature", "Temperature", "°C", 25, -100, 200),
                               setting("referenceTemperature", "Reference temperature", "°C", 25, -100, 200)};
    add("LDR", "Photoresistor", "Sensors", {"1", "2"}, 10000, "ldr", "Ω",
        {setting("lux", "Illumination", "lux", 100, 0, 1e9),
         setting("referenceLux", "Reference illumination", "lux", 100, .001, 1e9),
         setting("gamma", "Response exponent", "", .7, 0, 5),
         setting("dark", "Maximum dark resistance", "Ω", 1e9, 1, 1e15),
         setting("light", "Minimum light resistance", "Ω", 100, 1e-6, 1e15)});
    auto ntc = temperatureSettings;
    ntc << setting("beta", "Beta parameter", "K", 3950, 1, 20000);
    add("NTC", "NTC thermistor", "Sensors", {"1", "2"}, 10000, "ntc", "Ω", ntc);
    auto ptc = temperatureSettings;
    ptc << setting("alpha", "Linear coefficient per °C", "", .004, 0, 1);
    add("PTC", "PTC linear sensor", "Sensors", {"1", "2"}, 100, "ptc", "Ω", ptc);
    add("PHOTODIODE", "Photodiode", "Sensors", {"A", "K"}, 0, "photodiode", "",
        {setting("lux", "Illumination", "lux", 0, 0, 1e9),
         setting("response", "Photocurrent per lux", "", 1e-6, 0, 1)});
    add("PHOTOTRANS", "Phototransistor", "Sensors", {"C", "E"}, 0, "phototransistor", "",
        {setting("lux", "Illumination", "lux", 0, 0, 1e9),
         setting("response", "Collector current per lux", "", 1e-6, 0, 1),
         setting("ron", "Saturation resistance", "Ω", 50, .001, 1e6)});
    add("SENSOR", "Analog sensor", "Sensors", {"VCC", "GND", "OUT"}, 0, "sensor", "",
        {setting("stimulus", "Input stimulus", "", 50, -1e6, 1e6),
         setting("inputLow", "Input range minimum", "", 0, -1e6, 1e6),
         setting("inputHigh", "Input range maximum", "", 100, -1e6, 1e6),
         setting("outputLow", "Output at minimum", "V", .5, -100, 100),
         setting("outputHigh", "Output at maximum", "V", 4.5, -100, 100),
         setting("rout", "Output resistance", "Ω", 100, .001, 1e6),
         setting("currentLimit", "Output current limit", "A", .01, 1e-9, 10)})
        .dataKind = "curve";
    add("BATTERY", "Battery", "Sources", {"+", "−"}, 5, "battery", "V",
        {setting("resistance", "Internal resistance", "Ω", .1, 1e-6, 1e6),
         setting("capacity", "Capacity (0=fixed source)", "Ah", 0, 0, 10000),
         setting("charge", "Initial charge fraction", "", 1, 0, 1)});
    add("IPULSE", "Pulsed current", "Sources", {"+", "−"}, .001, "ipulse", "A",
        {setting("low", "Low current", "A", 0), setting("delay", "Delay", "s", .01, 0, 60),
         setting("rise", "Rise time", "s", 1e-6, 1e-12, 60),
         setting("fall", "Fall time", "s", 1e-6, 1e-12, 60),
         setting("width", "High duration", "s", .01, 1e-12, 60),
         setting("period", "Period", "s", .02, 1e-12, 60)})
        .prefix = "I";
    add("RAMP", "Ramp source", "Sources", {"+", "−"}, 5, "ramp", "V",
        {setting("low", "Starting level", "V", 0), setting("delay", "Start delay", "s", .01, 0, 60),
         setting("rise", "Ramp duration", "s", .01, 1e-9, 60)});
    add("TRIANGLE", "Triangle source", "Sources", {"+", "−"}, 5, "triangle", "V",
        {setting("offset", "DC offset", "V", 0), setting("frequency", "Frequency", "Hz", 50, 1e-6, 1e9),
         setting("phase", "Phase", "deg", 0, -360, 360)});
    add("SQUARE", "Square source", "Sources", {"+", "−"}, 5, "square", "V",
        {setting("low", "Low level", "V", 0), setting("frequency", "Frequency", "Hz", 1000, 1e-6, 1e9),
         setting("duty", "Duty fraction", "", .5, .001, .999), setting("delay", "Delay", "s", 0, 0, 60),
         setting("rise", "Rise time", "s", 1e-6, 1e-12, 60),
         setting("fall", "Fall time", "s", 1e-6, 1e-12, 60)});
    add("PWL", "PWL source", "Sources", {"+", "−"}, 0, "pwl", "").dataKind = "pwl";
    add("NOISE", "Seeded noise", "Sources", {"+", "−"}, .1, "noise", "V",
        {setting("offset", "DC offset", "V", 0), setting("rate", "Sample/hold rate", "Hz", 1000, 1e-6, 1e9),
         setting("seed", "Reproducible seed", "", 12345, 1, 1e9)});
    for (QString type : {"NMOS", "PMOS"})
        add(type, type == "NMOS" ? "N-channel MOSFET" : "P-channel MOSFET", "Transistors",
            {"2 D", "1 G", "3 S"}, 2, "mosfet", "V",
            {setting("kp", "Transconductance parameter", "", .1, 1e-9, 1e6),
             setting("lambda", "Channel modulation", "", .02, 0, 1),
             setting("gateCap", "Gate capacitance", "F", 1e-10, 0, 1)})
            .package = "Generic G/D/S TO-220 functional model";
    for (QString type : {"DARL_N", "DARL_P"})
        add(type, type == "DARL_N" ? "Darlington NPN" : "Darlington PNP", "Transistors", {"C", "B", "E"}, 0,
            "darlington");
    add("OPTO", "Optocoupler", "Sensors", {"1 A", "2 K", "4 C", "3 E"}, 0, "opto", "",
        {setting("ctr", "Current-transfer ratio", "", .5, 0, 100),
         setting("ron", "Saturation resistance", "Ω", 20, .001, 1e6)});
    add("RELAY", "Relay SPDT", "Loads & magnetics", {"COIL+", "COIL−", "COM", "NC", "NO"}, 250, "relay", "Ω",
        {setting("inductance", "Coil inductance", "H", .05, 1e-9, 1e6),
         setting("pullIn", "Pull-in current", "A", .015, 1e-9, 100),
         setting("dropOut", "Drop-out current", "A", .008, 0, 100),
         setting("response", "Contact response time constant", "s", .001, 1e-9, 10),
         setting("contactResistance", "Contact on resistance", "Ω", .05, 1e-6, 1e6),
         setting("initialCurrent", "Initial coil current", "A", 0, -100, 100)});
    add("TRANSFORMER", "Transformer", "Loads & magnetics", {"P•", "P", "S•", "S"}, 1, "transformer", "H",
        {setting("secondary", "Secondary inductance", "H", 4, 1e-9, 1e9),
         setting("coupling", "Coupling factor", "", .99, .001, 1),
         setting("primaryResistance", "Primary winding resistance", "Ω", .1, 1e-6, 1e9),
         setting("secondaryResistance", "Secondary winding resistance", "Ω", .1, 1e-6, 1e9),
         setting("primaryCurrent", "Initial primary current", "A", 0),
         setting("secondaryCurrent", "Initial secondary current", "A", 0)});
    add("MOTOR", "DC motor", "Loads & magnetics", {"+", "−"}, 2, "motor", "Ω",
        {setting("inductance", "Winding inductance", "H", .001, 1e-9, 1e6),
         setting("kt", "Torque constant (N m/A)", "", .02, 1e-9, 1e6),
         setting("ke", "Back-EMF constant (V s/rad)", "", .02, 1e-9, 1e6),
         setting("inertia", "Rotor inertia (kg m²)", "", 1e-5, 1e-12, 1e6),
         setting("friction", "Viscous friction (N m s/rad)", "", 1e-5, 0, 1e6),
         setting("load", "Load torque (N m)", "", 0, -1e6, 1e6),
         setting("speed", "Initial speed (rad/s)", "", 0, -1e9, 1e9),
         setting("angle", "Initial angle (rad)", "", 0, -1e9, 1e9)});
    add("BUZZER", "Active buzzer", "Loads & magnetics", {"+", "−"}, 5, "buzzer", "V",
        {setting("current", "Powered current", "A", .02, 1e-9, 100),
         setting("threshold", "Activation voltage", "V", 2.5, 0, 100)});
    add("PIEZO", "Passive piezo", "Loads & magnetics", {"1", "2"}, 1000, "acoustic", "Ω",
        {setting("inductance", "Motional inductance", "H", .01, 1e-12, 1e9),
         setting("motionalCap", "Motional capacitance", "F", 5e-9, 1e-15, 1),
         setting("parallelCap", "Static capacitance", "F", 20e-9, 0, 1)});
    add("SPEAKER", "Speaker RLC", "Loads & magnetics", {"+", "−"}, 8, "acoustic", "Ω",
        {setting("inductance", "Motional inductance", "H", .0005, 1e-12, 1e9),
         setting("motionalCap", "Motional capacitance", "F", 1e-4, 1e-15, 1),
         setting("parallelCap", "Static capacitance", "F", 0, 0, 1)});
    for (QString type : {"SCR", "TRIAC"})
        add(type, type == "SCR" ? "SCR thyristor" : "TRIAC", "Transistors",
            type == "SCR" ? QStringList{"A", "G", "K"} : QStringList{"MT2", "G", "MT1"}, .001, "thyristor",
            "A",
            {setting("trigger", "Gate trigger current", "A", .001, 1e-9, 100),
             setting("gateResistance", "Gate resistance", "Ω", 100, .001, 1e9),
             setting("ron", "On resistance", "Ω", 1, 1e-6, 1e6),
             setting("response", "Latch response time", "s", 1e-8, 1e-12, 1)});

    for (auto &definition : out)
        if (definition.behavior == "rnet")
            for (int i = 0; i < 4; ++i)
                definition.settings << setting("element" + QString::number(i),
                                               QString("Element %1 override (0=nominal)").arg(i + 1), "Ω", 0,
                                               0, 1e12);
    appendAnalogCatalog(out);
    appendDigitalCatalog(out);
    appendDisplayCatalog(out);
}
bool parseCurve(const QByteArray &data, QVector<QPointF> &points, QString &error, bool timeAxis)
{
    points.clear();
    const auto text = QString::fromUtf8(data).replace(';', '\n');
    for (auto line : text.split('\n'))
    {
        line = line.section('#', 0, 0).trimmed();
        if (line.isEmpty())
            continue;
        const auto columns = line.replace(',', ' ').simplified().split(' ');
        double x, y;
        if (columns.size() != 2 || !parseQuantityUnit(columns.value(0), timeAxis ? "s" : "", x) ||
            !parseQuantityUnit(columns.value(1), "V", y) || !std::isfinite(x) || !std::isfinite(y) ||
            (timeAxis && x < 0) || (!points.isEmpty() && x <= points.back().x()) || points.size() >= 4096)
        {
            error = "Use at most 4096 strictly increasing x/value pairs with finite SI values.";
            return false;
        }
        points << QPointF(x, y);
    }
    if (points.size() < 2)
    {
        error = "At least two x/value points are required.";
        return false;
    }
    return true;
}
static double sensorResistance(const Part &p)
{
    const auto b = spec(p.kind).behavior;
    if (b == "ldr")
        return qBound(parameter(p, "light"),
                      p.value * std::pow(qMax(1e-9, parameter(p, "lux")) / parameter(p, "referenceLux"),
                                         -parameter(p, "gamma")),
                      parameter(p, "dark"));
    if (b == "ntc")
        return p.value *
               std::exp(parameter(p, "beta") * (1 / (parameter(p, "temperature") + 273.15) -
                                                1 / (parameter(p, "referenceTemperature") + 273.15)));
    return p.value *
           (1 + parameter(p, "alpha") * (parameter(p, "temperature") - parameter(p, "referenceTemperature")));
}
bool expanded(const QString &kind)
{
    return !spec(kind).behavior.isEmpty();
}
double parameter(const Part &p, const QString &key)
{
    for (const auto &s : spec(p.kind).settings)
        if (s.key == key)
            return p.parameters.value(key, s.initial);
    return p.parameters.value(key, 0);
}
QStringList validateExpanded(const Part &p)
{
    QStringList errors;
    const auto &definition = spec(p.kind);
    if (!definition.unit.isEmpty() && definition.category != "Sources" && p.value <= 0)
        errors << p.id + ": value must be positive.";
    for (const auto &s : definition.settings)
    {
        const double value = parameter(p, s.key);
        if (!std::isfinite(value) || value < s.minimum || value > s.maximum)
            errors << p.id + ": invalid " + s.label + ".";
        if ((s.key == "state" || s.key == "scheduled" || s.key == "bidirectional" || s.key == "closed" ||
             s.key == "mated" || s.key == "level" || s.key.startsWith("key") || s.key.startsWith("bit")) &&
            value != 0 && value != 1)
            errors << p.id + ": " + s.label + " must be 0 or 1.";
    }
    for (const auto &key : {QString("link"), QString("rail")})
        if (p.parameters.contains(key) && std::floor(parameter(p, key)) != parameter(p, key))
            errors << p.id + ": identity must be an integer.";
    if (p.kind == "RPOWER" &&
        !QVector<double>{.05, .1, .25, .5, 1, 2, 5, 10, 20}.contains(parameter(p, "tolerance")))
        errors << p.id + ": use a supported color-code tolerance.";
    if (parameter(p, "scheduled") && parameter(p, "deadtime") >= parameter(p, "off") - parameter(p, "on") &&
        parameter(p, "deadtime") > 0)
        errors << p.id + ": break interval must fit the scheduled transition.";
    if (definition.behavior == "switch" && parameter(p, "scheduled") &&
        parameter(p, "off") <= parameter(p, "on"))
        errors << p.id + ": opening time must follow closing time.";
    const auto b = definition.behavior;
    if (digitalDevice(p.kind) && p.parameters.contains("initial") &&
        std::floor(parameter(p, "initial")) != parameter(p, "initial"))
        errors << p.id + ": initial digital state must be an integer.";
    if (p.kind == "CPU8085" && (p.value <= 0 || p.value > 1e8))
        errors << p.id + ": processor clock must be within 0–100 MHz.";
    if (b == "opamp" && p.value <= 0)
        errors << p.id + ": open-loop gain must be positive.";
    if (b == "avr" && p.value != 16000000)
        errors << p.id + ": Uno clock is fixed at 16 MHz.";
    if (b == "ldr" && parameter(p, "dark") < parameter(p, "light"))
        errors << p.id + ": dark resistance must not be below light resistance.";
    if ((b == "ldr" || b == "ntc" || b == "ptc") &&
        (!std::isfinite(sensorResistance(p)) || sensorResistance(p) < 1e-9 || sensorResistance(p) > 1e15))
        errors << p.id + ": derived resistance is outside the supported range.";
    if (b == "sensor" && parameter(p, "inputHigh") <= parameter(p, "inputLow"))
        errors << p.id + ": sensor range must increase.";
    if (b == "battery" && p.value <= 0)
        errors << p.id + ": battery voltage must be positive.";
    if (b == "ipulse" &&
        parameter(p, "period") < parameter(p, "width") + parameter(p, "rise") + parameter(p, "fall"))
        errors << p.id + ": pulse edges/width must fit its period.";
    if (b == "square" && parameter(p, "rise") + parameter(p, "fall") >=
                             qMin(parameter(p, "duty"), 1 - parameter(p, "duty")) / parameter(p, "frequency"))
        errors << p.id + ": edges are too long for the selected frequency/duty.";
    if ((b == "noise") && std::floor(parameter(p, "seed")) != parameter(p, "seed"))
        errors << p.id + ": seed must be an integer.";
    if (b == "relay" && parameter(p, "pullIn") <= parameter(p, "dropOut"))
        errors << p.id + ": pull-in current must exceed drop-out current.";
    if ((p.kind == "RAM" || p.kind == "ROM") && p.data.size() > 256)
        errors << p.id + ": memory image exceeds 256 bytes.";
    if ((p.kind == "UNO" || p.kind == "CPU8085") && p.data.size() > 32768)
        errors << p.id + ": firmware exceeds 32 KiB flash.";
    if (!p.data.isEmpty())
    {
        if (p.dataType != definition.dataKind || definition.dataKind.isEmpty())
            errors << p.id + ": unsupported component payload.";
        else if (definition.dataKind == "pwl" || definition.dataKind == "curve")
        {
            QVector<QPointF> points;
            QString error;
            if (!parseCurve(p.data, points, error, definition.dataKind == "pwl"))
                errors << p.id + ": " + error;
        }
    }
    return errors;
}
QStringList internalMeasurements(const Part &p)
{
    if (p.kind == "SEG_CC" || p.kind == "SEG_CA")
    {
        QStringList names;
        for (int i = 0; i < 8; ++i)
            names << "pin" + QString::number(i);
        return names;
    }
    if (p.kind == "MATRIX")
    {
        QStringList names;
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 4; ++c)
                names << "cell" + QString::number(r) + QString::number(c);
        return names;
    }

    const auto b = spec(p.kind).behavior;
    if (b == "motor")
        return {"speed", "angle"};
    if (b == "relay" || b == "thyristor")
        return {"state"};
    if (b == "battery" && parameter(p, "capacity") > 0)
        return {"soc"};
    return {};
}
QString internalKey(const Part &p, const QString &name)
{
    if (name.startsWith("pin"))
        return currentKey(p, name.mid(3).toInt());
    if (name.startsWith("cell"))
        return "v__cell_" + p.id.toLower() + "_" + name.mid(4) + "#branch";
    return "x__cmp_" + p.id.toLower() + "." + name;
}
bool measuresPins(const QString &kind)
{
    const auto b = spec(kind).behavior;
    return b != "board" && b != "connector" && b != "testpoint" && b != "rail";
}
QVector<QVector<int>> contactGroups(const QString &kind)
{
    if (kind == "SPDT")
        return {{0, 1, 2}};
    if (kind == "DPDT")
        return {{0, 1, 2}, {3, 4, 5}};
    if (kind == "DPST")
        return {{0, 1}, {2, 3}};
    if (kind == "DIP8")
    {
        QVector<QVector<int>> groups;
        for (int i = 0; i < 8; ++i)
            groups << QVector<int>{i * 2, i * 2 + 1};
        return groups;
    }
    return {{0, 1}};
}
QPointF physicalLocalPin(const Part &p, int index)
{
    if (expanded(p.kind))
        return expandedPin(p, index, true);
    if (p.kind == "LM339")
        return dipPinPosition(index, 14);
    static const QJsonObject artwork = []
    {
        initExpandedResources();
        QFile file(":/cmp/physical-pins.json");
        file.open(QIODevice::ReadOnly);
        return QJsonDocument::fromJson(file.readAll()).object();
    }();
    const auto a = artwork["pins"].toObject()[p.kind].toArray().at(index).toArray();
    const auto rect = artwork["artworkRect"].toArray();
    return {rect[0].toDouble() + a.at(0).toDouble() * rect[2].toDouble(),
            rect[1].toDouble() + a.at(1).toDouble() * rect[3].toDouble()};
}
static QPointF physicalPosition(const Part &p, int index)
{
    QTransform t;
    t.rotate(p.rotation);
    return p.position + t.map(physicalLocalPin(p, index));
}
QPointF snappedToBoard(const Circuit &c, const Part &p)
{
    if (p.kind == "BOARD")
        return p.position;
    QPointF delta;
    double nearest = 12;
    for (const auto &board : c.parts)
        if (board.kind == "BOARD")
            for (int hole = 0; hole < spec(board.kind).pins.size(); ++hole)
                for (int pin = 0; pin < spec(p.kind).pins.size(); ++pin)
                {
                    const auto offset = physicalPosition(board, hole) - physicalPosition(p, pin);
                    const auto distance = std::hypot(offset.x(), offset.y());
                    if (distance < nearest)
                    {
                        nearest = distance;
                        delta = offset;
                    }
                }
    return p.position + delta;
}
void joinExpandedConnections(const Circuit &c, const std::function<void(QString, QString)> &join)
{
    QMap<int, QString> rails;
    QMap<int, QVector<const Part *>> mates;
    for (const auto &p : c.parts)
    {
        if (p.kind == "RAIL")
        {
            const int id = int(parameter(p, "rail"));
            if (rails.contains(id))
                join(rails[id], Pin{p.id, 0}.key());
            else
                rails[id] = Pin{p.id, 0}.key();
        }
        if ((p.kind == "JPLUG" || p.kind == "JSOCKET") && parameter(p, "mated"))
            mates[int(parameter(p, "link"))] << &p;
        if (p.kind == "BOARD")
        {
            for (int row = 0; row < 20; ++row)
                for (int side = 0; side < 2; ++side)
                    for (int col = 1; col < 5; ++col)
                        join(Pin{p.id, row * 10 + side * 5}.key(),
                             Pin{p.id, row * 10 + side * 5 + col}.key());
            for (int rail = 0; rail < 4; ++rail)
                for (int half = 0; half < 2; ++half)
                    for (int row = 1; row < 10; ++row)
                        join(Pin{p.id, 200 + rail * 20 + half * 10}.key(),
                             Pin{p.id, 200 + rail * 20 + half * 10 + row}.key());
            for (const auto &part : c.parts)
                if (part.id != p.id && part.kind != "BOARD")
                    for (int pin = 0; pin < spec(part.kind).pins.size(); ++pin)
                        for (int hole = 0; hole < 280; ++hole)
                            if (QLineF(physicalPosition(part, pin), physicalPosition(p, hole)).length() < .01)
                                join(Pin{part.id, pin}.key(), Pin{p.id, hole}.key());
        }
    }
    for (const auto &parts : mates)
        if (parts.size() == 2 && parts[0]->kind != parts[1]->kind)
            for (int pin = 0; pin < 4; ++pin)
                join(Pin{parts[0]->id, pin}.key(), Pin{parts[1]->id, pin}.key());
}
QString currentKey(const Part &p, int pin)
{
    return "v__cmp_" + p.id.toLower() + "_" + QString::number(pin) + "#branch";
}
QStringList expandedNetlist(const Part &p, const QStringList &external)
{
    if (p.kind == "MATRIX")
    {
        QStringList lines;
        auto id = p.id.toLower();
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 4; ++c)
            {
                auto suffix = id + "_" + QString::number(r) + QString::number(c);
                auto node = "__cell_" + suffix;
                lines << "V__cell_" + suffix + " " + external[r] + " " + node + " 0"
                      << "D__cell_" + suffix + " " + node + " " + external[4 + c] + " CMP_LED";
            }
        return lines;
    }
    if (digitalDevice(p.kind))
        return digitalNetlist(p, external);
    if (p.kind == "UNO")
    {
        QStringList lines;
        QString id = p.id.toLower();
        for (int i = 0; i < 20; ++i)
        {
            auto suffix = id + "_" + QString::number(i);
            auto control = "__uno_ctl_" + suffix;
            auto v = "V(" + external[i] + "," + external[21] + ")";
            auto supply = "V(" + external[20] + "," + external[21] + ")";
            lines << "V__uno_" + suffix + " " + control + " 0 external";
            auto current = "V(" + control + ")<.5 ? " + v + "/25 : (V(" + control + ")<1.5 ? (" + v + "-" +
                           supply + ")/25 : (V(" + control + ")>2.5 ? (" + v + "-" + supply +
                           ")/50000 : " + v + "/1e12))";
            lines << "B__uno_" + suffix + " " + external[i] + " " + external[21] + " I=" + current;
            lines << "B__uno_power_" + suffix + " " + external[20] + " " + external[21] + " I=max(0,-(" +
                         current + "))";
        }
        lines << "R__uno_reset_" + id + " " + external[23] + " " + external[20] + " 10000"
              << "R__uno_ref_" + id + " " + external[22] + " " + external[21] + " 1e12";
        return lines;
    }

    QStringList lines, n;
    for (int pin = 0; pin < external.size(); ++pin)
        n << "p" + QString::number(pin);
    const auto &d = spec(p.kind);
    const auto b = d.behavior;
    const auto id = p.id.toLower();
    auto number = [](double v) { return QString::number(v, 'g', 16); };
    auto r = [&](QString tag, QString a, QString z, double value)
    { lines << "R" + id + tag + " " + a + " " + z + " " + number(value); };
    auto diode = [&](QString tag, QString a, QString z, QString model)
    { lines << "D" + id + tag + " " + a + " " + z + " " + model; };
    if (b == "resistor")
        r("", n[0], n[1], p.value);
    else if (b == "pot")
    {
        r("a", n[0], n[1], qMax(1e-6, p.value * (1 - parameter(p, "wiper"))));
        r("b", n[1], n[2], qMax(1e-6, p.value * parameter(p, "wiper")));
    }
    else if (b == "rnet")
    {
        for (int i = 0; i < 4; ++i)
            r(QString::number(i), n[p.kind == "RNET_B" ? 0 : i * 2],
              n[p.kind == "RNET_B" ? i + 1 : i * 2 + 1],
              parameter(p, "element" + QString::number(i)) > 0 ? parameter(p, "element" + QString::number(i))
                                                               : p.value);
    }
    else if (b == "capacitor" || b == "polarCap")
    {
        QString top = n[0];
        if (parameter(p, "esr") > 0)
        {
            top = "cap_" + id;
            r("esr", n[0], top, parameter(p, "esr"));
        }
        lines << "C" + id + " " + top + " " + n[1] + " " + number(p.value) + " IC=" + number(p.initial);
        if (parameter(p, "leakage") > 0)
            r("leak", top, n[1], parameter(p, "leakage"));
    }
    else if (b == "inductor")
    {
        r("w", n[0], "wind_" + id, parameter(p, "winding"));
        lines << "L" + id + " wind_" + id + " " + n[1] + " " + number(p.value) + " IC=" + number(p.initial);
    }
    else if (b == "pnp")
        lines << "Q" + id + " " + n.join(' ') + " CMP_PNP";
    else if (b == "sine")
        lines << "V" + id + " " + n.join(' ') + " SIN(" + number(parameter(p, "offset")) + " " +
                     number(p.value) + " " + number(parameter(p, "frequency")) + " 0 0 " +
                     number(parameter(p, "phase")) + ")";
    else if (b == "current")
        lines << "I" + id + " " + n.join(' ') + " " + number(p.value);
    else if (b == "ldr" || b == "ntc" || b == "ptc")
        r("", n[0], n[1], sensorResistance(p));
    else if (b == "photodiode")
    {
        diode("", n[0], n[1], "CMP_PHOTOD");
        lines << "Iphoto" + id + " " + n[1] + " " + n[0] + " " +
                     number(parameter(p, "lux") * parameter(p, "response"));
    }
    else if (b == "phototransistor")
    {
        lines << "Bphoto" + id + " " + n[0] + " " + n[1] + " I=max(0,min(" +
                     number(parameter(p, "lux") * parameter(p, "response")) + ",V(" + n[0] + "," + n[1] +
                     ")/" + number(parameter(p, "ron")) + "))";
        r("leak", n[0], n[1], 1e12);
    }
    else if (b == "sensor")
    {
        double fraction = qBound(0.,
                                 (parameter(p, "stimulus") - parameter(p, "inputLow")) /
                                     (parameter(p, "inputHigh") - parameter(p, "inputLow")),
                                 1.);
        double target =
            parameter(p, "outputLow") + fraction * (parameter(p, "outputHigh") - parameter(p, "outputLow"));
        if (!p.data.isEmpty())
        {
            QVector<QPointF> points;
            QString error;
            parseCurve(p.data, points, error, false);
            if (!points.isEmpty())
            {
                const double x = parameter(p, "stimulus");
                target = points.front().y();
                for (int i = 1; i < points.size(); ++i)
                {
                    if (x >= points[i].x())
                        target = points[i].y();
                    else
                    {
                        if (x > points[i - 1].x())
                            target = points[i - 1].y() + (x - points[i - 1].x()) /
                                                             (points[i].x() - points[i - 1].x()) *
                                                             (points[i].y() - points[i - 1].y());
                        break;
                    }
                }
            }
        }
        const auto desired = "max(0,min(" + number(target) + ",V(" + n[0] + "," + n[1] + ")))";
        const auto difference =
            "(" + desired + "-V(" + n[2] + "," + n[1] + "))/" + number(parameter(p, "rout"));
        lines << "Bsource" + id + " " + n[0] + " " + n[2] + " I=max(0,min(" +
                     number(parameter(p, "currentLimit")) + "," + difference + "))";
        lines << "Bsink" + id + " " + n[2] + " " + n[1] + " I=max(0,min(" +
                     number(parameter(p, "currentLimit")) + ",-(" + difference + ")))";
        r("load", n[2], n[1], 1e9);
    }
    else if (b == "battery")
    {
        lines << "Vsense " + n[0] + " terminal 0";
        r("internal", "terminal", "emf", parameter(p, "resistance"));
        if (parameter(p, "capacity") > 0)
        {
            lines << "Csoc soc 0 " + number(parameter(p, "capacity") * 3600) +
                         " IC=" + number(parameter(p, "charge"));
            lines << "Bsoc 0 soc I=(V(soc)>=1 && I(Vsense)>0) ? 0 : I(Vsense)";
            lines << "Bemf emf " + n[1] + " V=(V(soc)>0) ? " + number(p.value) +
                         "*(.8+.2*min(V(soc),1))*min(V(soc)/.01,1) : 0";
        }
        else
            lines << "Vemf emf " + n[1] + " " + number(p.value);
    }
    else if (b == "ramp")
        lines << "B" + id + " " + n[0] + " " + n[1] + " V=" + number(parameter(p, "low")) + "+(" +
                     number(p.value - parameter(p, "low")) + ")*max(0,min(1,(time-" +
                     number(parameter(p, "delay")) + ")/" + number(parameter(p, "rise")) + "))";
    else if (b == "triangle")
    {
        const auto phase =
            "(time*" + number(parameter(p, "frequency")) + "+" + number(parameter(p, "phase") / 360) + ")";
        lines << "B" + id + " " + n[0] + " " + n[1] + " V=" + number(parameter(p, "offset")) + "+" +
                     number(p.value) + "*(1-4*abs(" + phase + "-floor(" + phase + ")-.5))";
    }
    else if (b == "ipulse" || b == "square")
    {
        const double period = b == "ipulse" ? parameter(p, "period") : 1 / parameter(p, "frequency");
        const double width = b == "ipulse" ? parameter(p, "width")
                                           : period * parameter(p, "duty") -
                                                 (parameter(p, "rise") + parameter(p, "fall")) * .5;
        lines << (b == "ipulse" ? "I" : "V") + id + " " + n.join(' ') + " PULSE(" +
                     number(parameter(p, "low")) + " " + number(p.value) + " " +
                     number(parameter(p, "delay")) + " " + number(parameter(p, "rise")) + " " +
                     number(parameter(p, "fall")) + " " + number(width) + " " + number(period) + ")";
    }
    else if (b == "pwl")
    {
        QVector<QPointF> points;
        QString error;
        parseCurve(p.data.isEmpty() ? QByteArray("0 0\n.1 5\n.2 0") : p.data, points, error);
        QStringList values;
        for (const auto &point : points)
            values << number(point.x()) << number(point.y());
        lines << "V" + id + " " + n.join(' ') + " PWL(" + values.join(' ') + ")";
    }
    else if (b == "noise")
    {
        const auto hash = "sin(floor(time*" + number(parameter(p, "rate")) + ")*12.9898+" +
                          number(parameter(p, "seed")) + "*78.233)*43758.5453";
        lines << "B" + id + " " + n[0] + " " + n[1] + " V=" + number(parameter(p, "offset")) + "+" +
                     number(p.value) + "*(2*((" + hash + ")-floor(" + hash + "))-1)";
    }
    else if (b == "diode")
        diode("", n[0], n[1], p.kind == "RECT" ? "CMP_RECT" : "CMP_SCHOTTKY");
    else if (b == "led")
        diode("", n[0], n[1], "CMP_" + p.kind);
    else if (b == "zener" || b == "tvs")
    {
        const auto model = "local_" + id;
        lines << ".model " + model + " D(Is=1e-14 N=1.8 Bv=" + number(p.value) +
                     " Ibv=" + number(b == "zener" ? parameter(p, "testCurrent") : .001) +
                     " Rs=" + number(parameter(p, "series")) + ")";
        if (b == "tvs" && parameter(p, "bidirectional"))
        {
            diode("a", n[0], "tvs_" + id, model);
            diode("b", n[1], "tvs_" + id, model);
        }
        else
            diode("", n[0], n[1], model);
    }
    else if (b == "bridge")
    {
        diode("a", n[0], n[2], "CMP_RECT");
        diode("b", n[1], n[2], "CMP_RECT");
        diode("c", n[3], n[0], "CMP_RECT");
        diode("d", n[3], n[1], "CMP_RECT");
    }
    else if (b == "jumper")
        r("", n[0], n[1], parameter(p, "closed") ? 1e-6 : 1e12);
    else if (b == "connector" || b == "testpoint" || b == "rail" || b == "board")
    {
        for (int i = 0; i < n.size(); ++i)
            r("leak" + QString::number(i), n[i], "0", 1e15);
    }
    else if (b == "switch")
    {
        for (int pin = 0; pin < n.size(); ++pin)
            r("ref" + QString::number(pin), n[pin], "0", 1e15);
        const auto control = "ctl_" + id;
        QString state = number(parameter(p, "state"));
        if (parameter(p, "scheduled"))
            state = "(time>=" + number(parameter(p, "on")) + " && time<" + number(parameter(p, "off")) +
                    ") ? " + number(1 - parameter(p, "state")) + " : " + number(parameter(p, "state"));
        const bool changeover = p.kind == "SPDT" || p.kind == "DPDT";
        if (changeover && parameter(p, "scheduled") && parameter(p, "deadtime") > 0)
            state = "((time>=" + number(parameter(p, "on")) + " && time<" +
                    number(parameter(p, "on") + parameter(p, "deadtime")) +
                    ") || (time>=" + number(parameter(p, "off")) + " && time<" +
                    number(parameter(p, "off") + parameter(p, "deadtime")) + ")) ? 2 : (" + state + ")";
        if (p.kind != "DIP8")
            lines << "B" + id + " " + control + " 0 V=" + state;
        lines << ".model sm_" + id + " SW(Ron=" + number(parameter(p, "ron")) +
                     " Roff=" + number(parameter(p, "roff")) + " Vt=.5 Vh=0)";
        auto groups = contactGroups(p.kind);
        int pole = 0;
        for (const auto &group : groups)
        {
            const auto suffix = QString::number(pole++);
            if (p.kind == "DIP8")
                lines << "B" + id + suffix + " " + control + suffix +
                             " 0 V=" + number(parameter(p, "bit" + QString::number(pole - 1)));
            if (group.size() == 2)
                lines << "S" + id + suffix + " " + n[group[0]] + " " + n[group[1]] + " " + control +
                             (p.kind == "DIP8" ? suffix : QString()) + " 0 sm_" + id;
            else
            {
                lines << "Bnc" + id + suffix + " nc_" + id + suffix + " 0 V=V(" + control + ")<.5 ? 1 : 0";
                lines << "Bno" + id + suffix + " no_" + id + suffix + " 0 V=(V(" + control + ")>.5 && V(" +
                             control + ")<1.5) ? 1 : 0";
                lines << "Snc" + id + suffix + " " + n[group[0]] + " " + n[group[1]] + " nc_" + id + suffix +
                             " 0 sm_" + id;
                lines << "Sno" + id + suffix + " " + n[group[0]] + " " + n[group[2]] + " no_" + id + suffix +
                             " 0 sm_" + id;
            }
        }
    }
    else if (b == "mosfet")
    {
        const auto model = "mos_" + id;
        const bool pm = p.kind == "PMOS";
        lines << ".model " + model + " " + (pm ? "PMOS" : "NMOS") +
                     "(Level=1 Vto=" + number(pm ? -p.value : p.value) + " Kp=" + number(parameter(p, "kp")) +
                     " Lambda=" + number(parameter(p, "lambda")) + " Is=1e-12)";
        lines << "M" + id + " " + n[0] + " " + n[1] + " " + n[2] + " " + n[2] + " " + model;
        if (parameter(p, "gateCap") > 0)
            lines << "Cgate " + n[1] + " " + n[2] + " " + number(parameter(p, "gateCap"));
    }
    else if (b == "darlington")
    {
        const auto model = p.kind == "DARL_P" ? "CMP_PNP" : "CMP_NPN";
        lines << "Qfirst " + n[0] + " " + n[1] + " dar_mid " + model
              << "Qsecond " + n[0] + " dar_mid " + n[2] + " " + model;
        r("bleed", "dar_mid", n[2], 1e6);
    }
    else if (b == "opto")
    {
        lines << "Vled " + n[0] + " led 0";
        diode("led", "led", n[1], "CMP_LED");
        lines << "Bphoto " + n[2] + " " + n[3] + " I=max(0,min(" + number(parameter(p, "ctr")) +
                     "*max(I(Vled),0),V(" + n[2] + "," + n[3] + ")/" + number(parameter(p, "ron")) + "))";
        r("output", n[2], n[3], 1e12);
    }
    else if (b == "relay")
    {
        r("coil", n[0], "coil", p.value);
        lines << "Lcoil coil " + n[1] + " " + number(parameter(p, "inductance")) +
                     " IC=" + number(parameter(p, "initialCurrent"));
        lines << "Bstate desired 0 V=(abs(I(Lcoil))>" + number(parameter(p, "pullIn")) +
                     " || (V(state)>.5 && abs(I(Lcoil))>" + number(parameter(p, "dropOut")) + ")) ? 1 : 0";
        r("delay", "desired", "state", 1000);
        lines << "Cstate state 0 " + number(parameter(p, "response") / 1000) + " IC=0";
        lines << "Bnot notstate 0 V=1-V(state)";
        lines << "Sno " + n[2] + " " + n[4] + " state 0 relay_sw"
              << "Snc " + n[2] + " " + n[3] + " notstate 0 relay_sw";
        lines << ".model relay_sw SW(Ron=" + number(parameter(p, "contactResistance")) +
                     " Roff=1e12 Vt=.5 Vh=0)";
    }
    else if (b == "transformer")
    {
        r("pri", n[0], "pri", parameter(p, "primaryResistance"));
        r("sec", n[2], "sec", parameter(p, "secondaryResistance"));
        lines << "Lprimary pri " + n[1] + " " + number(p.value) +
                     " IC=" + number(parameter(p, "primaryCurrent"));
        lines << "Lsecondary sec " + n[3] + " " + number(parameter(p, "secondary")) +
                     " IC=" + number(parameter(p, "secondaryCurrent"));
        lines << "Kcouple Lprimary Lsecondary " + number(parameter(p, "coupling"));
    }
    else if (b == "motor")
    {
        r("winding", n[0], "coil", p.value);
        lines << "Lcoil coil emf " + number(parameter(p, "inductance")) + " IC=0";
        lines << "Bemf emf " + n[1] + " V=" + number(parameter(p, "ke")) + "*V(speed)";
        lines << "Cspeed speed 0 " + number(parameter(p, "inertia")) + " IC=" + number(parameter(p, "speed"));
        lines << "Btorque 0 speed I=" + number(parameter(p, "kt")) + "*I(Lcoil)-" +
                     number(parameter(p, "friction")) + "*V(speed)-" + number(parameter(p, "load"));
        lines << "Cangle angle 0 1 IC=" + number(parameter(p, "angle")) << "Bangle 0 angle I=V(speed)";
    }
    else if (b == "buzzer")
        lines << "Bload " + n[0] + " " + n[1] + " I=V(" + n[0] + "," + n[1] +
                     ")>=" + number(parameter(p, "threshold")) + " ? " + number(parameter(p, "current")) +
                     " : 0";
    else if (b == "acoustic")
    {
        r("motional", n[0], "motion", p.value);
        lines << "Lmotion motion resonate " + number(parameter(p, "inductance"))
              << "Cmotion resonate " + n[1] + " " + number(parameter(p, "motionalCap"));
        if (parameter(p, "parallelCap") > 0)
            lines << "Cstatic " + n[0] + " " + n[1] + " " + number(parameter(p, "parallelCap"));
    }
    else if (b == "thyristor")
    {
        const bool bidirectional = p.kind == "TRIAC";
        lines << "Vsense " + n[0] + " anode 0";
        r("gate", n[1], n[2], parameter(p, "gateResistance"));
        const auto current = bidirectional ? "abs(I(Vsense))" : "I(Vsense)";
        const auto gate = "abs(V(" + n[1] + "," + n[2] + "))/" + number(parameter(p, "gateResistance"));
        const auto polarity = bidirectional ? QString("1") : "V(" + n[0] + "," + n[2] + ")>0";
        lines << "Bset desired 0 V=((" + polarity + ") && (" + gate + ">" + number(parameter(p, "trigger")) +
                     " || (V(state)>.5 && " + current + ">" + number(p.value) + "))) ? 1 : 0";
        r("state", "desired", "state", 1000);
        lines << "Cstate state 0 " + number(parameter(p, "response") / 1000) + " IC=0";
        lines << "Smain anode " + n[2] + " state 0 thy_sw"
              << ".model thy_sw SW(Ron=" + number(parameter(p, "ron")) + " Roff=1e12 Vt=.5 Vh=0)";
    }
    else
        lines << analogDeviceNetlist(p, n) << displayNetlist(p, n);
    const auto model = "__cmp_part_" + id;
    QStringList wrapped{".subckt " + model + " " + n.join(' ')};
    wrapped << lines << ".ends " + model << "X__cmp_" + id + " " + external.join(' ') + " " + model;
    return wrapped;
}
} // namespace cmp
