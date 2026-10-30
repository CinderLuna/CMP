#pragma once
#include <QtWidgets>
namespace cmp
{
QRectF dipArtworkRect(int pins);
QRectF dipBodyRect(int pins);
QPointF dipPinPosition(int index, int pins);
void paintDipPackage(QPainter &, const QString &label, int pins);
QPixmap dipIcon(const QString &label, int pins, QSize size = {128, 128});
} // namespace cmp
