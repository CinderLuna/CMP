#include "cmp/Waveform.h"
#include "cmp/Theme.h"
#include <QtWidgets>
#include <algorithm>
#include <cmath>
namespace cmp
{
void Waveform::Extrema::include(double value, int index)
{
    if (!std::isfinite(value))
        return;
    if (!count || value < low)
    {
        low = value;
        lowAt = index;
    }
    if (!count || value > high)
    {
        high = value;
        highAt = index;
    }
    ++count;
}
void Waveform::Extrema::merge(const Extrema &other)
{
    if (!other.count)
        return;
    if (!count || other.low < low)
    {
        low = other.low;
        lowAt = other.lowAt;
    }
    if (!count || other.high > high)
    {
        high = other.high;
        highAt = other.highAt;
    }
    count += other.count;
}
void Waveform::Series::append(double value)
{
    values.append(value);
    if (values.size() % 16)
        return;
    Extrema block;
    for (int i = values.size() - 16; i < values.size(); ++i)
        block.include(values[i], i);
    int level = 0;
    for (;;)
    {
        if (level == levels.size())
            levels.append(QVector<Extrema>{});
        levels[level].append(block);
        int count = levels[level].size();
        if (count % 2)
            break;
        block = levels[level][count - 2];
        block.merge(levels[level][count - 1]);
        ++level;
    }
}
Waveform::Extrema Waveform::Series::range(int begin, int end) const
{
    Extrema result;
    while (begin < end && begin % 16)
    {
        result.include(values[begin], begin);
        ++begin;
    }
    while (begin + 16 <= end)
    {
        int block = begin / 16, level = 0, span = 16;
        while (level + 1 < levels.size() && block % 2 == 0 && begin + span * 2 <= end)
        {
            block /= 2;
            span *= 2;
            ++level;
        }
        result.merge(levels[level][block]);
        begin += span;
    }
    while (begin < end)
    {
        result.include(values[begin], begin);
        ++begin;
    }
    return result;
}
void Waveform::syncSeries()
{
    for (const auto &trace : traces)
    {
        auto &data = series[trace.key];
        for (int i = data.values.size(); i < samples.size(); ++i)
            data.append(samples[i].values.value(trace.key));
    }
}
Waveform::Waveform(QWidget *p) : QWidget(p)
{
    setMinimumHeight(150);
    setMouseTracking(true);
    setObjectName("waveformPlot");
    resizeSettled.setSingleShot(true);
    resizeSettled.setInterval(80);
    connect(&resizeSettled, &QTimer::timeout, this, qOverload<>(&Waveform::update));
}
void Waveform::clear()
{
    samples.clear();
    series.clear();
    traceLayer = {};
    ++revision;
    cursor = 0;
    update();
}
void Waveform::append(Samples a)
{
    samples += a;
    ++revision;
    syncSeries();
    if (!samples.isEmpty())
        cursor = samples.back().time;
    update();
}
void Waveform::resetZoom()
{
    start = 0;
    end = duration;
    update();
}
void Waveform::resizeEvent(QResizeEvent *event)
{
    // Reuse the trace raster while resizing. Rebuild sharply at the final size
    // after the gesture settles, rather than stroking every trace per resize tick.
    resizeSettled.start();
    QWidget::resizeEvent(event);
}
Sample Waveform::at(double t) const
{
    if (samples.isEmpty())
        return {};
    auto it = std::lower_bound(samples.begin(), samples.end(), t,
                               [](const Sample &a, double b) { return a.time < b; });
    if (it == samples.end())
        return samples.back();
    return *it;
}
bool Waveform::exportCsv(const QString &path, QString &error) const
{
    if (samples.isEmpty())
    {
        error = "No simulation samples to export.";
        return false;
    }
    QSet<QString> keys;
    for (auto &s : samples)
        for (auto it = s.values.begin(); it != s.values.end(); ++it)
            keys.insert(it.key());
    auto names = keys.values();
    names.sort();
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        error = f.errorString();
        return false;
    }
    QTextStream out(&f);
    out.setRealNumberPrecision(16);
    out << "time_s";
    for (auto &n : names)
        out << ",\"" << n << "\"";
    out << "\n";
    for (auto &s : samples)
    {
        out << s.time;
        for (auto &n : names)
            out << "," << s.values.value(n);
        out << "\n";
    }
    out.flush();
    if (!f.commit())
    {
        error = f.errorString();
        return false;
    }
    return true;
}
void Waveform::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), theme().base);
    p.setFont(QFont("Segoe UI", 9));
    QRectF area(55, 20, width() - 125, height() - 52);
    if (area.width() < 1 || area.height() < 1)
        return;
    if (samples.isEmpty())
    {
        p.setPen(theme().muted);
        p.drawText(rect(), Qt::AlignCenter, "Run the circuit to inspect voltage and current traces.");
        return;
    }
    syncSeries();
    auto lower = [&](double time)
    {
        return int(std::lower_bound(samples.cbegin(), samples.cend(), time,
                                    [](const Sample &sample, double t) { return sample.time < t; }) -
                   samples.cbegin());
    };
    const int first = lower(start);
    const int last = int(std::upper_bound(samples.cbegin(), samples.cend(), end,
                                          [](double t, const Sample &sample) { return t < sample.time; }) -
                         samples.cbegin());
    double minV = 0, maxV = 1, minI = 0, maxI = 1;
    bool hasV = false, hasI = false;
    for (const auto &t : traces)
        if (t.visible)
        {
            const auto entry = series.constFind(t.key);
            const auto &data = entry.value();
            const auto limits = data.range(first, last);
            if (!limits.count)
                continue;
            if (t.current)
            {
                hasI = true;
                minI = qMin(minI, limits.low * 1000);
                maxI = qMax(maxI, limits.high * 1000);
            }
            else
            {
                hasV = true;
                minV = qMin(minV, limits.low);
                maxV = qMax(maxV, limits.high);
            }
        }
    maxV = std::ceil(maxV * 1.05);
    maxI = std::ceil(maxI * 1.05);
    minV = std::floor(minV);
    minI = std::floor(minI);
    for (int i = 0; i <= 6; i++)
    {
        double x = area.left() + area.width() * i / 6;
        p.setPen(theme().grid);
        p.drawLine(QPointF(x, area.top()), QPointF(x, area.bottom()));
        p.setPen(theme().muted);
        p.drawText(QRectF(x - 32, area.bottom() + 6, 64, 18), Qt::AlignCenter,
                   QString::number(start + (end - start) * i / 6, 'g', 4));
    }
    for (int i = 0; i <= 5; i++)
    {
        double y = area.bottom() - area.height() * i / 5;
        p.setPen(theme().grid);
        p.drawLine(QPointF(area.left(), y), QPointF(area.right(), y));
        p.setPen(theme().muted);
        if (hasV)
            p.drawText(QRectF(0, y - 9, 47, 18), Qt::AlignRight,
                       QString::number(minV + (maxV - minV) * i / 5, 'g', 3));
        if (hasI)
            p.drawText(QRectF(area.right() + 7, y - 9, 60, 18), Qt::AlignLeft,
                       QString::number(minI + (maxI - minI) * i / 5, 'g', 3));
    }
    p.setClipRect(area.adjusted(-1, -1, 1, 1));
    QString key = QString::number(start, 'g', 17) + '/' + QString::number(end, 'g', 17) + '/' +
                  QString::number(devicePixelRatioF(), 'g', 17);
    for (const auto &trace : traces)
        key += '/' + trace.key + ':' + trace.color.name(QColor::HexArgb) + (trace.visible ? "v" : "h") +
               (trace.current ? "i" : "u");
    const qreal dpr = devicePixelRatioF();
    const QSize rasterSize(qMax(1, int(std::ceil(area.width() * dpr))),
                           qMax(1, int(std::ceil(area.height() * dpr))));
    if (traceLayer.isNull() || layerRevision != revision || layerKey != key ||
        (!resizeSettled.isActive() && traceLayer.size() != rasterSize))
    {
        traceLayer = QImage(rasterSize, QImage::Format_ARGB32_Premultiplied);
        traceLayer.setDevicePixelRatio(dpr);
        traceLayer.fill(Qt::transparent);
        QPainter layerPainter(&traceLayer);
        layerPainter.setRenderHint(QPainter::Antialiasing);
        auto renderTraces = [&](QPainter &p, const QRectF &area)
        {
            // Time bins use binary searches once per pixel, shared by all traces.
            // Include neighboring samples to preserve segments crossing the viewport boundary.
            const int drawFirst = qMax(0, first - 1), drawLast = qMin(int(samples.size()), last + 1);
            const int columns =
                int(qNextPowerOfTwo(quint32(qMax(1, int(std::ceil(area.width() * devicePixelRatioF()))))));
            QVector<int> boundaries;
            const bool dense = drawLast - drawFirst > columns * 2;
            if (dense)
            {
                boundaries.reserve(columns + 1);
                boundaries.append(drawFirst);
                for (int x = 1; x < columns; ++x)
                    boundaries.append(lower(start + (end - start) * x / columns));
                boundaries.append(drawLast);
            }
            for (const auto &t : traces)
                if (t.visible)
                {
                    auto &data = series[t.key];
                    if (data.pathSamples != samples.size() || data.pathStart != start ||
                        data.pathEnd != end || data.pathColumns != columns)
                    {
                        QPainterPath path;
                        bool begun = false;
                        auto point = [&](int index)
                        {
                            const double value = data.values[index];
                            if (!std::isfinite(value))
                            {
                                begun = false;
                                return;
                            }
                            const QPointF pos((samples[index].time - start) / (end - start), value);
                            if (begun)
                                path.lineTo(pos);
                            else
                                path.moveTo(pos);
                            begun = true;
                        };
                        if (!dense)
                        {
                            for (int index = drawFirst; index < drawLast; ++index)
                                point(index);
                        }
                        else
                            for (int x = 0; x < columns; ++x)
                            {
                                const int begin = boundaries[x], finish = boundaries[x + 1];
                                if (begin == finish)
                                    continue;
                                const auto extrema = data.range(begin, finish);
                                // Never draw a line across a bucket containing an invalid value.
                                if (extrema.count != finish - begin)
                                {
                                    begun = false;
                                    continue;
                                }
                                int indices[] = {begin, extrema.lowAt, extrema.highAt, finish - 1};
                                std::sort(std::begin(indices), std::end(indices));
                                int previous = -1;
                                for (int index : indices)
                                    if (index != previous)
                                    {
                                        point(index);
                                        previous = index;
                                    }
                            }
                        data.path = std::move(path);
                        data.pathSamples = samples.size();
                        data.pathStart = start;
                        data.pathEnd = end;
                        data.pathColumns = columns;
                    }
                    const double units = t.current ? 1000 : 1;
                    const double lo = (t.current ? minI : minV) / units;
                    const double hi = (t.current ? maxI : maxV) / units;
                    QTransform mapping;
                    mapping.translate(area.left(), area.bottom());
                    mapping.scale(area.width(), -area.height() / (hi - lo));
                    mapping.translate(0, -lo);
                    p.setPen(QPen(t.color, 1.8));
                    p.drawPath(mapping.map(data.path));
                }
        };
        renderTraces(layerPainter, QRectF(0, 0, area.width(), area.height()));
        layerRevision = revision;
        layerKey = key;
    }
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.drawImage(area, traceLayer);
    if (cursor >= start && cursor <= end)
    {
        double x = area.left() + (cursor - start) / (end - start) * area.width();
        p.setPen(QPen(theme().text, 1, Qt::DashLine));
        p.drawLine(QPointF(x, area.top()), QPointF(x, area.bottom()));
    }
    p.setClipping(false);
    p.setPen(theme().muted);
    p.drawText(QRectF(area.left(), height() - 19, area.width(), 18), Qt::AlignCenter,
               "Time (s) · wheel to zoom · click to inspect");
    if (hasV)
        p.drawText(6, 12, "V");
    if (hasI)
        p.drawText(width() - 55, 12, "mA");
}
void Waveform::mousePressEvent(QMouseEvent *e)
{
    cursor = qBound(start, start + (e->position().x() - 55) / qMax(1, width() - 125) * (end - start), end);
    emit cursorChanged(cursor, at(cursor));
    update();
}
void Waveform::wheelEvent(QWheelEvent *e)
{
    double center = start + (end - start) * qBound(0., (e->position().x() - 55) / qMax(1, width() - 125), 1.);
    double factor = e->angleDelta().y() > 0 ? .8 : 1.25;
    double span = qBound(duration / 10000, (end - start) * factor, duration);
    start = qBound(0., center - span / 2, duration - span);
    end = start + span;
    update();
    e->accept();
}
} // namespace cmp
