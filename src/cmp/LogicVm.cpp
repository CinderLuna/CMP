#include "cmp/LogicVm.h"
#include "cmp/NativeLogic.h"
namespace cmp
{
bool LogicVm::nativeCodeIsProtected() const
{
    return native && native->protectedCode();
}
void LogicVm::enableNative(bool enabled)
{
    nativeEnabled = enabled;
    if (!enabled)
        native.reset();
    else if (!native && !code.isEmpty())
        native = NativeLogic::compile(code);
}
bool LogicVm::gate(const QString &kind, Op &op)
{
    static const QMap<QString, Op> gates{{"BUF", Op::Buffer}, {"NOT", Op::Not},   {"AND", Op::And},
                                         {"OR", Op::Or},      {"NAND", Op::Nand}, {"NOR", Op::Nor},
                                         {"XOR", Op::Xor},    {"XNOR", Op::Xnor}, {"TRIBUF", Op::TriBuffer}};
    auto it = gates.constFind(kind);
    if (it == gates.cend())
        return false;
    op = it.value();
    return true;
}
bool LogicVm::load(quint32 slotCount, const QVector<Instruction> &program, QString &error)
{
    // Failed validation removes the old executable program as well.
    native.reset();
    code.clear();
    values.clear();
    cursor = 0;
    error.clear();
    if (!slotCount || slotCount > MaximumSlots || program.size() > MaximumInstructions)
    {
        error = "Logic VM resource limit exceeded";
        return false;
    }
    for (const auto &i : program)
        if (quint8(i.op) > quint8(Op::TriBuffer) || i.destination >= slotCount || i.a >= slotCount ||
            i.b >= slotCount)
        {
            error = "Invalid logic VM instruction or slot";
            return false;
        }
    code = program;
    values.fill(3, slotCount);
    if (nativeEnabled)
        native = NativeLogic::compile(code);
    return true;
}
bool LogicVm::set(quint32 slot, int state)
{
    if (slot >= quint32(values.size()) || state < 0 || state > 3)
        return false;
    values[slot] = quint8(state);
    return true;
}
int LogicVm::get(quint32 slot) const
{
    return slot < quint32(values.size()) ? values[slot] : 3;
}
LogicVm::Result LogicVm::run(quint64 budget, const std::atomic_bool *cancel)
{
    if (values.isEmpty())
        return {Stop::InvalidState, 0};
    qsizetype count = 0;
    while (cursor < code.size())
    {
        if (cancel && cancel->load(std::memory_order_relaxed))
            return {Stop::Cancelled, count};
        if (quint64(count) == budget)
            return {Stop::Budget, count};
        if (nativeEnabled && native)
        {
            auto advanced = native->execute(cursor, budget - quint64(count), values.data());
            if (advanced)
            {
                cursor += advanced;
                count += advanced;
                continue;
            }
        }
        const auto &i = code[cursor];
        const int a = values[i.a], b = values[i.b];
        int r = 3;
        switch (i.op)
        {
        case Op::Buffer:
            r = a < 2 ? a : 3;
            break;
        case Op::Not:
            r = a < 2 ? 1 - a : 3;
            break;
        case Op::And:
        case Op::Nand:
            r = a == 0 || b == 0 ? 0 : a == 1 && b == 1 ? 1 : 3;
            break;
        case Op::Or:
        case Op::Nor:
            r = a == 1 || b == 1 ? 1 : a == 0 && b == 0 ? 0 : 3;
            break;
        case Op::Xor:
        case Op::Xnor:
            r = a < 2 && b < 2 ? a ^ b : 3;
            break;
        case Op::TriBuffer:
            r = b == 0 ? 2 : b == 1 ? a : 3;
            break;
        }
        if ((i.op == Op::Nand || i.op == Op::Nor || i.op == Op::Xnor) && r < 2)
            r = 1 - r;
        values[i.destination] = quint8(r);
        ++cursor;
        ++count;
    }
    return {Stop::Complete, count};
}
} // namespace cmp
