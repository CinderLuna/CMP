#pragma once
#include <QPolygonF>
#include <QRectF>
#include <QString>
#include <QVector>
namespace cmp
{
struct WireRoute
{
    QPolygonF points;
    QString net;
};
// Orthogonal display routing only. Pin-to-pin electrical connectivity is unchanged.
QPolygonF routeWire(QPointF start, QPointF startExit, QPointF endExit, QPointF end,
                    const QVector<QRectF> &obstacles, const QVector<WireRoute> &existing, const QString &net);
} // namespace cmp
