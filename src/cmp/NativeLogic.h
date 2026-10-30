#pragma once
#include "cmp/LogicVm.h"
#include <memory>
namespace cmp
{
class NativeLogic
{
    friend class LogicVm;
    static std::shared_ptr<NativeLogic> compile(const QVector<LogicVm::Instruction> &);
    bool protectedCode() const;
    qsizetype execute(qsizetype cursor, quint64 budget, quint8 *values) const;

  private:
    struct Block
    {
        void *code = nullptr;
        qsizetype count = 0;
    };
    QVector<Block> blocks;

  public:
    ~NativeLogic();
};
} // namespace cmp
