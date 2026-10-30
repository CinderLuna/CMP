#pragma once
#include <QColor>
#include <QPixmap>
#include <QVector>
class QWidget;
namespace cmp
{
struct ThemeColors
{
    QColor window, panel, base, canvas, text, muted, disabled, border, grid, hover, selection, accent, error,
        good;
    QColor supply, ground, probe;
    QVector<QColor> wires, traces;
};
bool isDarkTheme();
bool appearanceIsDark(const QString &mode, Qt::ColorScheme systemScheme);
const ThemeColors &theme();
void installTheme(bool dark);
void setInvalidInput(QWidget *, bool invalid);
void updateWindowTheme(QWidget *);
QPixmap tintedPixmap(QPixmap, QColor);
} // namespace cmp
