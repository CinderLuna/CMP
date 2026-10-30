#include "cmp/Theme.h"
#include <QtWidgets>
#if defined(Q_OS_WIN)
#include <qt_windows.h>
#endif

namespace cmp
{
namespace
{
const ThemeColors light = {
    QColor("#f5f7fb"),
    QColor("#e7edf5"),
    QColor("#ffffff"),
    QColor("#f8fafd"),
    QColor("#202c3d"),
    QColor("#4d5d72"),
    QColor("#657185"),
    QColor("#73859e"),
    QColor("#d0dbe8"),
    QColor("#d7e5f7"),
    QColor("#c9dcf5"),
    QColor("#205e9c"),
    QColor("#b32323"),
    QColor("#326a37"),
    QColor("#a83d35"),
    QColor("#4e5d70"),
    QColor("#985017"),
    {QColor("#205f9c"), QColor("#965015"), QColor("#754092"), QColor("#126c6c"), QColor("#506d24"),
     QColor("#98406c"), QColor("#4d579d")},
    {QColor("#205f9c"), QColor("#965015"), QColor("#326a37"), QColor("#754092"), QColor("#a0383e")}};
const ThemeColors dark = {
    QColor("#1b2533"),
    QColor("#253245"),
    QColor("#202c3d"),
    QColor("#141e2c"),
    QColor("#edf2fa"),
    QColor("#b4c2d6"),
    QColor("#91a0b5"),
    QColor("#687e9c"),
    QColor("#2c3d53"),
    QColor("#344b68"),
    QColor("#334f73"),
    QColor("#91c4ff"),
    QColor("#ff9898"),
    QColor("#99d69d"),
    QColor("#ff9b91"),
    QColor("#c0ccdc"),
    QColor("#ffc286"),
    {QColor("#85beff"), QColor("#ffc286"), QColor("#d4a5f0"), QColor("#7bd7d1"), QColor("#bfdb82"),
     QColor("#f4a2cc"), QColor("#a9b5ff")},
    {QColor("#85beff"), QColor("#ffc286"), QColor("#99d69d"), QColor("#d4a5f0"), QColor("#ff9baf")}};
} // namespace
bool isDarkTheme()
{
    return QApplication::palette().color(QPalette::Window).lightness() < 128;
}
bool appearanceIsDark(const QString &mode, Qt::ColorScheme systemScheme)
{
    return mode == "dark" || (mode == "system" && systemScheme == Qt::ColorScheme::Dark);
}
const ThemeColors &theme()
{
    return isDarkTheme() ? dark : light;
}
void installTheme(bool useDark)
{
    const auto &c = useDark ? dark : light;
    QPalette palette;
    palette.setColor(QPalette::Window, c.window);
    palette.setColor(QPalette::WindowText, c.text);
    palette.setColor(QPalette::Base, c.base);
    palette.setColor(QPalette::AlternateBase, c.panel);
    palette.setColor(QPalette::Text, c.text);
    palette.setColor(QPalette::Button, c.panel);
    palette.setColor(QPalette::ButtonText, c.text);
    palette.setColor(QPalette::Highlight, c.selection);
    palette.setColor(QPalette::HighlightedText, c.text);
    palette.setColor(QPalette::ToolTipBase, c.base);
    palette.setColor(QPalette::ToolTipText, c.text);
    palette.setColor(QPalette::Link, c.accent);
    palette.setColor(QPalette::LinkVisited, c.accent);
    palette.setColor(QPalette::PlaceholderText, c.muted);
    palette.setColor(QPalette::Light, c.border);
    palette.setColor(QPalette::Midlight, c.hover);
    palette.setColor(QPalette::Mid, c.border);
    palette.setColor(QPalette::Dark, c.grid);
    palette.setColor(QPalette::Shadow, c.canvas);
    for (auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText, QPalette::PlaceholderText})
        palette.setColor(QPalette::Disabled, role, c.disabled);
    QApplication::setPalette(palette);
    QString sheet = R"(
QWidget{font-family:'Segoe UI';font-size:13px;color:@text;background:@window;}
QWidget:disabled{color:@disabled;}
QLabel{background:transparent;}
QMenuBar,QMenu,QStatusBar,#applicationHeader{background:@panel;}
QMenuBar::item{padding:7px 10px;background:transparent;}
QMenuBar::item:selected,QMenu::item:selected{background:@hover;color:@text;}
QMenu{border:1px solid @border;padding:4px;}
QMenu::item{padding:6px 28px 6px 12px;}
QMenu::separator{height:1px;background:@border;margin:4px 6px;}
QToolTip{background:@base;color:@text;border:1px solid @border;padding:5px;}
QToolButton{border:1px solid transparent;border-radius:4px;padding:8px;background:transparent;}
QToolButton:hover{background:@hover;border-color:@border;}
QToolButton:checked{background:@selection;border-color:@accent;}
QToolButton#runButton{background:@selection;border:1px solid @border;}
QToolButton#captionMinimize,QToolButton#captionMaximize,QToolButton#captionClose{border:0;border-radius:0;padding:0;}
QToolButton#captionMinimize:hover,QToolButton#captionMaximize:hover{background:@hover;}
QToolButton#captionClose:hover{background:#c42b1c;color:white;}
QWidget#mainToolbar{background:@window;border-bottom:1px solid @border;}
QFrame#titleMenuSeparator{background:@border;}
QLabel#panelTitle{background:@panel;padding-left:14px;font-weight:600;border-bottom:1px solid @border;min-height:40px;}
QLabel#pinDetails{padding:14px;color:@muted;}
QLabel#componentHint{padding:12px;color:@muted;}
QLabel#cursorReadout{color:@muted;}
QLabel#globalSearchShortcut{font-size:11px;color:@muted;background:transparent;}
QTreeWidget#circuitTree{background:@base;border:0;}
QWidget#circuitTreeHeader{background:@panel;border-bottom:1px solid @border;}
QToolButton#circuitTreeDelete{padding:4px;}
QTreeWidget#circuitTree::item{padding:4px 2px;}
QHeaderView::section{background:@panel;color:@muted;border:0;border-bottom:1px solid @border;padding:5px 4px;}
QLineEdit,QDoubleSpinBox,QSpinBox,QComboBox{background:@base;border:1px solid @border;border-radius:4px;padding:7px;min-height:18px;selection-background-color:@selection;selection-color:@text;}
QLineEdit:focus,QDoubleSpinBox:focus,QComboBox:focus{border:1px solid @accent;}
QLineEdit[invalid="true"]{border:2px solid @error;}
QPlainTextEdit,QTextBrowser,QTextEdit{background:@base;color:@text;border:1px solid @border;selection-background-color:@selection;selection-color:@text;}
QTabWidget::pane{border:0;}
QTabBar::tab{background:@panel;padding:10px 20px;border-right:1px solid @border;color:@muted;}
QTabBar::tab:selected{background:@window;border-top:2px solid @accent;color:@text;}
QSplitter::handle{background:@border;}
QStatusBar{border-top:1px solid @border;padding:2px 10px;}
QStatusBar::item{border:0;}
QPushButton{padding:6px 10px;border:1px solid @border;border-radius:3px;background:@panel;}
QPushButton:hover{background:@hover;border-color:@accent;}
QPushButton:default{border:1px solid @accent;}
QListWidget#componentLibrary{background:@panel;border:0;padding:6px;}
QListWidget#componentLibrary::item{background:@base;border:1px solid @border;border-radius:5px;padding:5px;color:@text;}
QListWidget#componentLibrary::item:hover{background:@hover;border-color:@accent;}
QListWidget#componentLibrary::item:selected{background:@selection;border-color:@accent;}
QAbstractItemView{background:@base;alternate-background-color:@panel;selection-background-color:@selection;selection-color:@text;}
QAbstractItemView::item:selected{background:@selection;color:@text;}
QCheckBox{spacing:5px;background:transparent;}
QCheckBox::indicator{width:12px;height:12px;border:1px solid @border;border-radius:2px;background:@base;}
QCheckBox::indicator:checked{background:@accent;border-color:@accent;image:url(:/theme/check-@scheme.svg);}
QCheckBox::indicator:disabled{border-color:@disabled;}
QScrollBar{background:@panel;}
QScrollBar:vertical{width:14px;}
QScrollBar:horizontal{height:14px;}
QScrollBar::handle{background:@border;border-radius:4px;}
QScrollBar::handle:vertical{min-height:20px;}
QScrollBar::handle:horizontal{min-width:20px;}
QScrollBar::handle:hover{background:@muted;}
QScrollBar::add-line,QScrollBar::sub-line{width:0;height:0;}
QScrollBar::add-page,QScrollBar::sub-page{background:transparent;}
)";
    const QMap<QString, QColor> substitutions = {
        {"text", c.text},         {"window", c.window}, {"panel", c.panel}, {"base", c.base},
        {"disabled", c.disabled}, {"border", c.border}, {"hover", c.hover}, {"selection", c.selection},
        {"accent", c.accent},     {"muted", c.muted},   {"error", c.error}};
    sheet.replace("@scheme", useDark ? "dark" : "light");
    for (auto it = substitutions.cbegin(); it != substitutions.cend(); ++it)
        sheet.replace('@' + it.key(), it.value().name());
    qApp->setStyleSheet(sheet);
}
void setInvalidInput(QWidget *widget, bool invalid)
{
    if (widget->property("invalid").isValid() && widget->property("invalid").toBool() == invalid)
        return;
    widget->setProperty("invalid", invalid);
    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
    widget->update();
}
QPixmap tintedPixmap(QPixmap pixmap, QColor color)
{
    QPainter painter(&pixmap);
    painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    painter.fillRect(pixmap.rect(), color);
    return pixmap;
}
void updateWindowTheme(QWidget *window)
{
#if defined(Q_OS_WIN)
    static QLibrary library("dwmapi");
    using SetAttribute = HRESULT(WINAPI *)(HWND, DWORD, LPCVOID, DWORD);
    static auto setAttribute = reinterpret_cast<SetAttribute>(library.resolve("DwmSetWindowAttribute"));
    if (setAttribute)
    {
        const BOOL darkFrame = isDarkTheme();
        setAttribute(reinterpret_cast<HWND>(window->winId()), 20, &darkFrame, sizeof(darkFrame));
    }
#else
    Q_UNUSED(window);
#endif
}
} // namespace cmp
