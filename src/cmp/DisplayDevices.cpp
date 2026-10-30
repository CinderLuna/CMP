#include "cmp/DisplayDevices.h"
#include "cmp/ExpandedLibrary.h"
namespace cmp
{
void appendDisplayCatalog(QVector<PartSpec> &out)
{
    auto add = [&](QString k, QString name, QStringList pins, QString behavior) -> PartSpec &
    {
        PartSpec s{k, name, "DS", pins, 0, -2};
        s.category = "Displays & input";
        s.behavior = behavior;
        s.package = "CMP functional package";
        s.model = "CMP_" + k;
        s.limitations = "Original MIT model. LEDs have their modeled series resistance only; add external "
                        "current-limiting resistors. No optical persistence or audio synthesis.";
        out << s;
        return out.last();
    };
    for (QString k : {"SEG_CC", "SEG_CA"})
        add(k, k == "SEG_CC" ? "Seven-segment common cathode" : "Seven-segment common anode",
            {"a", "b", "c", "d", "e", "f", "g", "dp", "COM"}, "segments");
    QStringList pins;
    for (int i = 0; i < 4; ++i)
        pins << "ROW" + QString::number(i);
    for (int i = 0; i < 4; ++i)
        pins << "COL" + QString::number(i);
    add("MATRIX", "LED matrix 4 × 4", pins, "matrix");
    auto &key = add("KEYPAD", "Keypad 4 × 4", pins, "keypad");
    key.prefix = "K";
    for (int i = 0; i < 16; ++i)
        key.settings << ParameterSpec{"key" + QString::number(i),
                                      QString("Key row %1, column %2 closed").arg(i / 4 + 1).arg(i % 4 + 1),
                                      "",
                                      0,
                                      0,
                                      1};
    key.limitations = "Actual switch matrix with 50 mΩ closed contacts and 1 TΩ open contacts; multiple "
                      "closed keys may ghost. No hidden isolation diodes or scanning controller.";
}
QStringList displayNetlist(const Part &p, const QStringList &n)
{
    QStringList l;
    if (spec(p.kind).behavior == "segments")
        for (int i = 0; i < 8; ++i)
            l << "Dsegment" + QString::number(i) + " " +
                     (p.kind == "SEG_CC" ? n[i] + " " + n[8] : n[8] + " " + n[i]) + " CMP_LED";
    if (p.kind == "KEYPAD")
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 4; ++c)
                l << QString("Rkey%1%2 %3 %4 %5")
                         .arg(r)
                         .arg(c)
                         .arg(n[r], n[4 + c],
                              parameter(p, "key" + QString::number(r * 4 + c)) ? ".05" : "1e12");
    return l;
}
} // namespace cmp
