#pragma once
#include <QtCore>
#include <memory>
namespace cmp
{
struct Circuit;
struct ElectricalReport;
struct Sample
{
    double time = 0;
    QMap<QString, double> values;
};
using Samples = QVector<Sample>;
class Simulator : public QObject
{
    Q_OBJECT
  public:
    explicit Simulator(QObject *parent = nullptr, const QString &runtimePath = {});
    ~Simulator() override;
    void start(const QString &netlist, bool paced = true);
    void start(const Circuit &, bool paced = true);
    void pause();
    void resume();
    void stop();
    qint64 workerProcessId() const;
    bool hasElectricalReport() const;
    ElectricalReport electricalReport() const;
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
Q_DECLARE_METATYPE(cmp::Samples)
