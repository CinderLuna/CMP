#include "cmp/Circuit.h"
#include "cmp/ExpandedLibrary.h"
#include "cmp/Simulator.h"
#include <QtCore>
#include <iostream>
using namespace cmp;
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    int failures = 0;
    auto check = [&](const char *name, bool ok)
    {
        std::cout << (ok ? "PASS " : "FAIL ") << name << "\n";
        failures += !ok;
    };
    Simulator engine;
    Samples samples;
    QStringList messages;
    QObject::connect(&engine, &Simulator::samplesReady, &app, [&](Samples s) { samples += s; });
    QObject::connect(&engine, &Simulator::message, &app, [&](QString s) { messages << s; });
    auto run = [&](const Circuit &c)
    {
        samples.clear();
        messages.clear();
        bool ok = false;
        QEventLoop loop;
        QTimer watchdog;
        watchdog.setSingleShot(true);
        auto connection = QObject::connect(&engine, &Simulator::completed, &loop,
                                           [&](bool value)
                                           {
                                               ok = value;
                                               loop.quit();
                                           });
        QObject::connect(&watchdog, &QTimer::timeout, &loop,
                         [&]
                         {
                             engine.stop();
                             loop.quit();
                         });
        watchdog.start(10000);
        engine.start(c.netlist(), false);
        loop.exec();
        QObject::disconnect(connection);
        if (!ok)
            std::cerr << messages.join("\n").toStdString();
        return ok;
    };
    auto read = [&](const Circuit &c, QString id, int pin)
    {
        const auto node = c.nodes().value(Pin{id, pin}.key());
        return node == "0" ? 0. : samples.back().values.value(node, -1e9);
    };
    Circuit c;
    c.operatingPoint = true;
    c.parts = {{"V1", "V", 5, 0, {}},
               {"S1", "SPDT", 0, 0, {}},
               {"R1", "R", 1000, 0, {}},
               {"R2", "R", 1000, 0, {}},
               {"G1", "GND", 0, 0, {}}};
    c.wires = {{{"V1", 0}, {"S1", 0}}, {{"S1", 1}, {"R1", 0}}, {{"S1", 2}, {"R2", 0}},
               {{"R1", 1}, {"G1", 0}}, {{"R2", 1}, {"G1", 0}}, {{"V1", 1}, {"G1", 0}}};
    bool spdt = run(c) && read(c, "R1", 0) > 4.9 && read(c, "R2", 0) < .01;
    c.part("S1")->parameters["state"] = 1;
    check("SPDT transfers common to the selected contact",
          spdt && run(c) && read(c, "R1", 0) < .01 && read(c, "R2", 0) > 4.9);
    c.operatingPoint = false;
    c.duration = .003;
    c.step = 1e-6;
    c.part("S1")->parameters = {
        {"state", 0}, {"scheduled", 1}, {"on", .001}, {"off", .002}, {"deadtime", .0001}};
    bool dead = run(c), bothOpen = false, bothClosed = false;
    auto map = c.nodes();
    for (const auto &s : samples)
    {
        double a = s.values.value(map.value(Pin{"R1", 0}.key())),
               b = s.values.value(map.value(Pin{"R2", 0}.key()));
        if (s.time > .00101 && s.time < .00109)
            bothOpen |= a < .1 && b < .1;
        bothClosed |= a > 1 && b > 1;
    }
    check("SPDT enforces scheduled break-before-make", dead && bothOpen && !bothClosed);
    c = Circuit{};
    c.operatingPoint = true;
    c.parts = {{"S1", "DIP8", 0, 0, {}}, {"V1", "V", 5, 0, {}}, {"G1", "GND", 0, 0, {}}};
    c.wires.append(Wire{{"V1", 1}, {"G1", 0}});
    for (int i = 0; i < 8; ++i)
    {
        QString id = "R" + QString::number(i);
        c.parts.append({id, "R", 1000, 0, {}});
        c.wires.append(Wire{{"V1", 0}, {"S1", i * 2}});
        c.wires.append(Wire{{"S1", i * 2 + 1}, {id, 0}});
        c.wires.append(Wire{{id, 1}, {"G1", 0}});
        c.part("S1")->parameters["bit" + QString::number(i)] = i % 2;
    }
    bool bank = run(c);
    for (int i = 0; i < 8; ++i)
        bank &=
            (i % 2 ? read(c, "R" + QString::number(i), 0) > 4.9 : read(c, "R" + QString::number(i), 0) < .01);
    check("DIP bank switches remain independent", bank);
    Circuit topology;
    topology.parts = {{"A", "JPLUG", 0, 0, {}},
                      {"B", "JSOCKET", 0, 0, {}},
                      {"P1", "RAIL", 0, 0, {}},
                      {"P2", "RAIL", 0, 0, {}},
                      {"J1", "HEADER", 0, 0, {}}};
    auto nodes = topology.nodes();
    check("unmated connector pins remain independent",
          nodes.value(Pin{"A", 0}.key()) != nodes.value(Pin{"B", 0}.key()));
    topology.part("A")->parameters["mated"] = 1;
    topology.part("B")->parameters["mated"] = 1;
    nodes = topology.nodes();
    bool mating = true;
    for (int i = 0; i < 4; ++i)
        mating &= nodes.value(Pin{"A", i}.key()) == nodes.value(Pin{"B", i}.key());
    mating &= nodes.value(Pin{"A", 0}.key()) != nodes.value(Pin{"A", 1}.key());
    check("mated pair joins only corresponding numbered pins", mating);
    check("passive header never joins adjacent pins",
          nodes.value(Pin{"J1", 0}.key()) != nodes.value(Pin{"J1", 1}.key()));
    check("rail aliases connect without creating a source",
          nodes.value(Pin{"P1", 0}.key()) == nodes.value(Pin{"P2", 0}.key()) &&
              !topology.netlist().contains("Vrail"));
    topology.part("P2")->parameters["rail"] = 2;
    nodes = topology.nodes();
    check("different rail identities remain isolated",
          nodes.value(Pin{"P1", 0}.key()) != nodes.value(Pin{"P2", 0}.key()));
    topology.parts.append({"C", "JPLUG", 0, 0, {}});
    topology.part("C")->parameters["mated"] = 1;
    check("ambiguous mating identity is rejected",
          topology.validate().join(" ").contains("exactly one plug"));
    Circuit board;
    board.parts = {{"BB1", "BOARD", 0, 0, {}}, {"U1", "LM339", 0, 0, {0, 10}}};
    nodes = board.nodes();
    auto net = [&](QString id, int pin) { return nodes.value(Pin{id, pin}.key()); };
    check("breadboard left row five holes share connectivity", net("BB1", 0) == net("BB1", 4));
    check("breadboard trench and neighboring rows stay isolated",
          net("BB1", 4) != net("BB1", 5) && net("BB1", 0) != net("BB1", 10));
    check("breadboard power rails split at their center", net("BB1", 200) == net("BB1", 209) &&
                                                              net("BB1", 209) != net("BB1", 210) &&
                                                              net("BB1", 200) != net("BB1", 220));
    bool landed = true;
    for (int pin = 0; pin < 14; ++pin)
    {
        const auto lead = physicalLocalPin(*board.part("U1"), pin) + board.part("U1")->position;
        bool found = false;
        for (int hole = 0; hole < 280; ++hole)
            if (QLineF(lead, expandedPin(*board.part("BB1"), hole, true)).length() < .01)
                found |= net("U1", pin) == net("BB1", hole);
        landed &= found;
    }
    check("all DIP14 leads automatically join matching breadboard holes", landed);
    auto previous = net("U1", 0);
    board.part("U1")->position += QPointF(0, 5);
    nodes = board.nodes();
    check("moving a lead off a hole disconnects it",
          nodes.value(Pin{"U1", 0}.key()) != nodes.value(Pin{"BB1", 74}.key()));
    Part candidate = *board.part("U1");
    candidate.position = {0, 14};
    check("nearby lead placement snaps to a breadboard hole",
          QLineF(snappedToBoard(board, candidate), QPointF(0, 10)).length() < .01);
    Circuit copy;
    QString error;
    check("board and connector settings persist",
          Circuit::parse(board.json(), copy, error) && copy.json() == board.json());

    c={};c.operatingPoint=true;c.parts={{"V1","V",5,0,{1000,1000}},{"R1","R",1000,0,{1200,1200}},{"R2","R",1000,0,{1400,1400}},{"G1","GND",0,0,{1600,1600}},{"BB1","BOARD",0,0,{5000,5000}}};
    c.wires={{{"V1",0},{"BB1",0}},{{"BB1",4},{"R1",0}},{{"R1",1},{"R2",0}},{{"R2",1},{"G1",0}},{{"V1",1},{"G1",0}}};
    check("breadboard groups carry actual solver current in a divider",run(c)&&std::abs(read(c,"R2",0)-2.5)<.001);
    return failures ? 1 : 0;
}
