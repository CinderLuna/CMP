#include "cmp/WindowSizeGrip.h"
#include "cmp/Theme.h"
#include <QPainter>

namespace cmp
{
WindowSizeGrip::WindowSizeGrip(QWidget *parent) : QSizeGrip(parent)
{
    setObjectName("windowResizeGrip");
    setAccessibleName("Resize window");
    setToolTip("Resize window");
    setFixedSize(18, 18);
}
void WindowSizeGrip::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), theme().panel);
    if (isRightToLeft())
    {
        painter.translate(width(), 0);
        painter.scale(-1, 1);
    }
    painter.setPen(QPen(theme().muted, 1.6, Qt::SolidLine, Qt::RoundCap));
    for (int length : {4, 8, 12})
        painter.drawLine(QPointF(width() - length - 3, height() - 4),
                         QPointF(width() - 4, height() - length - 3));
}
} // namespace cmp
