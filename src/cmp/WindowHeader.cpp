#include "cmp/WindowHeader.h"
#include <QFontDatabase>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QTimer>
#include <QToolButton>
#include <QWindow>
#include <QtMath>
#if defined(Q_OS_WIN)

#include <qt_windows.h>
#endif

namespace cmp
{
bool WindowHeader::supportsExpandedTitleBar()
{
#if defined(Q_OS_WIN) && QT_VERSION == QT_VERSION_CHECK(6, 9, 3)
    // This adapter depends on an audited backend implementation, not a public
    // button-geometry contract. Reaudit before enabling another runtime version.
    return QGuiApplication::platformName() == "windows" && qstrcmp(qVersion(), "6.9.3") == 0;
#else
    return false;
#endif
}
WindowHeader::WindowHeader(QWidget *parent) : QWidget(parent), row_(new QHBoxLayout(this))
{
    setObjectName("applicationHeader");
    setAttribute(Qt::WA_StyledBackground, true);
    setAccessibleName("Application header");
    row_->setContentsMargins(16, 0, 0, 0);
    setMinimumHeight(48);
}
void WindowHeader::installCaptionControls()
{
    if (controls_ || !supportsExpandedTitleBar())
        return;
    controls_ = new QWidget(this);
    controls_->setObjectName("captionControls");
    controls_->setStyleSheet("QWidget#captionControls { background:transparent; }");
    auto *layout = new QHBoxLayout(controls_);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    const auto families = QFontDatabase::families();
    QFont glyphs(families.contains("Segoe Fluent Icons") ? "Segoe Fluent Icons" : "Segoe MDL2 Assets");
    glyphs.setPixelSize(10);
    const auto add = [&](const char *name, const char *label, ushort glyph)
    {
        auto *button = new QToolButton(controls_);
        button->setObjectName(name);
        button->setAccessibleName(label);
        button->setToolTip(label);
        button->setText(QString(QChar(glyph)));
        button->setFont(glyphs);
        button->setFixedWidth(46);
        button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
        button->setStyleSheet(QString("QToolButton { padding:0; border:0; border-radius:0; "
                                      "font-family:'%1'; font-size:10px; }")
                                  .arg(glyphs.family()));
        layout->addWidget(button);
        return button;
    };
    auto *minimize = add("captionMinimize", "Minimize", 0xE921);
    maximize_ = add("captionMaximize", "Maximize", 0xE922);
    auto *close = add("captionClose", "Close", 0xE8BB);
    close->setStyleSheet(close->styleSheet() + "QToolButton:hover { background:#C42B1C; color:white; }");
#if defined(Q_OS_WIN)
    connect(minimize, &QToolButton::clicked, this, [this]
            { PostMessageW(reinterpret_cast<HWND>(window()->winId()), WM_SYSCOMMAND, SC_MINIMIZE, 0); });
    connect(close, &QToolButton::clicked, this,
            [this] { PostMessageW(reinterpret_cast<HWND>(window()->winId()), WM_SYSCOMMAND, SC_CLOSE, 0); });
#endif
    connect(maximize_, &QToolButton::clicked, this, &WindowHeader::requestMaximizeRestore);
    row_->addSpacing(12);
    row_->addWidget(controls_);
}
void WindowHeader::requestMaximizeRestore()
{
#if defined(Q_OS_WIN)
    if (supportsExpandedTitleBar() && !window()->isFullScreen())
    {
        const auto hwnd = reinterpret_cast<HWND>(window()->winId());
        PostMessageW(hwnd, WM_SYSCOMMAND, IsZoomed(hwnd) ? SC_RESTORE : SC_MAXIMIZE, 0);
        return;
    }
#endif
    window()->isMaximized() ? window()->showNormal() : window()->showMaximized();
}
void WindowHeader::updateCaptionSpace()
{
    int reserved = 0;
#if defined(Q_OS_WIN) && QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
    auto *handle = window()->windowHandle();
    if (supportsExpandedTitleBar() && handle && handle->flags().testFlag(Qt::ExpandedClientAreaHint) &&
        !window()->isFullScreen())
    {

        // QWidget may restore WindowTitleHint when it recreates its native window.
        // App-owned controls replace the Qt helper's caption visuals.
        // Only write a changed flag, avoiding needless native-frame updates.
        if (handle->flags().testFlag(Qt::WindowTitleHint))
            handle->setFlag(Qt::WindowTitleHint, false);
    }
#endif
    if (controls_)
    {
        controls_->setVisible(!window()->isFullScreen());
        reserved = window()->isFullScreen() ? 0 : controls_->sizeHint().width();
        maximize_->setText(QString(QChar(window()->isMaximized() ? 0xE923 : 0xE922)));
        maximize_->setAccessibleName(window()->isMaximized() ? "Restore Down" : "Maximize");
        maximize_->setToolTip(maximize_->accessibleName());
    }
    row_->setContentsMargins(16, 0, 0, 0);
    setProperty("captionReservedWidth", reserved);
}
void WindowHeader::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    updateCaptionSpace();
    // One event-driven retry after native creation; no polling. The context cancels
    // this callback if the header is destroyed before the event loop runs.
    QTimer::singleShot(0, this, &WindowHeader::updateCaptionSpace);
}
bool WindowHeader::event(QEvent *event)
{
    const bool handled = QWidget::event(event);
    if (event->type() == QEvent::DevicePixelRatioChange)
        updateCaptionSpace();
    return handled;
}
void WindowHeader::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && !window()->isFullScreen() && window()->windowHandle())
    {
        if (window()->windowHandle()->startSystemMove())
        {
            event->accept();
            return;
        }
    }
    QWidget::mousePressEvent(event);
}
void WindowHeader::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && !window()->isFullScreen())
    {
        requestMaximizeRestore();
        event->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}
} // namespace cmp
