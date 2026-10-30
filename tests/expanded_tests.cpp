#include "cmp/ComponentLibrary.h"
#include "cmp/ElectricalDiagnostics.h"
#include "cmp/ExpandedLibrary.h"
#include "cmp/Recovery.h"
#include "cmp/Simulator.h"
#include <QtWidgets>
#include <cmath>
#include <iostream>
using namespace cmp;
static Circuit series(QString kind, double supply = 5, double resistance = 1000)
{
    Circuit c;
    c.operatingPoint = true;
    c.parts = {{"V1", "V", supply, 0, {}},
               {"R1", "R", resistance, 0, {}},
               {"X1", kind, spec(kind).value, 0, {}},
               {"G1", "GND", 0, 0, {}}};
    c.wires = {
        {{"V1", 0}, {"R1", 0}}, {{"R1", 1}, {"X1", 0}}, {{"X1", 1}, {"G1", 0}}, {{"V1", 1}, {"G1", 0}}};
    c.part("R1")->ratings["power"] = 1;
    return c;
}
int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    int failed = 0;
    auto check = [&](QString name, bool pass)
    {
        std::cout << (pass ? "PASS " : "FAIL ") << name.toStdString() << "\n";
        failed += !pass;
    };
    Simulator engine;
    Samples samples;
    QStringList messages;
    QObject::connect(&engine, &Simulator::samplesReady, &app, [&](Samples batch) { samples += batch; });
    QObject::connect(&engine, &Simulator::message, &app, [&](QString s) { messages << s; });
    auto run = [&](const Circuit &c)
    {
        samples.clear();
        messages.clear();
        bool ok = false;
        QEventLoop loop;
        QTimer timer;
        timer.setSingleShot(true);
        auto connection = QObject::connect(&engine, &Simulator::completed, &loop,
                                           [&](bool result)
                                           {
                                               ok = result;
                                               loop.quit();
                                           });
        QObject::connect(&timer, &QTimer::timeout, &loop,
                         [&]
                         {
                             engine.stop();
                             loop.quit();
                         });
        timer.start(10000);
        engine.start(c.netlist(), false);
        loop.exec();
        QObject::disconnect(connection);
        if (!ok)
            std::cerr << messages.join("\n").toStdString() << "\n";
        return ok;
    };
    auto voltage = [&](const Circuit &c, QString id, int a = 0, int b = 1)
    {
        const auto map = c.nodes();
        auto node = [&](int pin)
        {
            auto name = map.value(Pin{id, pin}.key());
            return name == "0" ? 0. : samples.back().values.value(name, -1e20);
        };
        return samples.isEmpty() ? -1e20 : node(a) - node(b);
    };
    auto current = [&](QString id, int pin = 0)
    {
        return samples.isEmpty()
                   ? -1e20
                   : samples.back().values.value(
                         "v__cmp_" + id.toLower() + "_" + QString::number(pin) + "#branch", -1e20);
    };
    auto hasIssue = [](const ElectricalReport &r, QString part, QString rule)
    {
        for (const auto &i : r.issues)
            if (i.component == part && i.rule.contains(rule))
                return true;
        return false;
    };
    bool definitions = true, icons = true;
    Circuit persisted;
    for (const auto &s : catalog())
        if (expanded(s.kind))
        {
            Part p;
            p.id = "P" + QString::number(persisted.parts.size() + 1);
            p.kind = s.kind;
            p.value = s.value;
            p.rotation = 90;
            p.position = {double(persisted.parts.size() * 100), 0};
            definitions &= !s.category.isEmpty() && !s.behavior.isEmpty() && !s.model.isEmpty() &&
                           validateExpanded(p).isEmpty();
            icons &= !expandedIcon(s).isNull();
            persisted.parts << p;
        }
    Circuit copy;
    QString error;
    check("expanded definitions have metadata and valid defaults", definitions);
    check("all expanded native icons render", icons);
    check("all new kinds preserve parameters and geometry",
          Circuit::parse(persisted.json(), copy, error) && copy.json() == persisted.json());
    auto c = series("POT");
    c.wires = {{{"V1", 0}, {"X1", 0}},
               {{"X1", 2}, {"G1", 0}},
               {{"X1", 1}, {"R1", 0}},
               {{"R1", 1}, {"G1", 0}},
               {{"V1", 1}, {"G1", 0}}};
    c.part("R1")->value = 1e12;
    c.part("X1")->ratings["power"] = 1;
    bool divider = true;
    for (double position : {.25, .5, .75, 0., 1.})
    {
        c.part("X1")->parameters["wiper"] = position;
        divider &= run(c) && std::abs(voltage(c, "X1", 1, 2) - 5 * position) < .05;
    }
    check("potentiometer divider fractions and endpoint convergence", divider);
    c.part("X1")->parameters["wiper"] = .5;
    c.part("R1")->value = 1000;
    double lower = 5000. * 1000 / 6000;
    check("loaded potentiometer matches resistor equivalent",
          run(c) && std::abs(voltage(c, "X1", 1, 2) - 5 * lower / (5000 + lower)) < .01);
    c = series("ZENER", -6, 150);
    c.part("X1")->ratings["power"] = .5;
    check("zener reverse breakdown at stated test current region",
          run(c) && voltage(c, "X1") < -5 && voltage(c, "X1") > -5.3);
    c.part("V1")->value = -3;
    check("zener below-knee does not impose nominal voltage", run(c) && std::abs(voltage(c, "X1") + 3) < .01);
    c.part("V1")->value = -12;
    c.part("X1")->ratings["power"] = .01;
    check("zener over-power identifies component",
          run(c) && hasIssue(diagnoseElectrical(c, samples), "X1", "Zener dissipation"));
    auto diodeAtCurrent = [&](QString kind)
    {
        auto c = series(kind);
        c.parts.erase(
            std::remove_if(c.parts.begin(), c.parts.end(), [](const Part &p) { return p.id == "R1"; }),
            c.parts.end());
        c.part("V1")->kind = "I";
        c.part("V1")->value = -.01;
        c.wires = {{{"V1", 0}, {"X1", 0}}, {{"V1", 1}, {"G1", 0}}, {{"X1", 1}, {"G1", 0}}};
        return run(c) ? voltage(c, "X1") : -1.;
    };
    double schottky = diodeAtCurrent("SCHOTTKY"), silicon = diodeAtCurrent("D");
    check("Schottky distinct forward curve at same 10 mA",
          schottky > .1 && schottky < .6 && silicon - schottky > .15);
    c = series("L");
    c.operatingPoint = false;
    c.duration = .0001;
    c.step = 1e-7;
    c.part("X1")->ratings = {{"current", 1}, {"power", 1}};
    bool rl = run(c);
    double maximumError = 0;
    for (const auto &s : samples)
    {
        double expected = 5. / 1001 * (1 - std::exp(-s.time * 1001 / .01));
        maximumError = qMax(maximumError, std::abs(s.values.value("v__cmp_x1_0#branch") - expected));
    }
    check("RL step follows analytic response", rl && maximumError < .00005);
    c = series("SW");
    c.part("X1")->parameters["state"] = 0;
    bool open = run(c) && current("X1") < 1e-8;
    c.part("X1")->parameters["state"] = 1;
    check("toggle open and closed electrical states",
          open && run(c) && std::abs(current("X1") - 5 / 1000.01) < 1e-6);
    c.operatingPoint = false;
    c.duration = .3;
    c.step = .0005;
    c.part("X1")->parameters = {{"scheduled", 1}, {"state", 0}, {"on", .1}, {"off", .2}};
    bool scheduled = run(c), during = false, after = false;
    for (const auto &s : samples)
    {
        double i = s.values.value("v__cmp_x1_0#branch");
        if (s.time > .11 && s.time < .19)
            during |= i > .004;
        if (s.time > .21)
            after |= std::abs(i) < 1e-8;
    }
    check("scheduled switch replays close/open pair", scheduled && during && after);
    c.parts = {{"VCC", "V", 5, 0, {}},   {"BASE", "V", 0, 0, {}}, {"RB", "R", 10000, 0, {}},
               {"RL", "R", 1000, 0, {}}, {"Q1", "PNP", 0, 0, {}}, {"G1", "GND", 0, 0, {}}};
    c.wires = {{{"VCC", 0}, {"Q1", 2}}, {{"BASE", 0}, {"RB", 0}}, {{"RB", 1}, {"Q1", 1}},
               {{"Q1", 0}, {"RL", 0}},  {{"RL", 1}, {"G1", 0}},   {{"VCC", 1}, {"G1", 0}},
               {{"BASE", 1}, {"G1", 0}}};
    c.operatingPoint = true;
    bool pnp = run(c) && voltage(c, "RL") > 4.5 && current("Q1") < 0;
    c.part("BASE")->value = 5;
    check("PNP high-side switch and current polarity", pnp && run(c) && voltage(c, "RL") < .5);
    c = series("RHEO");
    c.part("V1")->kind = "SINE";
    c.part("X1")->value = 1000;
    c.operatingPoint = false;
    c.duration = .04;
    c.step = .00002;
    bool sine = run(c);
    double peak = -1, trough = 1;
    const auto map = c.nodes();
    const auto input = map.value(Pin{"V1", 0}.key());
    for (const auto &s : samples)
    {
        peak = qMax(peak, s.values.value(input));
        trough = qMin(trough, s.values.value(input));
    }
    check("sine source produces configured peak amplitude",
          sine && std::abs(peak - 5) < .05 && std::abs(trough + 5) < .05);
    c = series("RECT");
    check("rectifier conducts in forward direction", run(c) && current("X1") > .003 && voltage(c, "X1") > .4);
    c.part("V1")->value = -5;
    check("rectifier blocks reverse low-voltage input", run(c) && std::abs(current("X1")) < 1e-5);
    c = series("TVS", 20, 100);
    c.part("X1")->parameters["bidirectional"] = 1;
    bool tvs = run(c) && voltage(c, "X1") > 11 && voltage(c, "X1") < 15;
    c.part("V1")->value = -20;
    check("bidirectional TVS clamps both polarities",
          tvs && run(c) && voltage(c, "X1") < -11 && voltage(c, "X1") > -15);
    c = series("BRIDGE");
    c.part("V1")->kind = "SINE";
    c.wires = {{{"V1", 0}, {"X1", 0}},
               {{"V1", 1}, {"X1", 1}},
               {{"X1", 2}, {"R1", 0}},
               {{"X1", 3}, {"R1", 1}},
               {{"V1", 1}, {"G1", 0}}};
    c.operatingPoint = false;
    c.duration = .04;
    c.step = .00002;
    bool bridge = run(c);
    double first = 0, second = 0, minimum = 1e9;
    const auto nodes = c.nodes();
    for (const auto &s : samples)
    {
        double a = s.values.value(nodes.value(Pin{"X1", 2}.key())),
               b = s.values.value(nodes.value(Pin{"X1", 3}.key()));
        double output = a - b;
        minimum = qMin(minimum, output);
        if (s.time > .004 && s.time < .006)
            first = qMax(first, output);
        if (s.time > .014 && s.time < .016)
            second = qMax(second, output);
    }
    check("bridge rectifies both AC half cycles",
          bridge && minimum > -.01 && first > 3 && second > 3 && std::abs(first - second) < .05);
    c = series("RNET_B");
    c.wires = {{{"V1", 0}, {"X1", 0}}, {{"V1", 1}, {"G1", 0}}, {{"X1", 1}, {"G1", 0}},
               {{"X1", 2}, {"G1", 0}}, {{"X1", 3}, {"G1", 0}}, {{"X1", 4}, {"G1", 0}},
               {{"R1", 0}, {"G1", 0}}, {{"R1", 1}, {"G1", 0}}};
    check("bussed resistor array has four independent branches",
          run(c) && std::abs(current("X1") - .02) < 1e-6);
    c = series("ESRC");
    c.operatingPoint = false;
    c.duration = .02;
    c.step = .00001;
    c.part("X1")->initial = 0;
    check("ESR and leakage capacitor converges with preserved state", run(c) && voltage(c, "X1") > 4);
    c = series("FUSE", 5, 1);
    check("diagnostic fuse warns without secretly burning open",
          run(c) && current("X1") > 1 && hasIssue(diagnoseElectrical(c, samples), "X1", "fuse current"));
    c = series("RPOWER");
    c.part("X1")->ratings["power"] = 1;
    run(c);
    auto missing = samples;
    for (auto &s : missing)
        s.values.remove("v__cmp_x1_0#branch");
    check("missing total-power current vector remains unchecked",
          diagnoseElectrical(c, missing).incomplete.join(" ").contains("missing/nonfinite"));
    double parsed;
    check("unit-aware SI parsing rejects wrong dimensions",
          parseQuantityUnit("10mH", "H", parsed) && std::abs(parsed - .01) < 1e-12 &&
              !parseQuantityUnit("10mA", "H", parsed) && parseQuantityUnit("1ms", "s", parsed) &&
              std::abs(parsed - .001) < 1e-12);
    ComponentLibrary library;
    library.setViewMode(QListView::IconMode);
    library.setWrapping(true);
    library.setResizeMode(QListView::Fixed);
    library.resize(340, 500);
    for (QString category : {"Passives", "Diodes & protection"})
    {
        library.addCategory(category);
        for (const auto &d : catalog())
            if (componentCategory(d.kind) == category)
            {
                auto *item = new QListWidgetItem(d.name, &library);
                item->setData(Qt::UserRole, d.kind);
                item->setData(ComponentLibrary::CategoryRole, category);
            }
    }
    library.show();
    QApplication::processEvents();
    library.arrange();
    library.filterComponents("Zener");
    int visibleParts = 0, visibleHeaders = 0;
    for (int i = 0; i < library.count(); ++i)
        if (!library.item(i)->isHidden())
            library.item(i)->data(ComponentLibrary::HeaderRole).toBool() ? ++visibleHeaders : ++visibleParts;
    check("category search shows matching components and their section only",
          visibleParts == 1 && visibleHeaders == 1);
    library.filterComponents("");
    auto *header = library.item(0);
    check("category headers are full-width non-components",
          header->data(ComponentLibrary::HeaderRole).toBool() &&
              header->data(Qt::UserRole).toString().isEmpty() &&
              library.visualItemRect(header).width() > library.viewport()->width() - 8);
    int activated = 0;
    QObject::connect(&library, &ComponentLibrary::componentClicked, &app, [&](QString) { ++activated; });
    QPoint point = library.visualItemRect(header).center();
    QMouseEvent press(QEvent::MouseButtonPress, point, library.viewport()->mapToGlobal(point), Qt::LeftButton,
                      Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(library.viewport(), &press);
    check("category header collapses without entering placement",
          header->data(ComponentLibrary::CollapsedRole).toBool() && activated == 0);
    return failed ? 1 : 0;
}
