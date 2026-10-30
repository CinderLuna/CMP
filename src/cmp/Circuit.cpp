#include "cmp/ExecutionPolicy.h"
#include "cmp/Circuit.h"
#include "cmp/DigitalDevices.h"
#include "cmp/ElectricalDiagnostics.h"
#include "cmp/ExpandedLibrary.h"
#include <QFile>
#include <QSaveFile>
#include <cmath>
static void initModels()
{
    Q_INIT_RESOURCE(cmp_models);
}
namespace cmp
{
const QVector<PartSpec> &catalog()
{
    static const QVector<PartSpec> c = []
    {
        QVector<PartSpec> c = {{"R", "Resistor", "R", {"1", "2"}, 1000, 0},
                               {"C", "Capacitor", "C", {"+", "−"}, 1e-7, 1},
                               {"CP", "Polarized capacitor", "C", {"+", "−"}, 1e-5, 2},
                               {"GND", "Ground", "G", {"0"}, 0, 3},
                               {"V", "DC source", "V", {"+", "−"}, 5, 4},
                               {"PULSE", "Pulse source", "V", {"+", "−"}, 5, 5},
                               {"D", "1N4148 diode", "D", {"A", "K"}, 0, 6},
                               {"LED", "Red LED", "D", {"A", "K"}, 0, 7},
                               {"NPN", "2N3904 NPN", "Q", {"C", "B", "E"}, 0, 8},
                               {"555",
                                "NE555 timer",
                                "U",
                                {"1 GND", "2 TRIG", "3 OUT", "4 RESET", "5 CTRL", "6 THR", "7 DIS", "8 VCC"},
                                0,
                                9},
                               {"LM339",
                                "LM339 comparator",
                                "U",
                                {"1 OUT1", "2 OUT2", "3 VCC", "4 IN2−", "5 IN2+", "6 IN1−", "7 IN1+",
                                 "8 IN3−", "9 IN3+", "10 IN4−", "11 IN4+", "12 GND", "13 OUT4", "14 OUT3"},
                                0,
                                -1,
                                {{0, 5, 6}, {1, 3, 4}, {13, 7, 8}, {12, 9, 10}},
                                "CMP_LM339",
                                "TI LM339N PDIP-14"}};
        appendExpandedCatalog(c);
        return c;
    }();
    return c;
}
const PartSpec &spec(const QString &kind)
{
    for (const auto &s : catalog())
        if (s.kind == kind)
            return s;
    return catalog().front();
}
QString componentCategory(const QString &kind)
{
    const auto &s = spec(kind);
    if (!s.category.isEmpty())
        return s.category;
    if (kind == "R" || kind == "C" || kind == "CP")
        return "Passives";
    if (kind == "V" || kind == "PULSE" || kind == "GND")
        return "Sources";
    if (kind == "D" || kind == "LED")
        return "Diodes & protection";
    if (kind == "NPN")
        return "Transistors";
    return "Analog ICs";
}
Part *Circuit::part(const QString &id)
{
    for (auto &p : parts)
        if (p.id == id)
            return &p;
    return nullptr;
}
const Part *Circuit::part(const QString &id) const
{
    for (const auto &p : parts)
        if (p.id == id)
            return &p;
    return nullptr;
}
static QJsonObject pinJson(const Pin &p)
{
    return {{"part", p.part}, {"pin", p.index}};
}
static Pin readPin(const QJsonObject &o)
{
    return {o["part"].toString(), o["pin"].toInt(-1)};
}
QJsonObject Circuit::json() const
{
    QJsonArray a, b;
    for (auto &p : parts)
    {
        QJsonObject ratings;
        for (auto it = p.ratings.begin(); it != p.ratings.end(); ++it)
            ratings[it.key()] = it.value();
        QJsonObject parameters;
        for (auto it = p.parameters.begin(); it != p.parameters.end(); ++it)
            parameters[it.key()] = it.value();
        QJsonObject object{{"id", p.id},
                           {"kind", p.kind},
                           {"value", p.value},
                           {"initial", p.initial},
                           {"x", p.position.x()},
                           {"y", p.position.y()},
                           {"rotation", p.rotation},
                           {"parameters", parameters},
                           {"ratings", ratings}};
        if (!p.data.isEmpty())
        {
            object["data"] = QString::fromLatin1(p.data.toBase64());
            object["dataType"] = p.dataType;
        }
        a.append(object);
    }
    for (auto &w : wires)
        b.append(QJsonObject{{"a", pinJson(w.a)}, {"b", pinJson(w.b)}});
    QJsonObject pr;
    for (auto it = probes.begin(); it != probes.end(); ++it)
        pr[it.key()] = pinJson(it.value());
    QJsonArray labelsJson;
    for (const auto &label : labels)
        labelsJson.append(QJsonObject{{"name", label.name}, {"pin", pinJson(label.pin)}});
    return {{"format", "CMP"},
            {"version", 1},
            {"modelSet", "CMP-functional-0.1"},
            {"parts", a},
            {"wires", b},
            {"probes", pr},
            {"labels", labelsJson},
            {"duration", duration},
            {"step", step},
            {"operatingPoint", operatingPoint}};
}
bool Circuit::parse(const QJsonObject &o, Circuit &result, QString &error)
{
    const QSet<QString> topFields = {"format", "version",  "modelSet", "parts",          "wires",
                                     "probes", "duration", "step",     "operatingPoint", "labels"};
    for (auto it = o.begin(); it != o.end(); ++it)
        if (!topFields.contains(it.key()))
        {
            error = "Project contains an unsupported field: " + it.key() + ". Nothing was changed.";
            return false;
        }
    if (o["format"] != "CMP" || o["version"].toInt() != 1 ||
        o["modelSet"].toString("CMP-functional-0.1") != "CMP-functional-0.1")
    {
        error = "Unsupported CMP project format/version/model set. This file was not changed.";
        return false;
    }
    if (!o["parts"].isArray() || !o["wires"].isArray() || !o["probes"].isObject() ||
        o["parts"].toArray().size() > 500 || o["wires"].toArray().size() > 3000 ||
        o["probes"].toObject().size() > 1024 || o["labels"].toArray().size() > 3000)
    {
        error = "Invalid or oversized project structure.";
        return false;
    }
    Circuit c;
    c.duration = o["duration"].toDouble(-1);
    c.step = o["step"].toDouble(-1);
    c.operatingPoint = o["operatingPoint"].toBool();
    QSet<QString> ids;
    for (auto a : o["parts"].toArray())
    {
        auto p = a.toObject();
        for (const auto &field : {"value", "initial", "x", "y", "rotation"})
            if (!p[field].isDouble())
            {
                error = "Component has a missing or non-numeric field.";
                return false;
            }
        if (p.contains("parameters") && !p["parameters"].isObject())
        {
            error = "Invalid component parameters.";
            return false;
        }
        const QSet<QString> fields = {"id",       "kind",       "value",   "initial", "x",       "y",
                                      "rotation", "parameters", "ratings", "data",    "dataType"};
        for (auto it = p.begin(); it != p.end(); ++it)
            if (!fields.contains(it.key()))
            {
                error = "Unsupported component field: " + it.key();
                return false;
            }
        Part v{p["id"].toString(),
               p["kind"].toString(),
               p["value"].toDouble(),
               p["initial"].toDouble(),
               {p["x"].toDouble(), p["y"].toDouble()},
               p["rotation"].toInt(),
               {}};
        bool known = false;
        for (const auto &s : catalog())
            known |= s.kind == v.kind;
        if (!known || !QRegularExpression("^[A-Za-z][A-Za-z0-9_]{0,31}$").match(v.id).hasMatch() ||
            ids.contains(v.id.toLower()) || !std::isfinite(v.value) || !std::isfinite(v.initial) ||
            !std::isfinite(v.position.x()) || !std::isfinite(v.position.y()) ||
            std::abs(v.position.x()) > 1e5 || std::abs(v.position.y()) > 1e5 || v.rotation % 90)
        {
            error = "Invalid component or duplicate reference.";
            return false;
        }
        if(!validateExecutionSpec(spec(v.kind),error))return false;
        auto params = p["parameters"].toObject();
        QSet<QString> validParameters = {"low", "delay", "rise", "fall", "width", "period"};
        if (expanded(v.kind))
            validParameters.clear();
        for (const auto &setting : spec(v.kind).settings)
            validParameters.insert(setting.key);
        for (auto it = params.begin(); it != params.end(); ++it)
        {
            if (!validParameters.contains(it.key()) || !it.value().isDouble() ||
                !std::isfinite(it.value().toDouble()))
            {
                error = "Invalid pulse parameter.";
                return false;
            }
            v.parameters[it.key()] = it.value().toDouble();
        }
        if (p.contains("ratings") && !p["ratings"].isObject())
        {
            error = "Invalid component ratings.";
            return false;
        }
        const auto ratingObject = p["ratings"].toObject();
        QSet<QString> allowedRatings;
        for (const auto &rating : ratingDefinitions(v.kind))
            allowedRatings.insert(rating.key);
        for (auto it = ratingObject.begin(); it != ratingObject.end(); ++it)
        {
            const double value = it.value().toDouble();
            if (!allowedRatings.contains(it.key()) || !it.value().isDouble() || !std::isfinite(value) ||
                value <= 0)
            {
                error = "Invalid component rating: " + it.key();
                return false;
            }
            v.ratings[it.key()] = value;
        }
        if (p.contains("data") || p.contains("dataType"))
        {
            if (!p["data"].isString() || !p["dataType"].isString())
            {
                error = "Invalid component payload.";
                return false;
            }
            const auto encoded = p["data"].toString().toLatin1();
            v.data = QByteArray::fromBase64(encoded);
            v.dataType = p["dataType"].toString();
            if (v.data.size() > 1024 * 1024 || v.data.toBase64() != encoded ||
                spec(v.kind).dataKind.isEmpty() || v.dataType != spec(v.kind).dataKind)
            {
                error = "Invalid component payload encoding/type.";
                return false;
            }
        }
        if (expanded(v.kind) && !validateExpanded(v).isEmpty())
        {
            error = validateExpanded(v).join("\n");
            return false;
        }
        ids.insert(v.id.toLower());
        c.parts.append(v);
    }
    auto valid = [&](Pin p)
    {
        auto *a = c.part(p.part);
        return a && p.index >= 0 && p.index < spec(a->kind).pins.size();
    };
    for (auto a : o["wires"].toArray())
    {
        auto w = a.toObject();
        for (auto it = w.begin(); it != w.end(); ++it)
            if (it.key() != "a" && it.key() != "b")
            {
                error = "Unsupported wire field.";
                return false;
            }
        Wire v{readPin(w["a"].toObject()), readPin(w["b"].toObject())};
        if (!valid(v.a) || !valid(v.b) || v.a == v.b)
        {
            error = "Wire references an invalid pin.";
            return false;
        }
        c.wires.append(v);
    }
    auto pr = o["probes"].toObject();
    for (auto it = pr.begin(); it != pr.end(); ++it)
    {
        auto p = readPin(it.value().toObject());
        if (!valid(p) || !QRegularExpression("^[A-Za-z][A-Za-z0-9_]{0,31}$").match(it.key()).hasMatch())
        {
            error = "Invalid voltage probe.";
            return false;
        }
        c.probes[it.key()] = p;
    }
    if (c.duration <= 0 || c.duration > 60 || c.step <= 0 || c.step > c.duration ||
        c.duration / c.step > 200000)
    {
        error = "Invalid analysis duration or maximum step (limit 200,000 nominal steps).";
        return false;
    }
    if (o.contains("labels") && !o["labels"].isArray())
    {
        error = "Invalid net labels.";
        return false;
    }
    for (auto v : o["labels"].toArray())
    {
        auto label = v.toObject();
        Pin pin = readPin(label["pin"].toObject());
        QString name = label["name"].toString();
        if (!valid(pin) || !QRegularExpression("^[A-Za-z][A-Za-z0-9_]{0,31}$").match(name).hasMatch())
        {
            error = "Invalid net label.";
            return false;
        }
        c.labels.append({name, pin});
    }
    result = c;
    return true;
}
QMap<QString, QString> Circuit::nodes() const
{
    QMap<QString, QString> parent;
    parent["0"] = "0";
    for (auto &p : parts)
        for (int i = 0; i < spec(p.kind).pins.size(); i++)
        {
            QString k = Pin{p.id, i}.key();
            parent[k] = k;
        }
    std::function<QString(QString)> root = [&](QString k)
    {
        while (parent.value(k, k) != k)
            k = parent[k];
        return k;
    };
    auto join = [&](QString a, QString b)
    {
        a = root(a);
        b = root(b);
        if (a != b)
            parent[a] = b;
    };
    for (auto &p : parts)
        if (p.kind == "GND")
            join(Pin{p.id, 0}.key(), "0");
    for (auto &w : wires)
        if (parent.contains(w.a.key()) && parent.contains(w.b.key()))
            join(w.a.key(), w.b.key());
    joinExpandedConnections(*this, join);
    QMap<QString, QString> labelled;
    for (const auto &label : labels)
    {
        QString name = label.name.toLower(), key = label.pin.key();
        if (!parent.contains(key))
            continue;
        if (labelled.contains(name))
            join(labelled[name], key);
        else
            labelled[name] = key;
    }
    QMap<QString, QString> names, result;
    names[root("0")] = "0";
    int i = 0;
    for (auto it = parent.begin(); it != parent.end(); ++it)
    {
        auto r = root(it.key());
        if (!names.contains(r))
            names[r] = "n" + QString::number(++i);
        result[it.key()] = names[r];
    }
    return result;
}
bool Circuit::hasExternalBias(const Pin &pin) const
{
    const auto map = nodes();
    QSet<QString> visited;
    QStringList pending{map.value(pin.key())};
    while (!pending.isEmpty())
    {
        const auto node = pending.takeLast();
        if (node == "0")
            return true;
        if (node.isEmpty() || visited.contains(node))
            continue;
        visited.insert(node);
        for (const auto &p : parts)
        {
            const auto a = map.value(Pin{p.id, 0}.key()), b = map.value(Pin{p.id, 1}.key());
            if (p.kind == "V" || p.kind == "PULSE" ||
                (componentCategory(p.kind) == "Sources" && expanded(p.kind) &&
                 spec(p.kind).behavior != "current" && spec(p.kind).behavior != "ipulse" &&
                 spec(p.kind).behavior != "rail"))
            {
                if (node == a || node == b)
                    return true;
            }
            QVector<QPair<int, int>> links;
            const auto behavior = spec(p.kind).behavior;
            if (p.kind == "R" || behavior == "resistor" || behavior == "ldr" || behavior == "ntc" ||
                behavior == "ptc")
                links << qMakePair(0, 1);
            if (behavior == "pot")
                links << qMakePair(0, 1) << qMakePair(1, 2);
            if (behavior == "rnet")
                for (int pin = 0; pin < 4; ++pin)
                    links << qMakePair(p.kind == "RNET_B" ? 0 : pin * 2,
                                       p.kind == "RNET_B" ? pin + 1 : pin * 2 + 1);
            for (const auto &link : links)
            {
                const auto first = map.value(Pin{p.id, link.first}.key()),
                           second = map.value(Pin{p.id, link.second}.key());
                if (node == first)
                    pending << second;
                if (node == second)
                    pending << first;
            }
        }
    }
    return false;
}
QStringList Circuit::validate() const
{
    QStringList errors;
    if (parts.isEmpty())
        errors << "Place components before running.";
    bool ground = false;
    for (auto &p : parts)
        ground |= p.kind == "GND";
    if (!ground)
        errors << "Missing ground: add a Ground component and wire it to the circuit reference.";
    if (duration <= 0 || duration > 60 || step <= 0 || step > duration || duration / step > 200000)
        errors << "Analysis settings exceed supported duration/step limits.";
    auto n = nodes();
    QMap<QString, int> populations;
    for (const auto &node : n)
        ++populations[node];
    QSet<QString> connected;
    for (auto &w : wires)
    {
        connected.insert(w.a.key());
        connected.insert(w.b.key());
    }
    for (auto it = n.begin(); it != n.end(); ++it)
        if (populations[it.value()] > 1)
            connected.insert(it.key());
    QMap<int, QVector<QString>> mating;
    for (const auto &p : parts)
        if ((p.kind == "JPLUG" || p.kind == "JSOCKET") && parameter(p, "mated"))
            mating[int(parameter(p, "link"))] << p.kind;
    for (const auto &items : mating)
        if (items.size() != 2 || items[0] == items[1])
            errors << "Mated connector identity requires exactly one plug and one socket.";
    QMap<QString, int> labelCounts;
    for (const auto &label : labels)
        ++labelCounts[label.name.toLower()];
    for (const auto &label : labels)
        if (labelCounts[label.name.toLower()] > 1)
            connected.insert(label.pin.key());
    for (auto &p : parts)
    {
        if (expanded(p.kind))
            errors << validateExpanded(p);
        if ((p.kind == "UNO" || p.kind == "CPU8085") && p.data.isEmpty())
            errors << p.id + ": import firmware before running.";
        if ((p.kind == "UNO" || p.kind == "CPU8085") && operatingPoint)
            errors << p.id + ": firmware requires transient analysis.";
        if ((p.kind == "R" || p.kind == "C" || p.kind == "CP") && (!std::isfinite(p.value) || p.value <= 0))
            errors << p.id + ": value must be finite and greater than zero.";
        if (!std::isfinite(p.value) || !std::isfinite(p.initial))
            errors << p.id + ": non-finite parameter.";
        if (p.kind == "PULSE" &&
            (p.parameters.value("rise", 1e-6) <= 0 || p.parameters.value("fall", 1e-6) <= 0 ||
             p.parameters.value("delay", .2) < 0 || p.parameters.value("width", .5) <= 0 ||
             p.parameters.value("period", 1) < p.parameters.value("width", .5) +
                                                   p.parameters.value("rise", 1e-6) +
                                                   p.parameters.value("fall", 1e-6)))
            errors << p.id + ": pulse period must contain positive rise, width and fall times; delay must be "
                             "non-negative.";
        for (int j = 0; j < spec(p.kind).pins.size(); j++)
        {
            const auto behavior = spec(p.kind).behavior;
            bool optionalOutput = (digitalDevice(p.kind) && j != digitalLayout(p.kind).supply &&
                                   j != digitalLayout(p.kind).ground) ||
                                  (p.kind == "UNO" && j != 20 && j != 21) || behavior == "board" ||
                                  behavior == "connector" || behavior == "rail" || behavior == "testpoint" ||
                                  behavior == "switch";
            for (const auto &ch : spec(p.kind).comparators)
                optionalOutput |= ch.output == j;
            if (!optionalOutput && !connected.contains(Pin{p.id, j}.key()))
                errors << p.id + ": unconnected pin " + spec(p.kind).pins[j] + ".";
        }
        if ((p.kind == "V" || p.kind == "PULSE") &&
            n.value(Pin{p.id, 0}.key()) == n.value(Pin{p.id, 1}.key()))
            errors << p.id + ": voltage source terminals are shorted.";
    }
    return errors;
}
QString Circuit::netlist() const
{
    initModels();
    QStringList lines = {"CMP circuit", ".options method=gear reltol=1e-4 abstol=1e-10 temp=27"};
    auto map = nodes();
    auto n = [&](QString id, int p) { return map.value(Pin{id, p}.key(), "0"); };
    for (auto &p : parts)
    {
        QString a = n(p.id, 0), b = n(p.id, 1), v = QString::number(p.value, 'g', 14), r = p.id.toLower();
        if (p.kind == "GND")
            continue;
        if (expanded(p.kind))
        {
            QStringList pins;
            for (int index = 0; index < spec(p.kind).pins.size(); ++index)
            {
                if (!measuresPins(p.kind))
                {
                    pins << n(p.id, index);
                    continue;
                }
                const auto node = "px_" + r + "_" + QString::number(index);
                lines << "V__cmp_" + r + "_" + QString::number(index) + " " + n(p.id, index) + " " + node +
                             " 0";
                pins << node;
            }
            lines << expandedNetlist(p, pins);
        }
        else if (p.kind == "V")
            lines << "V" + r + " " + a + " " + b + " " + v;
        else if (p.kind == "PULSE")
        {
            QStringList args = {QString::number(p.parameters.value("low", 0), 'g', 14), v};
            for (auto pair : QVector<QPair<QString, double>>{
                     {"delay", .2}, {"rise", 1e-6}, {"fall", 1e-6}, {"width", .5}, {"period", 1}})
                args << QString::number(p.parameters.value(pair.first, pair.second), 'g', 14);
            lines << "V" + r + " " + a + " " + b + " PULSE(" + args.join(' ') + ")";
        }
        else if (p.kind == "NPN")
        {
            lines << "Vic_" + r + " " + a + " measure_c_" + r + " 0";
            lines << "Vib_" + r + " " + b + " measure_b_" + r + " 0";
            lines << "Q" + r + " measure_c_" + r + " measure_b_" + r + " " + n(p.id, 2) + " CMP_NPN";
        }
        else if (!spec(p.kind).comparators.isEmpty())
        {
            QStringList pins;
            for (int i = 0; i < spec(p.kind).pins.size(); ++i)
            {
                bool output = false;
                for (const auto &ch : spec(p.kind).comparators)
                    output |= ch.output == i;
                if (output)
                {
                    const auto internal = "measure_oc_" + r + "_" + QString::number(i + 1);
                    lines << "Voc_" + r + "_" + QString::number(i + 1) + " " + n(p.id, i) + " " + internal +
                                 " 0";
                    pins << internal;
                }
                else
                    pins << n(p.id, i);
            }
            lines << "X" + r + " " + pins.join(' ') + " " + spec(p.kind).model;
        }
        else if (p.kind == "555")
        {
            QStringList pins;
            for (int i = 0; i < 8; i++)
                pins << (i == 2 ? "measure_out_" + r : n(p.id, i));
            lines << "Vio_" + r + " measure_out_" + r + " " + n(p.id, 2) + " 0";
            lines << "X" + r + " " + pins.join(' ') + " CMP_555";
        }
        else
        {
            QString internal = "measure_" + r;
            lines << "Vi_" + r + " " + a + " " + internal + " 0";
            if (p.kind == "R")
                lines << "R" + r + " " + internal + " " + b + " " + v;
            else if (p.kind == "C" || p.kind == "CP")
                lines << "C" + r + " " + internal + " " + b + " " + v +
                             " IC=" + QString::number(p.initial, 'g', 14);
            else
                lines << "D" + r + " " + internal + " " + b + " " + (p.kind == "LED" ? "CMP_LED" : "CMP_D");
        }
    }
    QFile models(":/cmp/models.lib");
    models.open(QIODevice::ReadOnly);
    lines << QString::fromUtf8(models.readAll());
    QSet<QString> saved;
    for (auto node : map)
        if (node != "0")
            saved.insert("v(" + node + ")");
    for (auto &p : parts)
        if (!expanded(p.kind) && p.kind != "555" && p.kind != "NPN" && p.kind != "GND" &&
            spec(p.kind).comparators.isEmpty())
            saved.insert(
                "i(" +
                ((p.kind == "V" || p.kind == "PULSE") ? "v" + p.id.toLower() : "vi_" + p.id.toLower()) + ")");
    for (const auto &p : parts)
    {
        const auto r = p.id.toLower();
        for (const auto &name : internalMeasurements(p))
            saved.insert(internalKey(p, name).endsWith("#branch")
                             ? "i(" + internalKey(p, name).chopped(7) + ")"
                             : "v(" + internalKey(p, name) + ")");
        if (expanded(p.kind) && measuresPins(p.kind))
            for (int index = 0; index < spec(p.kind).pins.size(); ++index)
                saved.insert("i(v__cmp_" + r + "_" + QString::number(index) + ")");
        for (const auto &ch : (expanded(p.kind) ? QVector<ComparatorChannel>{} : spec(p.kind).comparators))
            saved.insert("i(voc_" + r + "_" + QString::number(ch.output + 1) + ")");
        if (p.kind == "NPN")
            saved.insert("i(vic_" + r + ")");
        if (p.kind == "NPN")
            saved.insert("i(vib_" + r + ")");
        if (p.kind == "555")
            saved.insert("i(vio_" + r + ")");
    }
    auto saves = saved.values();
    saves.sort();
    lines << ".save " + saves.join(' ');
    if (operatingPoint)
        lines << ".op";
    else
    {
        double maximumStep = step;
        for (const auto &p : parts)
            if (p.kind == "UNO" || digitalDevice(p.kind))
            {
                maximumStep = qMin(maximumStep, parameter(p, "quantum"));
                if (p.kind == "CLOCK")
                    maximumStep = qMin(maximumStep, 1 / (16 * parameter(p, "frequency")));
            }
        lines << QString(".tran %1 %2 0 %3 uic")
                     .arg(step, 0, 'g', 14)
                     .arg(duration, 0, 'g', 14)
                     .arg(maximumStep, 0, 'g', 14);
    }
    lines << ".end";
    return lines.join('\n');
}
Circuit Circuit::blinker()
{
    Circuit c;
    c.parts = {{"V1", "V", 5, 0, {180, 80}},      {"RA", "R", 10000, 0, {230, 220}},
               {"RB", "R", 68000, 0, {230, 370}}, {"C1", "CP", 1e-5, 0, {340, 490}},
               {"CC", "C", 1e-8, 0, {650, 470}},  {"CB", "C", 1e-7, 0, {80, 340}},
               {"U1", "555", 0, 0, {500, 300}},   {"RL", "R", 1000, 0, {810, 250}},
               {"D1", "LED", 0, 0, {810, 420}},   {"G1", "GND", 0, 0, {500, 610}}};
    auto net = [&](std::initializer_list<Pin> pins)
    {
        auto it = pins.begin();
        Pin first = *it++;
        for (; it != pins.end(); ++it)
            c.wires.append({first, *it});
    };
    net({{"V1", 0}, {"RA", 0}, {"U1", 7}, {"U1", 3}, {"CB", 0}});
    net({{"RA", 1}, {"RB", 0}, {"U1", 6}});
    net({{"RB", 1}, {"C1", 0}, {"U1", 1}, {"U1", 5}});
    net({{"U1", 4}, {"CC", 0}});
    net({{"U1", 2}, {"RL", 0}});
    net({{"RL", 1}, {"D1", 0}});
    net({{"G1", 0}, {"V1", 1}, {"C1", 1}, {"CC", 1}, {"CB", 1}, {"U1", 0}, {"D1", 1}});
    c.probes = {{"out", {"U1", 2}}, {"timing", {"C1", 0}}, {"vcc", {"V1", 0}}};
    for (auto &p : c.parts)
        if (p.kind == "R")
            p.ratings["power"] = .25;
        else if (p.kind == "C" || p.kind == "CP")
            p.ratings["voltage"] = 25;
        else if (p.kind == "LED")
        {
            p.ratings["current"] = .02;
            p.ratings["reverse"] = 5;
        }
    return c;
}
Circuit Circuit::rc()
{
    Circuit c;
    c.duration = .5;
    c.step = .0001;
    c.parts = {{"V1", "V", 5, 0, {100, 100}},
               {"R1", "R", 10000, 0, {300, 180}},
               {"C1", "CP", 1e-5, 0, {450, 300}},
               {"G1", "GND", 0, 0, {200, 430}}};
    c.wires = {
        {{"V1", 0}, {"R1", 0}}, {{"R1", 1}, {"C1", 0}}, {{"C1", 1}, {"G1", 0}}, {{"V1", 1}, {"G1", 0}}};
    c.probes = {{"capacitor", {"C1", 0}}};
    for (auto &p : c.parts)
        if (p.kind == "R")
            p.ratings["power"] = .25;
        else if (p.kind == "C" || p.kind == "CP")
            p.ratings["voltage"] = 25;
        else if (p.kind == "LED")
        {
            p.ratings["current"] = .02;
            p.ratings["reverse"] = 5;
        }
    return c;
}
Circuit Circuit::transistor()
{
    Circuit c;
    c.duration = .01;
    c.step = .00001;
    c.parts = {{"V1", "V", 5, 0, {100, 100}},    {"V2", "V", 5, 0, {100, 300}},
               {"RC", "R", 1000, 0, {350, 120}}, {"RB", "R", 10000, 0, {250, 300}},
               {"Q1", "NPN", 0, 0, {480, 300}},  {"G1", "GND", 0, 0, {450, 500}}};
    c.wires = {{{"V1", 0}, {"RC", 0}}, {{"RC", 1}, {"Q1", 0}}, {{"V2", 0}, {"RB", 0}}, {{"RB", 1}, {"Q1", 1}},
               {{"Q1", 2}, {"G1", 0}}, {{"V1", 1}, {"G1", 0}}, {{"V2", 1}, {"G1", 0}}};
    c.probes = {{"collector", {"Q1", 0}}};
    for (auto &p : c.parts)
        if (p.kind == "R")
            p.ratings["power"] = .25;
    return c;
}
Circuit Circuit::comparator()
{
    Circuit c;
    c.duration = .02;
    c.step = .00001;
    c.parts = {{"VCC", "V", 5, 0, {80, 60}},      {"REF", "V", 2.5, 0, {80, 250}},
               {"IN", "PULSE", 3, 0, {80, 450}},  {"U1", "LM339", 0, 0, {430, 280}},
               {"R1", "R", 10000, 0, {720, 100}}, {"R2", "R", 10000, 0, {850, 100}},
               {"R3", "R", 10000, 0, {980, 100}}, {"R4", "R", 10000, 0, {1110, 100}},
               {"G1", "GND", 0, 0, {430, 570}}};
    c.part("IN")->parameters = {{"low", 2},       {"delay", .002}, {"rise", .00001},
                                {"fall", .00001}, {"width", .005}, {"period", .01}};
    auto connect = [&](Pin a, Pin b) { c.wires.append({a, b}); };
    connect({"VCC", 0}, {"U1", 2});
    connect({"U1", 11}, {"G1", 0});
    for (const auto &id : {QString("VCC"), QString("REF"), QString("IN")})
        connect({id, 1}, {"G1", 0});
    int index = 0;
    for (const auto &ch : spec("LM339").comparators)
    {
        const auto id = "R" + QString::number(++index);
        connect({id, 0}, {"VCC", 0});
        connect({id, 1}, {"U1", ch.output});
        connect({"U1", ch.negative}, {"REF", 0});
        connect({"U1", ch.positive}, {"IN", 0});
        c.part(id)->ratings["power"] = .25;
        c.probes["out" + QString::number(index)] = {"U1", ch.output};
    }
    c.probes["input"] = {"IN", 0};
    c.probes["reference"] = {"REF", 0};
    return c;
}
class Edit : public QUndoCommand
{
    Document *d;
    Circuit before, after;

