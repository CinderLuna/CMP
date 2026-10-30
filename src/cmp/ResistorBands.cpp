#include "cmp/ResistorBands.h"
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <cmath>

namespace cmp
{
namespace
{
const QStringList names = {"black", "brown", "red",    "orange", "yellow",
                           "green", "blue",  "violet", "grey",   "white"};
const QVector<QColor> colors = {QColor("#171717"), QColor("#754021"), QColor("#cd2826"), QColor("#ed791e"),
                                QColor("#f2d23a"), QColor("#288643"), QColor("#2868b1"), QColor("#88469b"),
                                QColor("#8b8c90"), QColor("#f3f0e4")};
const QColor gold("#c5a044"), silver("#bcc0c6");
} // namespace
ResistorBands resistorBands(double ohms)
{
    ResistorBands result;
    if (!std::isfinite(ohms) || ohms <= 0)
        return result;
    // Prefer the familiar two-digit code. Three-digit nominal values use a
    // fifth band rather than silently rounding the user's resistance.
    for (int digits : {2, 3})
        for (int multiplier = -2; multiplier <= 9; ++multiplier)
        {
            const double scaled = ohms / std::pow(10., multiplier);
            const double integer = std::round(scaled);
            const int minimum = digits == 2 ? 10 : 100, maximum = digits == 2 ? 99 : 999;
            if (integer < minimum || integer > maximum ||
                std::abs(scaled - integer) > qMax(1., std::abs(scaled)) * 1e-10)
                continue;
            const auto significant = QString::number(int(integer));
            for (const auto digit : significant)
            {
                result.names << names[digit.digitValue()];
                result.colors << colors[digit.digitValue()];
            }
            result.names << (multiplier == -2 ? "silver" : multiplier == -1 ? "gold" : names[multiplier]);
            result.colors << (multiplier == -2 ? silver : multiplier == -1 ? gold : colors[multiplier]);
            result.names << "gold";
            result.colors << gold;
            result.exact = true;
            return result;
        }
    return result;
}
QString resistorBandDescription(double ohms)
{
    const auto bands = resistorBands(ohms);
    return bands.exact
               ? "Bands: " + bands.names.join(" / ") +
                     "\nGold tolerance stripe: visual ±5% default; simulation uses the exact nominal value."
               : "No exact four- or five-band colour code for this value.\n"
                 "The unbanded body and numeric label preserve the entered nominal resistance.";
}
void paintResistor(QPainter &p, double ohms)
{
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    // Match the atlas terminal anchors: x=.480, y=.090/.912 in its 86 px cell.
    constexpr double x = -1.72;
    QLinearGradient metal(x - 1.2, 0, x + 1.2, 0);
    metal.setColorAt(0, QColor("#676b70"));
    metal.setColorAt(.45, QColor("#eef1f3"));
    metal.setColorAt(1, QColor("#74777a"));
    p.setPen(QPen(QBrush(metal), 2.4, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(QPointF(x, -35.26), QPointF(x, -17));
    p.drawLine(QPointF(x, 17), QPointF(x, 35.432));
    QPainterPath body;
    body.moveTo(x, -19);
    body.cubicTo(x - 8, -19, x - 8.5, -13, x - 7, -8);
    body.lineTo(x - 6.7, 6);
    body.cubicTo(x - 8.5, 12, x - 8, 19, x, 19);
    body.cubicTo(x + 8, 19, x + 8.5, 12, x + 6.7, 6);
    body.lineTo(x + 7, -8);
    body.cubicTo(x + 8.5, -13, x + 8, -19, x, -19);
    body.closeSubpath();
    QLinearGradient ceramic(x - 8, 0, x + 8, 0);
    ceramic.setColorAt(0, QColor("#927149"));
    ceramic.setColorAt(.28, QColor("#e0c79b"));
    ceramic.setColorAt(.5, QColor("#eddbb9"));
    ceramic.setColorAt(.8, QColor("#cab085"));
    ceramic.setColorAt(1, QColor("#93714c"));
    p.setPen(QPen(QColor("#806748"), .5));
    p.setBrush(ceramic);
    p.drawPath(body);
    p.setClipPath(body);
    const auto bands = resistorBands(ohms);
    const QVector<double> positions =
        bands.colors.size() == 5 ? QVector<double>{-13, -8, -3, 2, 12} : QVector<double>{-12, -6, 0, 11};
    for (int i = 0; i < bands.colors.size(); ++i)
    {
        const auto color = bands.colors[i];
        QLinearGradient stripe(x - 8, 0, x + 8, 0);
        stripe.setColorAt(0, color.darker(155));
        stripe.setColorAt(.35, color.lighter(115));
        stripe.setColorAt(.65, color);
        stripe.setColorAt(1, color.darker(150));
        const double thickness = bands.colors.size() == 5 ? 2.3 : 2.8;
        p.fillRect(QRectF(x - 9, positions[i] - thickness / 2, 18, thickness), stripe);
    }
    p.restore();
}
} // namespace cmp
