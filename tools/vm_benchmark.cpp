#include "cmp/Circuit.h"
#include "cmp/Cpu8085.h"
#include "cmp/ExecutionPolicy.h"
#include "cmp/LogicVm.h"
#include "cmp/Simulator.h"
#include "cmp/Waveform.h"
#include <QApplication>
#include <QtCore>
#include <cmath>
#include <iostream>
using namespace cmp;
int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QJsonObject output;
    QJsonArray catalogAudit;
    for (const auto &part : catalog())
    {
        QString policyError;
        if (!validateExecutionSpec(part, policyError))
        {
            std::cerr << policyError.toStdString();
            return 10;
        }
        const auto *policy = executionPolicy(part.kind);
        const auto backend = executionBackendName(policy->backend);
        catalogAudit.append(QJsonObject{{"kind", part.kind},
                                        {"category", componentCategory(part.kind)},
                                        {"behavior", part.behavior},
                                        {"backend", backend}});
    }
    output["catalog"] = catalogAudit;
    QVector<LogicVm::Instruction> program;
    for (int i = 0; i < 1024; ++i)
        program.append(
            {LogicVm::Op(i % 9), quint32((i + 2) % 256), quint32(i % 256), quint32((i + 1) % 256)});
    QJsonObject gateResults;
    QByteArray reference;
    for (bool native : {false, true})
    {
        LogicVm vm;
        vm.enableNative(native);
        QString error;
        QElapsedTimer clock;
        clock.start();
        if (!vm.load(256, program, error))
            return 1;
        auto compile = clock.nsecsElapsed();
        for (int i = 0; i < 256; ++i)
            vm.set(i, i % 4);
        clock.restart();
        for (int i = 0; i < 10000; ++i)
        {
            vm.rewind();
            vm.run(1024);
        }
        auto elapsed = clock.nsecsElapsed();
        QByteArray state;
        for (int i = 0; i < 256; ++i)
            state.append(char(vm.get(i)));
        if (!native)
            reference = state;
        else if (reference != state)
            return 2;
        gateResults[native ? "native" : "interpreter"] =
            QJsonObject{{"load_ns", double(compile)},
                        {"run_ns", double(elapsed)},
                        {"operations", 10240000},
                        {"native_available", vm.hasNativeCode()}};
    }
    output["logic"] = gateResults;
    QJsonObject cpuResults;
    for (bool native : {false, true})
    {
        Cpu8085 cpu;
        cpu.enableNative(native);
        QByteArray rom;
        for (int i = 0; i < 128; ++i)
        {
            rom.append(char(0x3e));
            rom.append(char(i));
            rom.append(char(0x47));
        }
        rom += QByteArray::fromHex("c30000");
        QString error;
        cpu.load(rom, error);
        QElapsedTimer clock;
        clock.start();
        auto result = cpu.advanceTo(8000000, 4000000);
        cpuResults[native ? "native" : "interpreter"] =
            QJsonObject{{"run_including_compile_ns", double(clock.nsecsElapsed())},
                        {"cycles", double(result.reachedCycle)},
                        {"instructions", double(result.instructions)},
                        {"native_blocks", int(cpu.nativeBlocks())}};
    }
    output["cpu8085"] = cpuResults;
    QJsonObject scenarios;
    auto simulate = [&](const QString &name, const Circuit &c)
    {
        QJsonObject results;
        for (bool native : {false, true})
        {
            if (native)
                qunsetenv("CMP_VM_INTERPRETER");
            else
                qputenv("CMP_VM_INTERPRETER", "1");
            Simulator simulator;
            QEventLoop loop;
            bool success = false;
            QObject::connect(&simulator, &Simulator::completed, &loop,
                             [&](bool ok)
                             {
                                 success = ok;
                                 loop.quit();
                             });
            QTimer timeout;
            timeout.setSingleShot(true);
            QObject::connect(&timeout, &QTimer::timeout, &loop,
                             [&]
                             {
                                 simulator.stop();
                                 loop.quit();
                             });
            timeout.start(60000);
            simulator.start(c, false);
            loop.exec();
            auto metrics = simulator.statistics();
            metrics["success"] = success;
            results[native ? "native" : "interpreter"] = metrics;
            if (!success)
                return false;
        }
        scenarios[name] = results;
        return true;
    };
    auto rc = Circuit::rc();
    rc.duration = .05;
    if (!simulate("RC", rc) || !simulate("555", Circuit::blinker()))
        return 4;
    Circuit gates;
    gates.duration = .0001;
    gates.step = 1e-6;
    gates.parts = {{"V1", "V", 5, 0, {}}, {"G1", "GND", 0, 0, {}}, {"R1", "R", 1000, 0, {}}};
    gates.wires = {{{"V1", 1}, {"G1", 0}}};
    Pin previous{"V1", 0};
    for (int i = 0; i < 16; ++i)
    {
        auto id = "U" + QString::number(i + 1);
        gates.parts.append({id, "NOT", 0, 0, {}});
        gates.wires.append(Wire{previous, {id, 0}});
        gates.wires.append(Wire{{id, 2}, {"V1", 0}});
        gates.wires.append(Wire{{id, 3}, {"G1", 0}});
        previous = {id, 1};
    }
    gates.wires.append(Wire{previous, {"R1", 0}});
    gates.wires.append(Wire{{"R1", 1}, {"G1", 0}});
    if (!simulate("sixteen_gates", gates))
        return 5;
    Circuit uno;
    uno.duration = .0001;
    uno.step = 1e-6;
    uno.parts = {{"U1", "UNO", 16000000, 0, {}},
                 {"V1", "V", 5, 0, {}},
                 {"R1", "R", 1000, 0, {}},
                 {"G1", "GND", 0, 0, {}}};
    uno.part("U1")->data = QByteArray::fromHex("00e204b905b9ffcf");
    uno.part("U1")->dataType = "firmware";
    uno.wires = {{{"V1", 0}, {"U1", 20}}, {{"V1", 1}, {"G1", 0}}, {{"U1", 21}, {"G1", 0}},
                 {{"U1", 13}, {"R1", 0}}, {{"R1", 1}, {"G1", 0}}, {{"U1", 22}, {"V1", 0}}};
    if (!simulate("Uno", uno))
        return 6;
    auto second = *uno.part("U1");
    second.id = "U2";
    uno.parts.append(second);
    uno.parts.append({"R2", "R", 1000, 0, {}});
    uno.wires += {{{"V1", 0}, {"U2", 20}},
                  {{"U2", 21}, {"G1", 0}},
                  {{"U2", 13}, {"R2", 0}},
                  {{"R2", 1}, {"G1", 0}},
                  {{"U2", 22}, {"V1", 0}}};
    if (!simulate("two_Unos", uno))
        return 7;
    output["end_to_end"] = scenarios;
    Waveform waveform;
    waveform.resize(1200, 300);
    waveform.traces = {{"Signal", "signal", Qt::green, true, false}};
    Samples samples;
    for (int i = 0; i < 100000; ++i)
        samples.append({i * .00001, {{"signal", std::sin(i * .01)}}});
    QElapsedTimer clock;
    clock.start();
    waveform.append(samples);
    auto appendNs = clock.nsecsElapsed();
    QImage image(1600, 400, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::black);
    clock.restart();
    waveform.render(&image);
    auto firstPaint = clock.nsecsElapsed();
    clock.restart();
    for (int i = 0; i < 100; ++i)
    {
        waveform.resize(800 + i * 4, 300);
        waveform.render(&image);
    }
    output["waveform"] = QJsonObject{{"samples", 100000},
                                     {"append_ns", double(appendNs)},
                                     {"first_render_ns", double(firstPaint)},
                                     {"hundred_resize_renders_ns", double(clock.nsecsElapsed())}};
    output["note"] =
        "Single-run development measurements, not release performance guarantees. Debug build; timings vary.";
    const auto json = QJsonDocument(output).toJson(QJsonDocument::Indented);
    if (app.arguments().size() > 1)
    {
        QFile file(app.arguments()[1]);
        if (!file.open(QIODevice::WriteOnly) || file.write(json) != json.size())
            return 3;
    }
    else
        std::cout << json.toStdString();
    return 0;
}
