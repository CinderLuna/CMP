#include "cmp/ExpandedLibrary.h"
#include "cmp/ResistorBands.h"
#include "cmp/Theme.h"
#include <cmath>
namespace cmp
{
QRectF expandedBody(const Part &p, bool physical)
{
    const auto &s = spec(p.kind);
    const int pins = s.pins.size();
    if (p.kind == "UNO")
        return {-90, -155, 180, 310};
    if (physical && (s.behavior == "led" || s.behavior == "photodiode" || s.behavior == "phototransistor"))
        return {-20, -32, 40, 50};
    if (physical && s.category == "Sources" && s.behavior != "rail" && s.behavior != "battery")
        return {-34, -24, 68, 48};
    if (s.behavior == "board")
        return {-200, -220, 400, 440};
    if (s.behavior == "connector" && physical)
        return {-pins * 10., -22, pins * 20., 44};
    if (pins > 3)
    {
        const double h = qMax(64., ((pins + 1) / 2 - 1) * (physical ? 20. : 30.) + 24);
        return {physical ? -22. : -75., -h / 2, physical ? 44. : 150., h};
    }
    if (s.behavior == "pnp" || s.behavior == "darlington")
        return physical ? QRectF(-24, -30, 48, 40) : QRectF(-12, -40, 50, 80);
    if (s.behavior == "pot")
        return physical ? QRectF(-25, -25, 50, 50) : QRectF(-16, -30, 32, 60);
    return {-25, -28, 50, 56};
}
QPointF expandedPin(const Part &p, int index, bool physical)
{
    const auto &s = spec(p.kind);
    const int count = s.pins.size();
    if (p.kind == "UNO")
        return index < 14   ? QPointF(-110, -130 + index * 20)
               : index < 20 ? QPointF(110, -130 + (index - 14) * 20)
                            : QPointF(110, 30 + (index - 20) * 20);
    if (physical && (s.behavior == "led" || s.behavior == "photodiode" || s.behavior == "phototransistor" ||
                     s.behavior == "capacitor" || s.behavior == "polarCap"))
        return {index == 0 ? -10. : 10., 40};
    if (physical && s.category == "Sources" && s.behavior != "rail" && s.behavior != "battery")
        return {index == 0 ? -16. : 16., 14};
    if (s.behavior == "mosfet" || s.behavior == "thyristor")
        return physical ? QPointF(index == 0   ? 0
                                  : index == 1 ? -16
                                               : 16,
                                  40)
                        : (index == 0   ? QPointF(20, -48)
                           : index == 1 ? QPointF(-48, 0)
                                        : QPointF(20, 48));
    if (count == 3 && s.behavior == "sensor")
        return index == 0 ? QPointF(-48, -20) : index == 1 ? QPointF(-48, 20) : QPointF(48, 0);
    if (s.behavior == "board")
    {
        if (index < 200)
        {
            const int row = index / 10, col = index % 10;
            return {col < 5 ? -118. + col * 20 : 38. + (col - 5) * 20, -190. + row * 20};
        }
        const int strip = (index - 200) / 20, row = (index - 200) % 20;
        return {strip == 0 ? -178. : strip == 1 ? -158. : strip == 2 ? 158. : 178., -190. + row * 20};
    }
    if (s.behavior == "connector" && physical)
        return {-(count - 1) * 10. + index * 20, 35};

    if (s.behavior == "pnp" || s.behavior == "darlington")
        return physical ? QPointF(index == 0   ? 16
                                  : index == 1 ? 0
                                               : -16,
                                  40)
                        : (index == 0   ? QPointF(35, -40)
                           : index == 1 ? QPointF(-45, 0)
                                        : QPointF(35, 40));
    if (s.behavior == "pot")
        return index == 0 ? QPointF(0, -48) : index == 1 ? QPointF(48, 0) : QPointF(0, 48);
    if (count == 1)
        return {0, -48};
    if (count == 2)
        return {0, index == 0 ? -48. : 48.};
    const int side = (count + 1) / 2;
    const int rank = index < side ? index : count - 1 - index;
    return {index < side ? (physical ? -38. : -95.) : (physical ? 38. : 95.),
            -(side - 1) * (physical ? 10. : 15.) + rank * (physical ? 20. : 30.)};
}
void paintExpanded(QPainter &p, const Part &part, bool physical, double ledCurrent)
{
    const auto &s = spec(part.kind);
    const auto behavior = s.behavior;
    const auto body = expandedBody(part, physical);
    p.save();
    if (s.behavior == "led")
        p.setOpacity(qBound(.25, ledCurrent / .005, 1.));
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(physical ? QColor("#a8b5c5") : theme().text, 1.7));
    p.setBrush(physical ? QColor("#293340") : theme().base);
    if (physical && (behavior == "led" || behavior == "photodiode" || behavior == "phototransistor" ||
                     behavior == "capacitor" || behavior == "polarCap"))
    {
        for (int index = 0; index < 2; ++index)
        {
            const auto pin = expandedPin(part, index, true);
            p.drawLine(pin, QPointF(pin.x(), 12));
        }
        if (behavior == "led" || behavior == "photodiode" || behavior == "phototransistor")
        {
            QColor color = behavior != "led"             ? QColor("#252d38")
                           : part.kind.endsWith("BLUE")  ? QColor("#328bfa")
                           : part.kind.endsWith("GREEN") ? QColor("#4cc67c")
                           : part.kind.endsWith("WHITE") ? QColor("#edf1f9")
                                                         : QColor("#eeab38");
            p.setBrush(color);
            p.drawRoundedRect(QRectF(-17, -30, 34, 44), 14, 14);
            p.setPen(QPen(QColor("#8facc3"), 2));
            p.drawLine(-17, 12, 17, 12);
            p.setBrush(QColor("#ffffff66"));
            p.setPen(Qt::NoPen);
            p.drawEllipse(QRectF(-10, -23, 10, 20));
        }
        else if (behavior == "polarCap")
        {
            p.setBrush(QColor("#303b47"));
            p.drawRoundedRect(QRectF(-19, -28, 38, 48), 6, 6);
            p.setBrush(QColor("#a7b6c6"));
            p.drawEllipse(QRectF(-19, -28, 38, 11));
            p.fillRect(QRectF(7, -17, 8, 32), QColor("#b8c4d0"));
            p.setPen(QColor("#243343"));
            p.drawText(QRectF(7, -16, 8, 28), Qt::AlignCenter, "−");
        }
        else
        {
            p.setBrush(part.kind == "CERAMIC" ? QColor("#b58446") : QColor("#3a78a5"));
            if (part.kind == "CERAMIC")
                p.drawEllipse(QRectF(-23, -27, 46, 44));
            else
                p.drawRoundedRect(QRectF(-25, -23, 50, 38), 4, 4);
        }
        p.restore();
        return;
    }
    if (!physical && behavior == "phototransistor")
    {
        p.drawLine(0, -48, 0, -25);
        p.drawLine(0, 25, 0, 48);
        p.drawEllipse(QRectF(-23, -25, 46, 50));
        p.drawLine(-8, -13, -8, 13);
        p.drawLine(-8, -9, 0, -25);
        p.drawLine(-8, 9, 0, 25);
        p.drawText(-43, -10, "↘↘");
        p.drawPolyline(QPolygonF{{-2, 16}, {0, 25}, {-8, 21}});
        p.restore();
        return;
    }
    if (physical && s.category == "Sources" && s.behavior != "rail" && s.behavior != "battery")
    {
        QLinearGradient metal(0, -24, 0, 24);
        metal.setColorAt(0, QColor("#b4bac0"));
        metal.setColorAt(.15, QColor("#eef0f2"));
        metal.setColorAt(1, QColor("#b7c0c8"));
        p.setBrush(metal);
        p.setPen(QPen(QColor("#7c8994"), 1));
        p.drawRoundedRect(body, 4, 4);
        p.setBrush(QColor("#183f50"));
        p.drawRoundedRect(QRectF(-27, -17, 54, 20), 2, 2);
        p.setPen(QColor("#8addd5"));
        p.setFont(QFont("Segoe UI", 7));
        p.drawText(QRectF(-27, -17, 54, 20), Qt::AlignCenter,
                   s.behavior == "sine"      ? "∿"
                   : s.behavior == "current" ? "I"
                                             : s.kind);
        for (int pin = 0; pin < 2; ++pin)
        {
            const auto pos = expandedPin(part, pin, true);
            p.setBrush(pin ? QColor("#29343e") : QColor("#bd4d4e"));
            p.setPen(QColor("#5c6975"));
            p.drawEllipse(pos, 5, 5);
        }
        p.restore();
        return;
    }
    if (s.behavior == "sensor")
    {
        p.drawRoundedRect(body, physical ? 4 : 0, physical ? 4 : 0);
        for (int pin = 0; pin < 3; ++pin)
        {
            const auto pos = expandedPin(part, pin, physical);
            p.drawLine(pos, QPointF(pos.x() < 0 ? body.left() : body.right(), pos.y()));
        }
        p.setFont(QFont("Segoe UI", 7));
        p.setPen(physical ? Qt::white : theme().text);
        p.drawText(body, Qt::AlignCenter, "SENSOR");
        p.restore();
        return;
    }
    if (s.behavior == "battery")
    {
        p.drawLine(0, -48, 0, -20);
        p.drawLine(0, 20, 0, 48);
        if (physical)
        {
            p.setBrush(QColor("#a44b4f"));
            p.drawRoundedRect(QRectF(-21, -29, 42, 58), 5, 5);
            p.setPen(Qt::white);
            p.drawText(QRectF(-20, -24, 40, 48), Qt::AlignCenter, "+\nBAT\n−");
        }
        else
        {
            p.drawLine(-24, -14, 24, -14);
            p.drawLine(-12, -4, 12, -4);
            p.drawLine(-24, 6, 24, 6);
            p.drawLine(-12, 16, 12, 16);
        }
        p.restore();
        return;
    }
    if (s.behavior == "board")
    {
        p.setBrush(physical ? QColor("#d9dce0") : theme().panel);
        p.setPen(QPen(physical ? QColor("#929da8") : theme().border, 2));
        p.drawRoundedRect(body, 12, 12);
        p.setBrush(physical ? QColor("#b6bcc4") : theme().canvas);
        p.setPen(Qt::NoPen);
        p.drawRect(QRectF(-17, -205, 34, 410));
        for (int rail = 0; rail < 4; ++rail)
        {
            p.setPen(QPen(rail % 2 ? QColor("#3683b9") : QColor("#ca6263"), 2));
            const double x = rail == 0 ? -186 : rail == 1 ? -146 : rail == 2 ? 146 : 186;
            p.drawLine(QPointF(x, -200), QPointF(x, -10));
            p.drawLine(QPointF(x, 10), QPointF(x, 200));
        }
        for (int row = 0; row < 20; ++row)
        {
            const double y = -190 + row * 20;
            if (!physical)
            {
                p.setPen(QPen(theme().muted, 1));
                p.drawLine(QPointF(-118, y), QPointF(-38, y));
                p.drawLine(QPointF(38, y), QPointF(118, y));
            }
            p.setPen(physical ? QColor("#455464") : theme().text);
            p.setFont(QFont("Segoe UI", 6));
            p.drawText(QRectF(-14, y - 7, 28, 14), Qt::AlignCenter, QString::number(row + 1));
        }
        p.setPen(QPen(QColor("#737e89"), .6));
        p.setBrush(QColor("#394451"));
        for (int pin = 0; pin < 280; ++pin)
            p.drawEllipse(expandedPin(part, pin, physical), 2.5, 2.5);
        p.restore();
        return;
    }
    if (s.behavior == "connector" && physical)
    {
        p.setBrush(part.kind == "TERMINAL" ? QColor("#357f66") : QColor("#26313c"));
        p.drawRoundedRect(body, 3, 3);
        for (int i = 0; i < s.pins.size(); ++i)
        {
            const auto pin = expandedPin(part, i, true);
            p.setPen(QPen(QColor("#b6c4d1"), 2));
            p.drawLine(pin, QPointF(pin.x(), 22));
            p.setBrush(QColor("#bbc8d4"));
            p.drawEllipse(QPointF(pin.x(), 0), 6, 6);
            p.setPen(QPen(QColor("#415164"), 2));
            p.drawLine(QPointF(pin.x() - 3, -3), QPointF(pin.x() + 3, 3));
        }
        p.restore();
        return;
    }
    if (s.behavior == "testpoint" || s.behavior == "rail")
    {
        p.drawLine(0, -48, 0, -12);
        p.setBrush(s.behavior == "testpoint" ? QColor("#c49b49") : theme().base);
        p.drawEllipse(QPointF(0, 0), 12, 12);
        p.drawText(QRectF(-25, 18, 50, 18), Qt::AlignCenter,
                   s.behavior == "testpoint" ? "TP" : "RAIL " + QString::number(parameter(part, "rail")));
        p.restore();
        return;
    }
    if (behavior == "motor" || behavior == "buzzer" || behavior == "acoustic")
    {
        p.drawLine(0, -48, 0, -27);
        p.drawLine(0, 27, 0, 48);
        p.setBrush(physical ? QColor("#657786") : theme().base);
        p.drawEllipse(QRectF(-25, -27, 50, 54));
        p.setPen(physical ? QColor("#f0f2f4") : theme().text);
        if (behavior == "motor")
        {
            p.drawText(QRectF(-20, -20, 40, 40), Qt::AlignCenter, "M");
            if (physical)
            {
                p.save();
                p.rotate(part.parameters.value("__view_angle", 0) * 180 / 3.141592653589793);
                p.setPen(QPen(QColor("#d5dfe6"), 4));
                p.drawLine(-18, 0, 18, 0);
                p.restore();
            }
        }
        else
        {
            p.drawEllipse(QRectF(-13, -15, 26, 30));
            if (physical)
            {
                p.setPen(QPen(QColor("#24323d"), 2));
                for (int y = -10; y <= 10; y += 5)
                    p.drawLine(-9, y, 9, y);
            }
            else
            {
                p.drawLine(12, -10, 24, -20);
                p.drawLine(12, 10, 24, 20);
            }
        }
        p.restore();
        return;
    }
    if (behavior == "mosfet" || behavior == "thyristor")
    {
        if (physical)
        {
            for (int i = 0; i < 3; ++i)
            {
                auto pin = expandedPin(part, i, true);
                p.drawLine(pin, QPointF(pin.x(), 0));
            }
            p.setBrush(QColor("#aab5bf"));
            p.drawRoundedRect(QRectF(-22, -33, 44, 43), 3, 3);
            p.setBrush(QColor("#28323d"));
            p.drawEllipse(QPointF(0, -25), 4, 4);
            p.drawRoundedRect(QRectF(-24, -15, 48, 30), 3, 3);
            p.setPen(Qt::white);
            p.setFont(QFont("Segoe UI", 7));
            p.drawText(QRectF(-23, -14, 46, 28), Qt::AlignCenter, part.kind);
        }
        else if (behavior == "mosfet")
        {
            p.drawLine(20, -48, 20, -20);
            p.drawLine(20, 20, 20, 48);
            p.drawLine(20, -20, 0, -20);
            p.drawLine(0, -20, 0, 20);
            p.drawLine(0, 20, 20, 20);
            p.drawLine(-8, -22, -8, 22);
            p.drawLine(-48, 0, -8, 0);
            p.drawPolyline(part.kind == "PMOS" ? QPolygonF{{-1, -6}, {-7, 0}, {-1, 6}}
                                               : QPolygonF{{-7, -6}, {-1, 0}, {-7, 6}});
        }
        else
        {
            p.drawLine(20, -48, 20, -17);
            p.drawLine(20, 17, 20, 48);
            p.drawPolygon(QPolygonF{{5, -17}, {35, -17}, {20, 14}});
            p.drawLine(3, 17, 37, 17);
            p.drawLine(-48, 0, -10, 0);
            p.drawLine(-10, 0, 5, 17);
            if (part.kind == "TRIAC")
                p.drawPolyline(QPolygonF{{7, 12}, {20, -12}, {33, 12}});
        }
        p.restore();
        return;
    }

