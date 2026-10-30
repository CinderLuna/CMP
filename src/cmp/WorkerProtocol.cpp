#include "cmp/WorkerProtocol.h"
#include <QtEndian>
namespace cmp::ipc
{
QByteArray frame(QJsonObject object)
{
    object["version"] = Version;
    auto data = QJsonDocument(object).toJson(QJsonDocument::Compact);
    if (data.isEmpty() || data.size() > MaximumFrame)
        return {};
    QByteArray result("CVM1", 4);
    quint32 size = qToBigEndian(quint32(data.size()));
    result.append(reinterpret_cast<const char *>(&size), 4);
    result += data;
    return result;
}
bool writeSampleRows(const QJsonArray &rows, const std::function<bool(QJsonObject)> &write, QString &error)
{
    QJsonArray pending;
    qsizetype bytes = 0;
    auto flush = [&]
    {
        if (pending.isEmpty())
            return true;
        if (!write({{"type", "samples"}, {"samples", pending}}))
            return false;
        pending = {};
        bytes = 0;
        return true;
    };
    for (const auto &value : rows)
    {
        if (!value.isObject())
        {
            error = "Invalid sample row";
            return false;
        }
        auto row = value.toObject();
        auto size = QJsonDocument(row).toJson(QJsonDocument::Compact).size() + 1;
        if (size > MaximumFrame - 128)
        {
            error = "Single sample exceeds IPC frame limit";
            return false;
        }
        if ((bytes + size > MaximumFrame / 2 || pending.size() == 128) && !flush())
            return false;
        pending.append(row);
        bytes += size;
    }
    return flush();
}
bool Decoder::feed(const QByteArray &bytes, QVector<QJsonObject> &messages, QString &error)
{
    if (failed)
    {
        error = "IPC decoder is closed";
        return false;
    }
    auto fail = [&](const QString &why)
    {
        failed = true;
        buffer.clear();
        messages.clear();
        error = why;
        return false;
    };
    if (bytes.size() > MaximumFrame + 8 || buffer.size() + bytes.size() > 2 * (MaximumFrame + 8))
        return fail("IPC buffer limit exceeded");
    buffer += bytes;
    while (buffer.size() >= 8)
    {
        if (buffer.first(4) != "CVM1")
            return fail("Invalid IPC signature");
        quint32 size = qFromBigEndian<quint32>(buffer.constData() + 4);
        if (!size || size > MaximumFrame)
            return fail("IPC frame limit exceeded");
        if (buffer.size() < size + 8)
            break;
        QJsonParseError parse;
        auto json = QJsonDocument::fromJson(buffer.mid(8, size), &parse);
        if (parse.error != QJsonParseError::NoError || !json.isObject() ||
            json.object().value("version") != Version || !json.object().value("type").isString())
            return fail("Invalid IPC object/version");
        if (messages.size() >= 1024)
            return fail("IPC message count exceeded");
        messages.append(json.object());
        buffer.remove(0, size + 8);
    }
    return true;
}
bool safeNetlist(const QString &text, QString &error)
{
    if (text.toUtf8().size() > MaximumFrame / 2 || text.contains(QChar(0)))
    {
        error = "Netlist size or character limit exceeded";
        return false;
    }
    const QSet<QString> allowed{".options", ".model",   ".subckt", ".ends", ".param", ".func",
                                ".ic",      ".nodeset", ".save",   ".tran", ".op",    ".end"};
    const QRegularExpression forbidden("(file[ \\t]*=|pwlfile[ \\t]*\\()",
                                       QRegularExpression::CaseInsensitiveOption);
    QString normalized = text;
    normalized.replace("\r\n", "\n");
    for (const auto character : normalized)
        if ((character.unicode() < 32 && character != '\n' && character != '\t') ||
            character.unicode() == 127)
        {
            error = "Unsupported netlist control character";
            return false;
        }
    QStringList statements;
    int physicalLines = 0;
    for (const auto &raw : normalized.split('\n'))
    {
        if (++physicalLines > 50000)
        {
            error = "Netlist line limit exceeded";
            return false;
        }
        const auto line = raw.trimmed();
        if (line.startsWith('+'))
        {
            if (statements.size() < 2 || statements.back().startsWith('*') || statements.back().isEmpty())
            {
                error = "Ambiguous netlist continuation";
                return false;
            }
            statements.back() += " " + line.mid(1).trimmed();
        }
        else
            statements.append(line);
    }
    int lines = 0;
    for (const auto &raw : statements)
    {
        if (++lines > 50000)
        {
            error = "Netlist line limit exceeded";
            return false;
        }
        auto line = raw.trimmed();
        if (lines == 1 && line.startsWith('.'))
        {
            error = "Netlist requires a plain title line";
            return false;
        }
        if (line.isEmpty() || line.startsWith('*') || lines == 1)
            continue;
        if (line.size() > 65536 || line.contains('`') || line.contains('$') || line.contains('\\') ||
            forbidden.match(line).hasMatch())
        {
            error = "Netlist contains unsupported external access";
            return false;
        }
        if (line.startsWith('.'))
        {
            auto directive = line.section(QRegularExpression("\\s+"), 0, 0).toLower();
            if (!allowed.contains(directive))
            {
                error = "Unsupported netlist directive: " + directive;
                return false;
            }
        }
        else if (!QString("rclkvibedqmsxfgh+RCLKVIBEDQMSXFGH").contains(line[0]))
        {
            error = "Unsupported netlist element";
            return false;
        }
    }
    return true;
}
} // namespace cmp::ipc
