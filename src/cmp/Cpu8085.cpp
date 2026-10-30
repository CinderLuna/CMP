#include "cmp/Cpu8085.h"
#include <cstring>
namespace cmp
{
bool Cpu8085::load(const QByteArray &data, QString &error)
{
    error.clear();
    if (data.isEmpty() || data.size() > 32768)
    {
        error = "8085 trainer ROM must contain 1–32768 bytes.";
        fault = error;
        halted = true;
        native.clear();
        hot.clear();
        return false;
    }
    native.clear();
    hot.clear();
    memory.fill(0);
    std::memcpy(memory.data(), data.data(), data.size());
    std::memset(r, 0, sizeof r);
    std::memset(input, 0, sizeof input);
    std::memset(output, 0, sizeof output);
    cycles = pc = 0;
    sp = 0xfffe;
    flags = 2;
    enabled = false;
    mask = 7;
    halted = false;
    fault.clear();
    return true;
}
quint8 Cpu8085::fetch()
{
    return read(pc++);
}
quint16 Cpu8085::word()
{
    quint16 low = fetch();
    return low | (quint16(fetch()) << 8);
}
quint16 Cpu8085::pair(int p) const
{
    return p == 3 ? sp : quint16((r[p * 2] << 8) | r[p * 2 + 1]);
}
void Cpu8085::setPair(int p, quint16 v)
{
    if (p == 3)
        sp = v;
    else
    {
        r[p * 2] = v >> 8;
        r[p * 2 + 1] = v;
    }
}
quint8 Cpu8085::get(int i) const
{
    return i == 6 ? read(pair(2)) : r[i];
}
void Cpu8085::set(int i, quint8 v)
{
    if (i == 6)
        write(pair(2), v);
    else
        r[i] = v;
}
void Cpu8085::write(quint16 a, quint8 v)
{
    if (a >= 32768)
        memory[a] = char(v);
    else
        fault = QString("ROM write at 0x%1").arg(a, 4, 16, QChar('0'));
}
void Cpu8085::push(quint16 v)
{
    write(--sp, v >> 8);
    write(--sp, v);
}
quint16 Cpu8085::pop()
{
    quint16 lo = read(sp++);
    return lo | (quint16(read(sp++)) << 8);
}
void Cpu8085::szp(quint8 v)
{
    int ones = 0;
    for (int i = 0; i < 8; ++i)
        ones += (v >> i) & 1;
    flags = (flags & 0x11) | 2 | (v & 0x80) | (v == 0 ? 0x40 : 0) | (ones % 2 == 0 ? 4 : 0);
}
void Cpu8085::alu(int op, quint8 b)
{
    int a = r[7], carry = flags & 1;
    int result = 0;
    bool aux = false, cy = false;
    if (op < 2)
    {
        result = a + b + (op ? carry : 0);
        aux = (a & 15) + (b & 15) + (op ? carry : 0) > 15;
        cy = result > 255;
    }
    else if (op < 4 || op == 7)
    {
        int c = op == 3 ? carry : 0;
        result = a - b - c;
        aux = (a & 15) < (b & 15) + c;
        cy = result < 0;
    }
    else if (op == 4)
    {
        result = a & b;
        aux = true;
    }
    else if (op == 5)
        result = a ^ b;
    else
        result = a | b;
    flags = 2 | (aux ? 16 : 0) | (cy ? 1 : 0);
    szp(quint8(result));
    if (op != 7)
        r[7] = quint8(result);
}
bool Cpu8085::condition(int c) const
{
    switch (c)
    {
    case 0:
        return !(flags & 0x40);
    case 1:
        return flags & 0x40;
    case 2:
        return !(flags & 1);
    case 3:
        return flags & 1;
    case 4:
        return !(flags & 4);
    case 5:
        return flags & 4;
    case 6:
        return !(flags & 0x80);
    default:
        return flags & 0x80;
    }
}
bool Cpu8085::step()
{
    if (halted || !fault.isEmpty())
        return false;
    quint16 at = pc;
    int op = fetch();
    if (op >= 0x40 && op <= 0x7f)
    {
        if (op == 0x76)
        {
            halted = true;
            cycles += 7;
        }
        else
        {
            int dst = (op >> 3) & 7, src = op & 7;
            set(dst, get(src));
            cycles += dst == 6 || src == 6 ? 7 : 4;
        }
        return fault.isEmpty();
    }
    if (op >= 0x80 && op <= 0xbf)
    {
        alu((op >> 3) & 7, get(op & 7));
        cycles += (op & 7) == 6 ? 7 : 4;
        return true;
    }
    if ((op & 0xc7) == 0x06)
    {
        int reg = (op >> 3) & 7;
        set(reg, fetch());
        cycles += reg == 6 ? 10 : 7;
        return fault.isEmpty();
    }
    if ((op & 0xcf) == 0x01)
    {
        setPair((op >> 4) & 3, word());
        cycles += 10;
        return true;
    }
    if ((op & 0xcf) == 0x03 || (op & 0xcf) == 0x0b)
    {
        int p = (op >> 4) & 3;
        setPair(p, pair(p) + ((op & 8) ? -1 : 1));
        cycles += 6;
        return true;
    }
    if ((op & 0xc7) == 0x04 || (op & 0xc7) == 0x05)
    {
        int reg = (op >> 3) & 7;
        int old = get(reg);
        bool decrement = op & 1;
        int value = old + (decrement ? -1 : 1);
        quint8 carry = flags & 1;
        flags = carry | 2 | ((decrement ? (old & 15) == 0 : (old & 15) == 15) ? 16 : 0);
        szp(value);
        set(reg, value);
        cycles += reg == 6 ? 10 : 4;
        return fault.isEmpty();
    }
    if ((op & 0xcf) == 0x09)
    {
        quint32 sum = quint32(pair(2)) + pair((op >> 4) & 3);
        setPair(2, sum);
        flags = (flags & ~1) | (sum > 65535);
        cycles += 10;
        return true;
    }
    if ((op & 0xc7) == 0xc2)
    {
        auto target = word();
        bool jump = condition((op >> 3) & 7);
        if (jump)
            pc = target;
        cycles += jump ? 10 : 7;
        return true;
    }
    if ((op & 0xc7) == 0xc4)
    {
        auto target = word();
        bool call = condition((op >> 3) & 7);
        if (call)
        {
            push(pc);
            pc = target;
        }
        cycles += call ? 18 : 9;
        return fault.isEmpty();
    }
    if ((op & 0xc7) == 0xc0)
    {
        bool ret = condition((op >> 3) & 7);
        if (ret)
            pc = pop();
        cycles += ret ? 12 : 6;
        return true;
    }
    if ((op & 0xcf) == 0xc5)
    {
        int p = (op >> 4) & 3;
        push(p == 3 ? (r[7] << 8) | flags : pair(p));
        cycles += 12;
        return fault.isEmpty();
    }
    if ((op & 0xcf) == 0xc1)
    {
        int p = (op >> 4) & 3;
        auto value = pop();
        if (p == 3)
        {
            r[7] = value >> 8;
            flags = (value & 0xd5) | 2;
        }
        else
            setPair(p, value);
        cycles += 10;
        return true;
    }
    if ((op & 0xc7) == 0xc6)
    {
        alu((op >> 3) & 7, fetch());
        cycles += 7;
        return true;
    }
    if ((op & 0xc7) == 0xc7)
    {
        push(pc);
        pc = op & 0x38;
        cycles += 12;
        return fault.isEmpty();
    }
    switch (op)
    {
    case 0x00:
        cycles += 4;
        break;
    case 0x02:
    case 0x12:
        write(pair((op >> 4) & 1), r[7]);
        cycles += 7;
        break;
    case 0x0a:
    case 0x1a:
        r[7] = read(pair((op >> 4) & 1));
        cycles += 7;
        break;
    case 0x22:
    {
        auto address = word();
        write(address, r[5]);
        write(address + 1, r[4]);
        cycles += 16;
        break;
    }
    case 0x2a:
    {
        auto address = word();
        r[5] = read(address);
        r[4] = read(address + 1);
        cycles += 16;
        break;
    }
    case 0x32:
    {
        auto address = word();
        write(address, r[7]);
        cycles += 13;
        break;
    }
    case 0x3a:
    {
        auto address = word();
        r[7] = read(address);
        cycles += 13;
        break;
    }
    case 0x07:
    {
        auto carry = r[7] >> 7;
        r[7] = (r[7] << 1) | carry;
        flags = (flags & ~1) | carry;
        cycles += 4;
        break;
    }
    case 0x0f:
    {
        auto carry = r[7] & 1;
        r[7] = (r[7] >> 1) | (carry << 7);
        flags = (flags & ~1) | carry;
        cycles += 4;
        break;
    }
    case 0x17:
    {
        auto carry = r[7] >> 7;
        r[7] = (r[7] << 1) | (flags & 1);
        flags = (flags & ~1) | carry;
        cycles += 4;
        break;
    }
    case 0x1f:
    {
        auto carry = r[7] & 1;
        r[7] = (r[7] >> 1) | ((flags & 1) << 7);
        flags = (flags & ~1) | carry;
        cycles += 4;
        break;
    }
    case 0x27:
    {
        int a = r[7], adjust = 0;
        bool carry = flags & 1;
        if ((a & 15) > 9 || (flags & 16))
            adjust |= 6;
        if (a > 0x99 || carry)
        {
            adjust |= 0x60;
            carry = true;
        }
        int sum = a + adjust;
        flags = 2 | (((a & 15) + (adjust & 15) > 15) ? 16 : 0) | (carry ? 1 : 0);
        r[7] = sum;
        szp(r[7]);
        cycles += 4;
        break;
    }
    case 0x2f:
        r[7] = ~r[7];
        cycles += 4;
        break;
    case 0x37:
        flags |= 1;
        cycles += 4;
        break;
    case 0x3f:
        flags ^= 1;
        cycles += 4;
        break;
    case 0xc3:
        pc = word();
        cycles += 10;
        break;
    case 0xcd:
    {
        auto target = word();
        push(pc);
        pc = target;
        cycles += 18;
        break;
    }
    case 0xc9:
        pc = pop();
        cycles += 10;
        break;
    case 0xd3:
    {
        auto port = fetch();
        output[port] = r[7];
        cycles += 10;
        break;
    }
    case 0xdb:
    {
        auto port = fetch();
        r[7] = input[port];
        cycles += 10;
        break;
    }
    case 0xe3:
    {
        auto value = pop();
        push(pair(2));
        setPair(2, value);
        cycles += 18;
        break;
    }
    case 0xe9:
        pc = pair(2);
        cycles += 6;
        break;
    case 0xeb:
    {
        auto de = pair(1);
        setPair(1, pair(2));
        setPair(2, de);
        cycles += 4;
        break;
    }
    case 0xf9:
        sp = pair(2);
        cycles += 6;
        break;
    case 0xf3:
        enabled = false;
        cycles += 4;
        break;
    case 0xfb:
        enabled = true;
        cycles += 4;
        break;
    case 0x20:
        r[7] = (enabled ? 8 : 0) | mask;
        cycles += 4;
        break;
    case 0x30:
        if (r[7] & 8)
            mask = r[7] & 7;
        cycles += 4;
        break;
    default:
        fault = QString("Unsupported 8085 opcode 0x%1 at 0x%2")
                    .arg(op, 2, 16, QChar('0'))
                    .arg(at, 4, 16, QChar('0'));
        break;
    }
    return fault.isEmpty();
}
bool Cpu8085::nativeCodeIsProtected() const
{
    if (native.isEmpty())
        return false;
    for (const auto &block : native)
        if (!block || !block->protectedCode())
            return false;
    return true;
}
QByteArray Cpu8085::architecturalState() const
{
    QByteArray bytes;
    QDataStream stream(&bytes, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream << quint32(1) << memory << pc << sp << cycles << flags << mask << enabled << halted << fault;
    stream.writeRawData(reinterpret_cast<const char *>(r), sizeof r);
    stream.writeRawData(reinterpret_cast<const char *>(input), sizeof input);
    stream.writeRawData(reinterpret_cast<const char *>(output), sizeof output);
    return bytes;
}
ProcessorRun Cpu8085::advanceTo(quint64 target, quint64 budget, const std::atomic_bool *cancel)
{
    quint64 work = 0;
    while (cycles < target && !halted && fault.isEmpty())
    {
        if (cancel && cancel->load(std::memory_order_relaxed))
            return {cycles, work, RunReason::Cancelled};
        if (work == budget)
            return {cycles, work, RunReason::Budget};
        if (nativeEnabled && pc < 32768 && native.size() < 256)
        {
            auto block = native.value(pc);
            if (!block)
            {
                auto &heat = hot[pc];
                if (heat < 3)
                    ++heat;
                if (heat == 3 && hot.size() < 32768)
                {
                    block = Native8085::compile(memory, pc);
                    if (block)
                        native.insert(pc, block);
                    else
                        heat = 4; // Known side exit, do not retry compilation on every visit.
                }
            }
            if (block)
            {
                Native8085::State state{};
                std::memcpy(state.r, r, sizeof r);
                state.pc = pc;
                state.cycles = cycles;
                const auto executed = block->run(state, target, qMin(quint64(32), budget - work));
                if (executed)
                {
                    std::memcpy(r, state.r, sizeof r);
                    pc = state.pc;
                    cycles = state.cycles;
                    work += executed;
                    continue;
                }
            }
        }
        step();
        ++work;
    }
    return {cycles, work,
            !fault.isEmpty() ? RunReason::Fault
            : halted         ? RunReason::Halt
                             : RunReason::Deadline};
}
void Cpu8085::advance(quint64 target)
{
    advanceTo(target);
}
} // namespace cmp