    if (behavior == "digital" && s.pins.size() <= 3)
    {
        p.setBrush(physical ? QColor("#45677b") : theme().base);
        p.drawRoundedRect(body, 4, 4);
        for (int i = 0; i < s.pins.size(); ++i)
        {
            auto pin = expandedPin(part, i, physical);
            p.drawLine(pin, QPointF(pin.x() < 0 ? body.left() : body.right(), pin.y()));
        }
        p.setPen(physical ? Qt::white : theme().text);
        p.setFont(QFont("Segoe UI", 7));
        p.drawText(body, Qt::AlignCenter, part.kind == "CLOCK" ? "CLK" : "0 / 1");
        p.restore();
        return;
    }
    if (behavior == "segments" || behavior == "matrix" || behavior == "keypad")
    {
        p.setBrush(physical ? QColor("#222c36") : theme().base);
        p.drawRoundedRect(body, 4, 4);
        for (int i = 0; i < s.pins.size(); ++i)
        {
            auto pin = expandedPin(part, i, physical);
            p.setPen(QPen(physical ? QColor("#a8b5c5") : theme().text, 1.7));
            p.drawLine(pin, QPointF(pin.x() < 0 ? body.left() : body.right(), pin.y()));
        }
        if (behavior == "segments")
        {
            double scale = qMin(body.width() / 45., body.height() / 75.);
            p.save();
            p.scale(scale, scale);
            const QLineF strokes[] = {QLineF(-10, -25, 10, -25), QLineF(14, -21, 14, -4),
                                      QLineF(14, 4, 14, 21),     QLineF(-10, 25, 10, 25),
                                      QLineF(-14, 4, -14, 21),   QLineF(-14, -21, -14, -4),
                                      QLineF(-10, 0, 10, 0)};
            for (int i = 0; i < 7; ++i)
            {
                double current = std::abs(part.parameters.value("__view_pin" + QString::number(i)));
                p.setPen(QPen(current > .0005 ? QColor("#f55e64") : QColor("#68343b"), 4, Qt::SolidLine,
                              Qt::RoundCap));
                p.drawLine(strokes[i]);
            }
            p.setBrush(std::abs(part.parameters.value("__view_pin7")) > .0005 ? QColor("#f55e64")
                                                                              : QColor("#68343b"));
            p.setPen(Qt::NoPen);
            p.drawEllipse(QPointF(22, 25), 2, 2);
            p.restore();
        }
        else
        {
            double size = qMin(body.width() / 5., body.height() / 5.);
            p.save();
            p.translate(-1.5 * size, -1.5 * size);
            for (int r = 0; r < 4; ++r)
                for (int c = 0; c < 4; ++c)
                {
                    bool lit = behavior == "keypad"
                                   ? parameter(part, "key" + QString::number(r * 4 + c))
                                   : part.parameters.value("__view_cell" + QString::number(r) +
                                                           QString::number(c)) > .0005;
                    p.setBrush(lit ? QColor("#e76869") : QColor("#5d3c45"));
                    p.setPen(Qt::NoPen);
                    if (behavior == "matrix")
                        p.drawEllipse(QPointF(c * size, r * size), size * .3, size * .3);
                    else
                    {
                        p.drawRoundedRect(QRectF((c - .4) * size, (r - .4) * size, size * .8, size * .8), 2,
                                          2);
                        p.setPen(Qt::white);
                        p.setFont(QFont("Segoe UI", 5));
                        p.drawText(QRectF((c - .4) * size, (r - .4) * size, size * .8, size * .8),
                                   Qt::AlignCenter, QString::number(r * 4 + c));
                    }
                }
            p.restore();
        }
        p.restore();
        return;
    }
    if (part.kind == "UNO")
    {
        p.setBrush(physical ? QColor("#287184") : theme().base);
        p.drawRoundedRect(body, 8, 8);
        for (int i = 0; i < 24; ++i)
        {
            auto point = expandedPin(part, i, physical);
            p.drawLine(point, QPointF(point.x() < 0 ? -90 : 90, point.y()));
        }
        p.setBrush(QColor("#25313d"));
        p.drawRoundedRect(QRectF(-20, -90, 40, 180), 3, 3);
        p.setPen(Qt::white);
        p.setFont(QFont("Segoe UI", 8));
        p.drawText(QRectF(-75, 100, 150, 40), Qt::AlignCenter, "UNO · ATmega328P");
        p.save();
        p.rotate(-90);
        p.drawText(QRectF(-90, -15, 180, 30), Qt::AlignCenter, "ATmega328P");
        p.restore();
        p.restore();
        return;
    }
    if (s.pins.size() > 3)
    {
        p.drawRoundedRect(body, physical ? 4 : 0, physical ? 4 : 0);
        for (int i = 0; i < s.pins.size(); ++i)
        {
            const auto pin = expandedPin(part, i, physical);
            p.drawLine(pin, QPointF(pin.x() < 0 ? body.left() : body.right(), pin.y()));
        }
        p.setPen(physical ? QColor("#eef2f6") : theme().text);
        p.setFont(QFont("Segoe UI", 8, QFont::DemiBold));
        p.drawText(body.adjusted(4, 4, -4, -4), Qt::AlignCenter | Qt::TextWordWrap,
                   QString(s.kind).replace('_', ' '));
        p.restore();
        return;
    }
    if (behavior == "pnp" || behavior == "darlington")
    {
        if (physical)
        {
            for (int i = 0; i < 3; ++i)
            {
                auto pin = expandedPin(part, i, true);
                p.drawLine(pin, QPointF(pin.x(), 0));
            }
            p.drawRoundedRect(body, 8, 8);
            p.setPen(Qt::white);
            p.drawText(body, Qt::AlignCenter, behavior == "darlington" ? "DARL" : "3906");
        }
        else
        {
            p.drawLine(-45, 0, -8, 0);
            p.drawLine(-8, -20, -8, 20);
            p.drawLine(-8, -12, 35, -40);
            p.drawLine(-8, 12, 35, 40);
            p.setBrush(theme().text);
            p.drawPolygon(part.kind == "DARL_N" ? QPolygonF{{26, 34}, {14, 31}, {21, 24}}
                                                : QPolygonF{{4, 19}, {16, 20}, {11, 29}});
            if (behavior == "darlington")
            {
                p.setBrush(Qt::NoBrush);
                p.drawEllipse(QRectF(-14, -34, 42, 68));
            }
        }
    }
    else if (behavior == "pot" || behavior == "resistor" || behavior == "rnet" || behavior == "jumper" ||
             behavior == "ldr" || behavior == "ntc" || behavior == "ptc")
    {
        p.drawLine(0, -48, 0, -25);
        p.drawLine(0, 25, 0, 48);
        p.setBrush(physical ? QColor("#ceb18b") : theme().base);
        p.drawRoundedRect(QRectF(-9, -25, 18, 50), physical ? 5 : 0, physical ? 5 : 0);
        if (behavior == "pot")
        {
            if (physical)
            {
                p.setPen(QPen(QColor("#aebdcc"), 1.7));
                p.drawLine(48, 0, 25, 0);
                p.setBrush(QColor("#276b9c"));
                p.drawRoundedRect(QRectF(-25, -25, 50, 50), 6, 6);
                p.setBrush(QColor("#bbc5d0"));
                p.drawEllipse(QPointF(0, 0), 15, 15);
                p.save();
                p.rotate(-120 + 240 * parameter(part, "wiper"));
                p.setPen(QPen(QColor("#263645"), 3));
                p.drawLine(0, -11, 0, 11);
                p.restore();
            }
            else
            {
                const double y = 25 - 50 * parameter(part, "wiper");
                p.drawLine(QPointF(48, 0), QPointF(12, y));
                p.drawPolyline(QPolygonF{{20, y - 5}, {12, y}, {20, y + 5}});
            }
        }
        if (behavior == "ldr")
            p.drawText(QRectF(15, -16, 38, 25), Qt::AlignCenter, "↙↙");
        if (behavior == "ntc" || behavior == "ptc")
        {
            p.drawLine(-22, 20, 22, -20);
            p.drawText(16, 28, behavior == "ntc" ? "−t" : "+t");
        }
        if (part.kind == "FUSE")
        {
            p.drawLine(0, -23, 0, 23);
            p.drawText(12, 4, "F");
        }
        if (behavior == "jumper")
        {
            p.drawText(QRectF(-25, -25, 50, 50), Qt::AlignCenter, parameter(part, "closed") ? "●" : "○");
        }
        if (physical && behavior == "resistor" && part.kind != "FUSE")
        {
            auto bands = resistorBands(part.value);
            if (part.kind == "RPOWER" && !bands.colors.isEmpty())
            {
                const QMap<double, QColor> tolerance = {{.05, QColor("#85898d")}, {.1, QColor("#804b9b")},
                                                        {.25, QColor("#3479c5")}, {.5, QColor("#3c9865")},
                                                        {1, QColor("#784728")},   {2, QColor("#bb3d38")},
                                                        {5, QColor("#c9a14a")},   {10, QColor("#b7bec6")}};
                if (parameter(part, "tolerance") == 20)
                    bands.colors.removeLast();
                else
                    bands.colors.last() = tolerance.value(parameter(part, "tolerance"), QColor("#c9a14a"));
            }
            for (int i = 0; i < bands.colors.size(); ++i)
            {
                p.setPen(QPen(bands.colors[i], 3));
                p.drawLine(-8, -17 + i * 8, 8, -17 + i * 8);
            }
        }
    }
    else if (behavior == "capacitor" || behavior == "polarCap")
    {
        p.drawLine(0, -48, 0, -7);
        p.drawLine(0, 7, 0, 48);
        if (physical)
        {
            p.setBrush(behavior == "polarCap" ? QColor("#323b49") : QColor("#3975a6"));
            p.drawRoundedRect(body, behavior == "polarCap" ? 9 : 4, 5);
            p.setPen(Qt::white);
            p.drawText(body, Qt::AlignCenter, quantity(part.value, part.kind));
        }
        else
        {
            p.drawLine(-22, -7, 22, -7);
            p.drawLine(-22, 7, 22, 7);
            if (behavior == "polarCap")
                p.drawText(-34, -13, "+");
        }
    }
    else if (behavior == "inductor")
    {
        p.drawLine(0, -48, 0, -28);
        p.drawLine(0, 28, 0, 48);
        p.setBrush(Qt::NoBrush);
        if (physical)
        {
            p.setPen(QPen(QColor("#b67b42"), 5));
            for (int i = 0; i < 5; ++i)
                p.drawEllipse(QRectF(-14, -27 + i * 10, 28, 12));
        }
        else
            for (int i = 0; i < 4; ++i)
                p.drawArc(QRectF(-10, -28 + i * 14, 20, 14), -90 * 16, 180 * 16);
    }
    else if (behavior == "switch")
    {
        p.drawLine(0, -48, 0, -20);
        p.drawLine(0, 20, 0, 48);
        p.drawEllipse(QPointF(0, -20), 3, 3);
        p.drawEllipse(QPointF(0, 20), 3, 3);
        p.drawLine(QPointF(0, -20), parameter(part, "state") ? QPointF(0, 20) : QPointF(20, 14));
        if (physical)
        {
            p.setBrush(QColor("#397589"));
            p.drawRoundedRect(QRectF(-26, -14, 52, 28), 5, 5);
            p.drawText(QRectF(-25, -14, 50, 28), Qt::AlignCenter, "SW");
        }
    }
    else if (s.category == "Sources")
    {
        p.drawLine(0, -48, 0, -25);
        p.drawEllipse(QRectF(-25, -25, 50, 50));
        p.drawLine(0, 25, 0, 48);
        p.drawText(QRectF(-20, -20, 40, 40), Qt::AlignCenter, behavior == "current" ? "↓" : "∿");
    }
    else
    {
        p.drawLine(0, -48, 0, -18);
        p.drawLine(0, 18, 0, 48);
        if (physical)
        {
            QColor fill = behavior == "led" ? QColor(part.kind.endsWith("BLUE")    ? "#328bfa"
                                                     : part.kind.endsWith("GREEN") ? "#4cc67c"
                                                     : part.kind.endsWith("WHITE") ? "#edf1f9"
                                                                                   : "#eeab38")
                                            : QColor("#252b35");
            p.setBrush(fill);
            p.drawRoundedRect(QRectF(-11, -22, 22, 44), behavior == "led" ? 10 : 3, 5);
            p.setPen(QPen(QColor("#dce3ef"), 5));
            p.drawLine(-10, 12, 10, 12);
        }
        else
        {
            p.drawPolygon(QPolygonF{{-16, -16}, {16, -16}, {0, 16}});
            p.drawLine(-19, 16, 19, 16);
            if (behavior == "zener" || behavior == "tvs")
            {
                p.drawLine(-19, 16, -23, 10);
                p.drawLine(19, 16, 23, 22);
            }
            if (part.kind == "SCHOTTKY")
            {
                p.drawLine(-19, 16, -19, 9);
                p.drawLine(19, 16, 19, 23);
            }
            if (behavior == "photodiode" || behavior == "phototransistor")
                p.drawText(-42, -6, "↘↘");
            if (behavior == "led")
                p.drawText(21, -2, "↗");
        }
    }
    p.restore();
}
QPixmap expandedIcon(const PartSpec &definition)
{
    Part part;
    part.kind = definition.kind;
    part.value = definition.value;
    auto bounds = expandedBody(part, true).adjusted(-22, -22, 22, 22).united(QRectF(-54, -54, 108, 108));
    QPixmap result(144, 144);
    result.fill(Qt::transparent);
    QPainter p(&result);
    p.translate(72, 72);
    double scale = qMin(140. / bounds.width(), 140. / bounds.height());
    p.scale(scale, scale);
    paintExpanded(p, part, true);
    return result;
}
} // namespace cmp
