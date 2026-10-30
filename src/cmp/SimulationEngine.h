#pragma once
#include "cmp/Simulator.h"
namespace cmp
{
class SimulationEngine : public QObject
{
    Q_OBJECT
  public:
    explicit SimulationEngine(QObject *parent = nullptr, const QString &runtimePath = {});
    ~SimulationEngine() override;
    void start(const QString &netlist, bool paced = true);
    void start(const Circuit &, bool paced = true);
    void pause();
    void resume();
    void stop();
    QJsonObject statistics() const;
  signals:
    void samplesReady(cmp::Samples);
    void message(QString);
    void stateChanged(QString);
    void completed(bool);

  private:
    void startPrepared(const QString &, bool, const Circuit &);
    struct Impl;
    std::unique_ptr<Impl> p;
};
} // namespace cmp
