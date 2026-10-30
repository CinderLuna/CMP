#pragma once
#include "cmp/Native8085.h"
#include "cmp/ProcessorRuntime.h"
#include <QtCore>
namespace cmp
{
// Functional trainer: 32 KiB ROM, 32 KiB RAM, 256 I/O ports. No multiplexed bus or external interrupts.
class Cpu8085
{
  public:
    bool load(const QByteArray &, QString &);
    bool step();
    void advance(quint64 cycles);
    ProcessorRun advanceTo(quint64 target, quint64 instructionBudget = 1000000,
                           const std::atomic_bool *cancel = nullptr);
    QByteArray architecturalState() const;
    bool nativeCodeIsProtected() const;
    void enableNative(bool enabled)
    {
        nativeEnabled = enabled;
    }
    qsizetype nativeBlocks() const
    {
        return native.size();
    }
    quint8 read(quint16 address) const
    {
        return quint8(memory[address]);
    }
    quint8 input[256] = {}, output[256] = {};
    quint64 cycles = 0;
    quint16 pc = 0, sp = 0xfffe;
    bool halted = false;
    QString fault;

  private:
    bool nativeEnabled = !qEnvironmentVariableIsSet("CMP_VM_INTERPRETER");
    QHash<quint16, std::shared_ptr<Native8085>> native;
    QHash<quint16, quint8> hot;
    QByteArray memory = QByteArray(65536, 0);
    quint8 r[8] = {};
    quint8 flags = 2, mask = 7;
    bool enabled = false;
    quint8 fetch();
    quint16 word();
    quint16 pair(int) const;
    void setPair(int, quint16);
    quint8 get(int) const;
    void set(int, quint8);
    void write(quint16, quint8);
    void push(quint16);
    quint16 pop();
    void szp(quint8);
    void alu(int, quint8);
    bool condition(int) const;
};
} // namespace cmp
