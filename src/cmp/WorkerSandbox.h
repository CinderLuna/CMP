#pragma once
#include <QProcess>
#include <QtCore>
#include <memory>
namespace cmp
{
class WorkerSandbox
{
  public:
    WorkerSandbox();
    ~WorkerSandbox();
    bool prepare(QProcess &, const QString &executable, QString &error);
    static bool isContained();

  private:
    struct Impl;
    std::unique_ptr<Impl> p;
};
} // namespace cmp
