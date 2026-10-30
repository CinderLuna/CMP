#include "cmp/Theme.h"
#include <QtWidgets>
#include <cmath>

static double luminance(QColor color)
{
    auto linear = [](double v) { return v <= .04045 ? v / 12.92 : std::pow((v + .055) / 1.055, 2.4); };
    return .2126 * linear(color.redF()) + .7152 * linear(color.greenF()) + .0722 * linear(color.blueF());
}
static double contrast(QColor a, QColor b)
{
    const double x = luminance(a), y = luminance(b);
    return (qMax(x, y) + .05) / (qMin(x, y) + .05);
}
int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setStyle("Fusion");
    bool pass = true;
    for (auto scheme : {Qt::ColorScheme::Light, Qt::ColorScheme::Dark, Qt::ColorScheme::Unknown})
    {
        pass &= !cmp::appearanceIsDark("light", scheme);
        pass &= cmp::appearanceIsDark("dark", scheme);
        pass &= cmp::appearanceIsDark("system", scheme) == (scheme == Qt::ColorScheme::Dark);
    }
    QLineEdit invalid;
    invalid.setText("unfinished input");
    cmp::setInvalidInput(&invalid, true);
    for (bool dark : {false, true, false})
    {
        cmp::installTheme(dark);
        const auto &c = cmp::theme();
        pass &= cmp::isDarkTheme() == dark;
        double minimumText = 100, minimumBorder = 100;
        for (const auto &background : {c.window, c.panel, c.base, c.canvas})
            for (const auto &foreground : {c.text, c.muted})
                minimumText = qMin(minimumText, contrast(foreground, background));
        for (const auto &background : {c.window, c.panel, c.base})
            minimumBorder = qMin(minimumBorder, contrast(c.border, background));
        pass &= minimumText >= 4.5 && minimumBorder >= 3;
        for (const auto &color : c.traces)
            pass &= contrast(color, c.base) >= 4.5;
        for (const auto &color : c.wires)
            pass &= contrast(color, c.canvas) >= 4.5;
        pass &= contrast(c.text, c.selection) >= 4.5;
        pass &= invalid.text() == "unfinished input" && invalid.property("invalid").toBool();
        QTextStream(stdout) << (dark ? "dark" : "light") << " minimum_text_contrast=" << minimumText
                            << " minimum_control_border_contrast=" << minimumBorder << '\n';
    }
    QTextStream(stdout) << "appearance_resolution_contrast_and_input_preservation=" << pass << '\n';
    return pass ? 0 : 1;
}
