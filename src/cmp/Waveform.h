#pragma once
#include "cmp/Simulator.h"
#include <QImage>
#include <QPainterPath>
#include <QTimer>
#include <QWidget>
namespace cmp
{
struct Trace
{
    QString name, key;
    QColor color;
    bool visible = true, current = false;
};
class Waveform : public QWidget
{
    Q_OBJECT
  public:
    explicit Waveform(QWidget *p = nullptr);
    Samples samples;
    QVector<Trace> traces;
    double duration = 6, cursor = 0, start = 0, end = 6;
    void clear();
    void append(Samples);
    void resetZoom();
    Sample at(double) const;
    bool exportCsv(const QString &, QString &) const;
  signals:
    void cursorChanged(double, cmp::Sample);

  protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void wheelEvent(QWheelEvent *) override;
    void resizeEvent(QResizeEvent *) override;

  private:
    struct Extrema
    {
        double low = 0, high = 0;
        int lowAt = -1, highAt = -1, count = 0;
        void include(double value, int index);
        void merge(const Extrema &other);
    };
    struct Series
    {
        QVector<double> values;
        // Level zero summarizes complete blocks of 16 samples. Higher levels
        // combine pairs, keeping range queries independent of capture length.
        QVector<QVector<Extrema>> levels;
        QPainterPath path;
        int pathSamples = -1, pathColumns = 0;
        double pathStart = 0, pathEnd = 0;
        void append(double);
        Extrema range(int begin, int end) const;
    };
    QHash<QString, Series> series;
    QImage traceLayer;
    QTimer resizeSettled;
    quint64 revision = 0, layerRevision = 0;
    QString layerKey;
    void syncSeries();
};
} // namespace cmp
