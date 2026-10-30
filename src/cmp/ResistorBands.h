#pragma once
#include <QColor>
#include <QStringList>
#include <QVector>
class QPainter;
namespace cmp
{
struct ResistorBands
{
    QStringList names;
    QVector<QColor> colors;
    bool exact = false;
};
ResistorBands resistorBands(double ohms);
// Draws in the same -43..43 local artwork coordinates as the component atlas.
void paintResistor(QPainter &, double ohms);
QString resistorBandDescription(double ohms);
} // namespace cmp
