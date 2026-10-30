#include "cmp/Firmware.h"
namespace cmp
{
bool parseFirmware(const QByteArray &source, bool hex, QByteArray &image, QString &error, int capacity)
{
    image.clear();
    error.clear();
    auto fail = [&](QString s)
    {
        image.clear();
        error = s;
        return false;
    };
    if (capacity < 1 || capacity > 1024 * 1024)
        return fail("Invalid flash capacity.");
    if (!hex)
    {
        if (source.isEmpty() || source.size() > capacity)
            return fail("Binary firmware is empty or exceeds flash capacity.");
        image = source;
        return true;
    }
    if (source.size() > capacity * 8)
        return fail("HEX file exceeds import limit.");
    QByteArray staged(capacity, char(255));
    QBitArray used(capacity);
    quint32 base = 0;
    int end = 0;
    bool eof = false;
    for (auto raw : source.split('\n'))
    {
        auto line = raw.trimmed();
        if (line.isEmpty())
            continue;
        if (eof)
            return fail("Record after HEX end-of-file.");
        if (!line.startsWith(':') || line.size() < 11 || (line.size() % 2) != 1)
            return fail("Malformed Intel HEX record.");
        auto chars = line.mid(1);
        for (auto c : chars)
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')))
                return fail("Non-hexadecimal record.");
        auto record = QByteArray::fromHex(chars);
        auto u = [&](int i) { return quint8(record[i]); };
        if (record.size() != u(0) + 5)
            return fail("HEX record length mismatch.");
        unsigned sum = 0;
        for (auto c : record)
            sum += quint8(c);
        if (sum % 256)
            return fail("HEX checksum mismatch.");
        quint32 address = (u(1) << 8) | u(2);
        int type = u(3), count = u(0);
        if (type == 0)
        {
            quint64 absolute = quint64(base) + address;
            if (absolute + count > quint64(capacity))
                return fail("Firmware address exceeds flash capacity.");
            for (int i = 0; i < count; ++i)
            {
                int at = int(absolute) + i;
                if (used[at])
                    return fail("Overlapping HEX records.");
                used.setBit(at);
                staged[at] = record[4 + i];
                end = qMax(end, at + 1);
            }
        }
        else if (type == 1)
        {
            if (count || address)
                return fail("Invalid EOF record.");
            eof = true;
        }
        else if (type == 2 || type == 4)
        {
            if (count != 2 || address)
                return fail("Invalid extended-address record.");
            base = quint32((u(4) << 8) | u(5)) << (type == 2 ? 4 : 16);
        }
        else if (type == 3 || type == 5)
        {
            if (count != 4 || address)
                return fail("Invalid start-address record.");
        }
        else
            return fail("Unsupported Intel HEX record type.");
    }
    if (!eof || !end)
        return fail("HEX requires program bytes and an EOF record.");
    image = staged.left(end);
    return true;
}
} // namespace cmp
