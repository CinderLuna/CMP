#include "cmp/NativeLogic.h"
#include <cstring>
#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
namespace cmp
{
NativeLogic::~NativeLogic()
{
#if defined(Q_OS_WIN) && defined(Q_PROCESSOR_X86_64)
    for (auto &block : blocks)
        if (block.code)
            VirtualFree(block.code, 0, MEM_RELEASE);
#endif
}
std::shared_ptr<NativeLogic> NativeLogic::compile(const QVector<LogicVm::Instruction> &program)
{
#if defined(Q_OS_WIN) && defined(Q_PROCESSOR_X86_64)
    // A deliberately small Windows x64 emitter. Only validated byte slot accesses and
    // constant truth-table reads are emitted. No host calls, imported code or guest branches.
    if (program.isEmpty() || program.size() > LogicVm::MaximumInstructions)
        return {};
    const quint8 tables[][16] = {
        {0, 0, 0, 0, 1, 1, 1, 1, 3, 3, 3, 3, 3, 3, 3, 3}, {1, 1, 1, 1, 0, 0, 0, 0, 3, 3, 3, 3, 3, 3, 3, 3},
        {0, 0, 0, 0, 0, 1, 3, 3, 0, 3, 3, 3, 0, 3, 3, 3}, {0, 1, 3, 3, 1, 1, 1, 1, 3, 1, 3, 3, 3, 1, 3, 3},
        {1, 1, 1, 1, 1, 0, 3, 3, 1, 3, 3, 3, 1, 3, 3, 3}, {1, 0, 3, 3, 0, 0, 0, 0, 3, 0, 3, 3, 3, 0, 3, 3},
        {0, 1, 3, 3, 1, 0, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3}, {1, 0, 3, 3, 0, 1, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3},
        {2, 0, 3, 3, 2, 1, 3, 3, 2, 2, 3, 3, 2, 3, 3, 3}};
    auto result = std::make_shared<NativeLogic>();
    for (qsizetype first = 0; first < program.size(); first += 64)
    {
        QByteArray code;
        struct Fix
        {
            qsizetype offset;
            int table;
        };
        QVector<Fix> fixes;
        auto byte = [&](quint8 value) { code.append(char(value)); };
        auto word = [&](quint32 value)
        {
            for (int i = 0; i < 4; ++i)
                byte(quint8(value >> (8 * i)));
        };
        auto count = qMin(qsizetype(64), program.size() - first);
        for (qsizetype n = 0; n < count; ++n)
        {
            const auto &i = program[first + n];
            if (quint8(i.op) >= 9 || i.a >= LogicVm::MaximumSlots || i.b >= LogicVm::MaximumSlots ||
                i.destination >= LogicVm::MaximumSlots)
                return {};
            byte(0x0f);
            byte(0xb6);
            byte(0x81);
            word(i.a); // movzx eax,byte [rcx+a]
            byte(0xc1);
            byte(0xe0);
            byte(2); // shl eax,2
            byte(0x0f);
            byte(0xb6);
            byte(0x91);
            word(i.b); // movzx edx,byte [rcx+b]
            byte(0x09);
            byte(0xd0); // or eax,edx
            byte(0x4c);
            byte(0x8d);
            byte(0x05); // lea r8,[rip+table]
            fixes.append({code.size(), int(i.op)});
            word(0);
            byte(0x41);
            byte(0x0f);
            byte(0xb6);
            byte(0x04);
            byte(0x00); // movzx eax,byte [r8+rax]
            byte(0x88);
            byte(0x81);
            word(i.destination); // mov byte [rcx+dest],al
        }
        byte(0xc3);
        auto tableStart = code.size();
        code.append(reinterpret_cast<const char *>(tables), sizeof(tables));
        for (auto fix : fixes)
        {
            qint32 relative = qint32(tableStart + fix.table * 16 - (fix.offset + 4));
            for (int i = 0; i < 4; ++i)
                code[fix.offset + i] = char(quint32(relative) >> (8 * i));
        }
        void *memory = VirtualAlloc(nullptr, SIZE_T(code.size()), MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (!memory)
            return {};
        std::memcpy(memory, code.constData(), size_t(code.size()));
        DWORD old;
        if (!VirtualProtect(memory, SIZE_T(code.size()), PAGE_EXECUTE_READ, &old) ||
            !FlushInstructionCache(GetCurrentProcess(), memory, SIZE_T(code.size())))
        {
            VirtualFree(memory, 0, MEM_RELEASE);
            return {};
        }
        result->blocks.append({memory, count});
    }
    return result;
#else
    Q_UNUSED(program);
    return {};
#endif
}
bool NativeLogic::protectedCode() const
{
#if defined(Q_OS_WIN) && defined(Q_PROCESSOR_X86_64)
    if (blocks.isEmpty())
        return false;
    for (const auto &block : blocks)
    {
        MEMORY_BASIC_INFORMATION information{};
        if (!VirtualQuery(block.code, &information, sizeof(information)) || information.State != MEM_COMMIT ||
            information.Protect != PAGE_EXECUTE_READ)
            return false;
    }
    return true;
#else
    return false;
#endif
}
qsizetype NativeLogic::execute(qsizetype cursor, quint64 budget, quint8 *values) const
{
    if (cursor < 0 || cursor % 64 || cursor / 64 >= blocks.size())
        return 0;
    const auto &block = blocks[cursor / 64];
    if (budget < quint64(block.count))
        return 0;
    reinterpret_cast<void (*)(quint8 *)>(block.code)(values);
    return block.count;
}
} // namespace cmp
