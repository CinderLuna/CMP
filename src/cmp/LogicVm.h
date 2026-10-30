#pragma once
#include <QtCore>
#include <atomic>
#include <memory>
namespace cmp
{
class NativeLogic;
// No host pointers, branches, file access or guest-controlled allocation in this instruction set.
class LogicVm
{
  public:
    enum class Op : quint8
    {
        Buffer,
        Not,
        And,
        Or,
        Nand,
        Nor,
        Xor,
        Xnor,
        TriBuffer
    };
    struct Instruction
    {
        Op op;
        quint32 destination, a, b;
    };
    enum class Stop
    {
        Complete,
        Budget,
        Cancelled,
        InvalidState
    };
    struct Result
    {
        Stop reason;
        qsizetype executed;
    };
    static constexpr quint32 MaximumSlots = 65536;
    static constexpr qsizetype MaximumInstructions = 65536;
    bool load(quint32 slotCount, const QVector<Instruction> &, QString &error);
    bool set(quint32 slot, int state);
    int get(quint32 slot) const;
    Result run(quint64 budget, const std::atomic_bool *cancel = nullptr);
    void rewind()
    {
        cursor = 0;
    }
    // Backends share the verified program and identical budget/resume contract.
    void enableNative(bool enabled);
    bool nativeCodeIsProtected() const;
    bool hasNativeCode() const
    {
        return bool(native);
    }
    static bool gate(const QString &, Op &);

  private:
    std::shared_ptr<NativeLogic> native;
    bool nativeEnabled = !qEnvironmentVariableIsSet("CMP_VM_INTERPRETER");
    QVector<Instruction> code;
    QVector<quint8> values;
    qsizetype cursor = 0;
};
} // namespace cmp
