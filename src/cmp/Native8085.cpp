#include "cmp/Native8085.h"
#include <cstddef>
#include <cstring>
#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
namespace cmp
{
Native8085::~Native8085()
{
#ifdef Q_OS_WIN
    if (code)
        VirtualFree(code, 0, MEM_RELEASE);
#endif
}
std::shared_ptr<Native8085> Native8085::compile(const QByteArray &memory, quint16 start)
{
#if defined(Q_OS_WIN) && defined(Q_PROCESSOR_X86_64)
    if (memory.size() != 65536 || start >= 32768)
        return {};
    QByteArray output;
    QVector<qsizetype> exits;
    auto b = [&](quint8 v) { output.append(char(v)); };
    auto w = [&](quint32 v)
    {
        for (int i = 0; i < 4; ++i)
            b(quint8(v >> (i * 8)));
    };
    auto exit = [&]
    {
        b(0x0f);
        b(0x83);
        exits.append(output.size());
        w(0);
    };
    b(0x45);
    b(0x31);
    b(0xc9); // xor r9d,r9d (instruction count)
    quint32 pc = start;
    for (int count = 0; count < 32 && pc < 32768; ++count)
    {
        const int op = quint8(memory[pc]);
        const int dst = (op >> 3) & 7, src = op & 7;
        bool move = op >= 0x40 && op <= 0x7f && dst != 6 && src != 6;
        bool immediate = (op & 0xc7) == 0x06 && dst != 6 && pc + 1 < 32768;
        if (op != 0 && !move && !immediate)
            break;
        b(0x48);
        b(0x39);
        b(0x51);
        b(offsetof(State, cycles));
        exit(); // reached target
        b(0x4d);
        b(0x39);
        b(0xc1);
        exit(); // exhausted budget
        if (move)
        {
            b(0x8a);
            b(0x41);
            b(src);
            b(0x88);
            b(0x41);
            b(dst);
        } // mov al,[rcx+src]; mov [rcx+dst],al
        if (immediate)
        {
            b(0xc6);
            b(0x41);
            b(dst);
            b(quint8(memory[pc + 1]));
        }
        pc += immediate ? 2 : 1;
        b(0x66);
        b(0xc7);
        b(0x41);
        b(offsetof(State, pc));
        b(quint8(pc));
        b(quint8(pc >> 8));
        b(0x48);
        b(0x83);
        b(0x41);
        b(offsetof(State, cycles));
        b(immediate ? 7 : 4);
        b(0x49);
        b(0xff);
        b(0xc1); // inc r9
    }
    if (pc == start)
        return {};
    const auto end = output.size();
    b(0x4c);
    b(0x89);
    b(0xc8);
    b(0xc3); // mov rax,r9; ret
    for (auto at : exits)
    {
        quint32 offset = quint32(end - (at + 4));
        for (int i = 0; i < 4; ++i)
            output[at + i] = char(offset >> (8 * i));
    }
    auto result = std::make_shared<Native8085>();
    result->code = VirtualAlloc(nullptr, SIZE_T(output.size()), MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!result->code)
        return {};
    std::memcpy(result->code, output.constData(), size_t(output.size()));
    DWORD old;
    if (!VirtualProtect(result->code, SIZE_T(output.size()), PAGE_EXECUTE_READ, &old) ||
        !FlushInstructionCache(GetCurrentProcess(), result->code, SIZE_T(output.size())))
        return {};
    return result;
#else
    Q_UNUSED(memory);
    Q_UNUSED(start);
    return {};
#endif
}
bool Native8085::protectedCode() const
{
#if defined(Q_OS_WIN) && defined(Q_PROCESSOR_X86_64)
    MEMORY_BASIC_INFORMATION information{};
    return code && VirtualQuery(code, &information, sizeof(information)) && information.State == MEM_COMMIT &&
           information.Protect == PAGE_EXECUTE_READ;
#else
    return false;
#endif
}
quint64 Native8085::run(State &state, quint64 target, quint64 budget) const
{
    return reinterpret_cast<quint64 (*)(State *, quint64, quint64)>(code)(&state, target, budget);
}
} // namespace cmp