  public:
    Edit(Document *doc, const Circuit &a, const Circuit &b, const QString &text)
        : QUndoCommand(text), d(doc), before(a), after(b)
    {
    }
    void undo() override
    {
        d->circuit = before;
        emit d->changed();
    }
    void redo() override
    {
        d->circuit = after;
        emit d->changed();
    }
};
void Document::edit(const QString &text, const std::function<void(Circuit &)> &fn)
{
    if (editsLocked)
        return;
    Circuit next = circuit;
    fn(next);
    if (next.json() != circuit.json())
        undo.push(new Edit(this, circuit, next, text));
}
void Document::replace(const Circuit &c, const QString &f)
{
    circuit = c;
    path = f;
    undo.clear();
    undo.setClean();
    emit changed();
}
bool Document::save(const QString &p, QString &error)
{
    QSaveFile file(p);
    if (!file.open(QIODevice::WriteOnly))
    {
        error = file.errorString();
        return false;
    }
    auto data = QJsonDocument(circuit.json()).toJson();
    if (file.write(data) != data.size() || !file.commit())
    {
        error = file.errorString();
        return false;
    }
    path = p;
    undo.setClean();
    return true;
}
bool Document::load(const QString &p, QString &error)
{
    QFile f(p);
    if (!f.open(QIODevice::ReadOnly))
    {
        error = f.errorString();
        return false;
    }
    if (f.size() > 8 * 1024 * 1024)
    {
        error = "Project exceeds 8 MB limit.";
        return false;
    }
    QJsonParseError e;
    auto json = QJsonDocument::fromJson(f.readAll(), &e);
    Circuit c;
    if (e.error != QJsonParseError::NoError || !json.isObject())
    {
        error = "Invalid JSON: " + e.errorString();
        return false;
    }
    if (!Circuit::parse(json.object(), c, error))
        return false;
    replace(c, p);
    return true;
}
QString quantity(double v, const QString &k)
{
    QString unit = !spec(k).unit.isEmpty()   ? spec(k).unit
                   : k == "R"                ? "Ω"
                   : (k == "C" || k == "CP") ? "F"
                   : k == "I"                ? "A"
                                             : "V";
    for (auto pair : QVector<QPair<double, QString>>{{1e12, "T"},
                                                     {1e9, "G"},
                                                     {1e6, "M"},
                                                     {1e3, "k"},
                                                     {1, ""},
                                                     {1e-3, "m"},
                                                     {1e-6, "µ"},
                                                     {1e-9, "n"},
                                                     {1e-12, "p"}})
        if (std::abs(v) >= pair.first * .999)
            return QString::number(v / pair.first, 'g', 6) + " " + pair.second + unit;
    return QString::number(v, 'g', 6) + " " + unit;
}
bool parseQuantityUnit(QString text, const QString &unit, double &value)
{
    text = text.trimmed();
    static QRegularExpression suffix("(degC|°C|Hz|Ah|ohm|deg|lux|rpm|[FVHAΩWJsKC%])$");
    const auto match = suffix.match(text);
    if (match.hasMatch())
    {
        const auto actual = match.captured() == "ohm" ? QString("Ω") : match.captured();
        if (unit.isEmpty() || actual != (unit == "ohm" ? QString("Ω") : unit))
            return false;
        text.chop(match.captured().size());
    }
    return parseQuantity(text, value);
}
bool parseQuantity(QString s, double &value)
{
    s = s.trimmed();
    static QRegularExpression re("^([+-]?(?:[0-9]+(?:\\.[0-9]*)?|\\.[0-9]+)(?:[eE][+-]?[0-9]+)?)\\s*(["
                                 "pnumkMµGT]?)\\s*(?:[FVHAΩ]|Hz|ohm)?$");
    auto m = re.match(s);
    if (!m.hasMatch())
        return false;
    bool ok;
    value = m.captured(1).toDouble(&ok);
    QMap<QString, double> scale = {{"p", 1e-12}, {"n", 1e-9}, {"u", 1e-6}, {"µ", 1e-6}, {"m", 1e-3},
                                   {"", 1},      {"k", 1e3},  {"M", 1e6},  {"G", 1e9},  {"T", 1e12}};
    value *= scale[m.captured(2)];
    return ok && std::isfinite(value);
}
} // namespace cmp
