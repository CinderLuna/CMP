#include "cmp/ElectricalDiagnostics.h"
#include <iostream>
#include <limits>
using namespace cmp;
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    int failed = 0;
    auto check = [&](const char *name, bool pass)
    {
        std::cout << (pass ? "PASS " : "FAIL ") << name << "\n";
        failed += !pass;
    };
    Circuit c = Circuit::blinker();
    auto sample = [&](double t, double supply)
    {
        Sample s;
        s.time = t;
        for (const auto &node : c.nodes())
            if (node != "0")
                s.values[node] = 0;
        s.values[c.nodes().value(Pin{"V1", 0}.key())] = supply;
        for (const auto &p : c.parts)
        {
            if (p.kind == "R" || p.kind == "C" || p.kind == "CP" || p.kind == "LED" || p.kind == "D")
                s.values["vi_" + p.id.toLower() + "#branch"] = 0;
            if (p.kind == "555")
                s.values["vio_" + p.id.toLower() + "#branch"] = 0;
        }
        return s;
    };
    auto find = [](const ElectricalReport &r, QString id, QString rule) -> const ElectricalIssue *
    {
        for (const auto &i : r.issues)
            if (i.component == id && i.rule == rule)
                return &i;
        return nullptr;
    };
    auto normal = sample(0, 5);
    check("synthetic normal has full coverage", diagnoseElectrical(c, {normal}).issues.isEmpty() &&
                                                    diagnoseElectrical(c, {normal}).incomplete.isEmpty());
    auto stress = sample(.2, 500);
    stress.values["vi_ra#branch"] = .05;
    stress.values["vi_d1#branch"] = .1;
    stress.values[c.nodes().value(Pin{"C1", 0}.key())] = -2;
    auto peak = stress;
    peak.time = .3;
    peak.values["vi_d1#branch"] = .2;
    auto report = diagnoseElectrical(c, {normal, stress, peak});
    check("500 V supply rating",
          find(report, "U1", "Supply maximum") && find(report, "U1", "Supply maximum")->peak == 500);
    check("500 V operating range separate",
          find(report, "U1", "Supply above recommended range") &&
              find(report, "U1", "Supply above recommended range")->operatingRange);
    check("resistor power from measured current",
          find(report, "RA", "Resistor power") && find(report, "RA", "Resistor power")->peak == 25);
    check("capacitor overvoltage", find(report, "CB", "Capacitor voltage"));
    check("capacitor reverse bias", find(report, "C1", "Polarized capacitor reverse bias"));
    const auto *led = find(report, "D1", "Forward current");
    check("first timestamp and peak aggregate", led && led->firstTime == .2 && led->peak == .2);
    auto low = diagnoseElectrical(c, {sample(.5, 3)});
    check("undervoltage", find(low, "U1", "Supply below recommended range") &&
                              find(low, "U1", "Supply below recommended range")->peak == 3);
    auto range = diagnoseElectrical(c, {sample(0, 17)});
    check("17 V range only",
          find(range, "U1", "Supply above recommended range") && !find(range, "U1", "Supply maximum"));
    c.part("RA")->ratings.clear();
    check("missing generic rating not pass", !diagnoseElectrical(c, {normal}).incomplete.isEmpty());
    c.part("RA")->ratings["power"] = .25;
    auto missing = normal;
    missing.values.remove("vi_ra#branch");
    check("missing measurement not zero", !diagnoseElectrical(c, {missing}).incomplete.isEmpty());
    missing = normal;
    missing.values["vi_ra#branch"] = std::numeric_limits<double>::quiet_NaN();
    check("nonfinite measurement incomplete", !diagnoseElectrical(c, {missing}).incomplete.isEmpty());
    check("empty data incomplete", !diagnoseElectrical(c, {}).incomplete.isEmpty());
    check("failed solver never gets pass",
          !diagnoseElectrical(c, {normal}).lines(false).join(" ").contains("No violations observed"));
    Circuit restored;
    QString error;
    check("rating persistence",
          Circuit::parse(c.json(), restored, error) && restored.part("RA")->ratings.value("power") == .25);
    auto json = c.json();
    auto parts = json["parts"].toArray();
    auto p = parts[1].toObject();
    p["ratings"] = QJsonObject{{"power", -1}};
    parts[1] = p;
    json["parts"] = parts;
    check("invalid stored rating rejected", !Circuit::parse(json, restored, error));
    p["ratings"] = QJsonObject{{"unknown", 1}};
    parts[1] = p;
    json["parts"] = parts;
    check("unknown stored rating rejected", !Circuit::parse(json, restored, error));
    auto legacy = c.json();
    parts = legacy["parts"].toArray();
    for (int i = 0; i < parts.size(); ++i)
    {
        auto p = parts[i].toObject();
        p.remove("ratings");
        parts[i] = p;
    }
    legacy["parts"] = parts;
    check("legacy file loads with unspecified ratings",
          Circuit::parse(legacy, restored, error) && restored.part("RA")->ratings.isEmpty());
    Document doc;
    doc.edit("Rating", [](Circuit &c) { c.part("RA")->ratings["power"] = 1; });
    doc.undo.undo();
    check("rating undo", doc.circuit.part("RA")->ratings.value("power") == .25);
    doc.undo.redo();
    check("rating redo", doc.circuit.part("RA")->ratings.value("power") == 1);
    Circuit transistor = Circuit::transistor();
    Sample q;
    q.time = .7;
    for (auto n : transistor.nodes())
        if (n != "0")
            q.values[n] = 0;
    q.values[transistor.nodes().value(Pin{"Q1", 0}.key())] = 50;
    q.values["vic_q1#branch"] = .3;
    q.values["vib_q1#branch"] = .001;
    for (auto id : {"RC", "RB"})
        q.values["vi_" + QString(id).toLower() + "#branch"] = 0;
    auto qr = diagnoseElectrical(transistor, {q});
    check("transistor voltage limit", find(qr, "Q1", "Collector-emitter voltage"));
    check("transistor current limit", find(qr, "Q1", "Collector current"));
    check("transistor dissipation", find(qr, "Q1", "Transistor dissipation"));
    return failed ? 1 : 0;
}
