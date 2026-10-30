#include "cmp/Circuit.h"
#include "cmp/WorkerProtocol.h"
#include <iostream>
#include <random>
using namespace cmp;
int main()
{
    int failures = 0;
    auto check = [&](bool ok)
    {
        if (!ok)
            ++failures;
    };
    auto bytes = ipc::frame({{"type", "pause"}});
    QString error;
    for (int split = 0; split <= bytes.size(); ++split)
    {
        ipc::Decoder decoder;
        QVector<QJsonObject> messages;
        check(decoder.feed(bytes.first(split), messages, error));
        check(decoder.feed(bytes.mid(split), messages, error));
        check(messages.size() == 1 && messages[0]["type"] == "pause" && decoder.atBoundary());
    }
    for (auto bad :
         {QByteArray("BAD!1234"), QByteArray("CVM1\0\0\0\0", 8), QByteArray("CVM1\xff\xff\xff\xff", 8)})
    {
        ipc::Decoder decoder;
        QVector<QJsonObject> messages;
        check(!decoder.feed(bad, messages, error));
        check(!decoder.feed(bytes, messages, error));
    }
    ipc::Decoder incomplete;
    QVector<QJsonObject> messages;
    check(incomplete.feed(bytes.first(bytes.size() - 1), messages, error) && !incomplete.atBoundary() &&
          messages.isEmpty());
    std::mt19937 random(917);
    for (int i = 0; i < 1000; ++i)
    {
        QByteArray fuzz(int(random() % 256) + 8, 0);
        for (auto &c : fuzz)
            c = char(random());
        ipc::Decoder decoder;
        QVector<QJsonObject> output;
        check(!decoder.feed(fuzz, output, error));
    }
    for (const auto &command : {".control", ".include secret", ".lib secret", ".shell calc",
                                ".codemodel evil", ".load evil", ".write stolen"})
        check(!ipc::safeNetlist(QString("Circuit\n") + command + "\n.end", error));
    check(ipc::safeNetlist("Circuit\n.options method=gear\nRload in 0 1000\nV1 in 0 5\n.op\n.end", error));
    check(!ipc::safeNetlist(".control\nshell calc\n.endc", error));
    // A legal capture can exceed a frame even when its sample count is below 128.
    QJsonObject wideValues;
    for (int i = 0; i < 5000; ++i)
        wideValues["node" + QString::number(i)] = 1e300;
    QJsonArray wideRows;
    for (int i = 0; i < 128; ++i)
        wideRows.append(QJsonObject{{"time", i * .001}, {"values", wideValues}});
    int reconstructed = 0, frameCount = 0;
    check(ipc::writeSampleRows(
        wideRows,
        [&](QJsonObject object)
        {
            auto encoded = ipc::frame(object);
            check(!encoded.isEmpty() && encoded.size() <= ipc::MaximumFrame + 8);
            ipc::Decoder decoder;
            QVector<QJsonObject> decoded;
            check(decoder.feed(encoded, decoded, error) && decoder.atBoundary() && decoded.size() == 1);
            if (!decoded.isEmpty())
                reconstructed += decoded[0]["samples"].toArray().size();
            ++frameCount;
            return true;
        },
        error));
    check(reconstructed == 128 && frameCount > 1);
    check(!ipc::safeNetlist("Circuit\nV1 n 0 PWL file\n+ = /private/data\n.end", error));
    check(!ipc::safeNetlist("Circuit\nV1 n 0 5\r.control\r.endc\n.end", error));
    check(
        ipc::safeNetlist("Circuit\r\n.model model D(Is=1n\r\n+ N=1)\r\nD1 n 0 model\r\n.op\r\n.end", error));
    auto project = Circuit::blinker().json();
    auto probes = project["probes"].toObject();
    for (int i = 0; i < 1025; ++i)
        probes["probe" + QString::number(i)] = QJsonObject{{"part", "R1"}, {"index", 0}};
    project["probes"] = probes;
    Circuit parsed;
    check(!Circuit::parse(project, parsed, error));
    project = Circuit::blinker().json();
    QJsonArray labels;
    for (int i = 0; i < 3001; ++i)
        labels.append(QJsonObject{{"name", "NODE"}, {"pin", QJsonObject{{"part", "R1"}, {"index", 0}}}});
    project["labels"] = labels;
    check(!Circuit::parse(project, parsed, error));
    std::cout << failures << " protocol failures\n";
    return failures ? 1 : 0;
}
