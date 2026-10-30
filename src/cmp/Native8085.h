#pragma once
#include <QtCore>
#include <memory>
namespace cmp
{
// ROM-only straight-line lowering; unsupported instructions and all RAM execution side-exit.
class Native8085
{
  public:
    struct State
    {
        quint8 r[8];
        quint16 pc;
        quint64 cycles;
    };
    static std::shared_ptr<Native8085> compile(const QByteArray &memory, quint16 pc);
    ~Native8085();
    bool protectedCode() const;
    quint64 run(State &, quint64 target, quint64 budget) const;

  private:
    void *code = nullptr;
};
} // namespace cmp
