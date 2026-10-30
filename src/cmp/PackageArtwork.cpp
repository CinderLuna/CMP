#include "cmp/PackageArtwork.h"
namespace cmp
{
QRectF dipBodyRect(int pins)
{
    const double h = (pins / 2 - 1) * 20 + 24;
    return {-22, -h / 2, 44, h};
}
QRectF dipArtworkRect(int pins)
{
    return dipBodyRect(pins).adjusted(-24, -12, 24, 12);
}
QPointF dipPinPosition(int index, int pins)
{
    const int side = pins / 2;
    return index < side ? QPointF(-38, -(side - 1) * 10 + index * 20)
                        : QPointF(38, (side - 1) * 10 - (index - side) * 20);
}
void paintDipPackage(QPainter &p, const QString &label, int pins)
{
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    for (int i = 0; i < pins; ++i)
    {
        const auto pin = dipPinPosition(i, pins);
        QLinearGradient metal(0, pin.y() - 4, 0, pin.y() + 4);
        metal.setColorAt(0, QColor("#dbe2e8"));
        metal.setColorAt(.5, QColor("#8794a3"));
        metal.setColorAt(1, QColor("#b8c3ce"));
        p.setPen(QPen(QColor("#687789"), .8));
        p.setBrush(metal);
        p.drawRect(QRectF(pin.x() < 0 ? -38 : 22, pin.y() - 3.5, 16, 7));
    }
    const auto body = dipBodyRect(pins);
    QLinearGradient resin(body.topLeft(), body.topRight());
    resin.setColorAt(0, QColor("#36404c"));
    resin.setColorAt(.2, QColor("#202833"));
    resin.setColorAt(1, QColor("#131a23"));
    p.setBrush(resin);
    p.setPen(QPen(QColor("#0b1119"), 1.5));
    p.drawRoundedRect(body, 3, 3);
    p.setBrush(QColor("#0d141d"));
    p.drawEllipse(QRectF(-6, body.top() - 5, 12, 12));
    p.setBrush(QColor("#c8d1db"));
    p.setPen(Qt::NoPen);
    p.drawEllipse(QPointF(-14, body.top() + 4), 1.5, 1.5);
    p.setPen(QColor("#eef2f6"));
    p.setFont(QFont("Segoe UI", 9, QFont::DemiBold));
    p.rotate(-90);
    p.drawText(QRectF(-60, -9, 120, 18), Qt::AlignCenter, label);
    p.restore();
}
QPixmap dipIcon(const QString &label, int pins, QSize size)
{
    QPixmap result(size);
    result.fill(Qt::transparent);
    QPainter p(&result);
    const auto bounds = dipArtworkRect(pins);
    const double scale = qMin(size.width() / bounds.width(), size.height() / bounds.height());
    p.translate(size.width() / 2., size.height() / 2.);
    p.scale(scale, scale);
    paintDipPackage(p, label, pins);
    return result;
}
} // namespace cmp
