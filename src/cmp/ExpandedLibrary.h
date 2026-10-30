#pragma once
#include "cmp/Circuit.h"
#include <QPainter>
namespace cmp
{
void appendExpandedCatalog(QVector<PartSpec> &);
bool expanded(const QString &);
double parameter(const Part &, const QString &);
QStringList validateExpanded(const Part &);
QStringList expandedNetlist(const Part &, const QStringList &pins);
QPointF expandedPin(const Part &, int, bool physical);
QRectF expandedBody(const Part &, bool physical);
void paintExpanded(QPainter &, const Part &, bool physical, double ledCurrent = .02);
QPixmap expandedIcon(const PartSpec &);
bool measuresPins(const QString &);
QPointF physicalLocalPin(const Part &, int);
QPointF snappedToBoard(const Circuit &, const Part &);
void joinExpandedConnections(const Circuit &, const std::function<void(QString, QString)> &);
QVector<QVector<int>> contactGroups(const QString &);
bool parseCurve(const QByteArray &, QVector<QPointF> &, QString &, bool timeAxis = true);
QStringList internalMeasurements(const Part &);
QString internalKey(const Part &, const QString &);
QString currentKey(const Part &, int pin = 0);
} // namespace cmp

namespace cmp
{
void appendAnalogCatalog(QVector<PartSpec> &);
QStringList analogDeviceNetlist(const Part &, const QStringList &);
} // namespace cmp
