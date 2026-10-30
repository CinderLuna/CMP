#include "cmp/MainWindow.h"
#include "cmp/Theme.h"
#include "cmp/WindowHeader.h"
#include <QLineEdit>
#include <QMenuBar>
#include <QToolButton>
#include <QtMath>
#if defined(Q_OS_WIN)
#include <qt_windows.h>
#include <windowsx.h>
#endif

namespace cmp
{
void MainWindow::syncWindowsChrome()
{
    updateWindowTheme(this);
#if defined(Q_OS_WIN)
    if (!extendedHeader_ || isFullScreen())
        return;
    const auto hwnd = reinterpret_cast<HWND>(winId());
    // Preserve native system-command support/animation while Qt's expanded-client
    // backend owns WM_NCCALCSIZE. Decoration hints stay off so its helper is blank.
    const LONG_PTR oldStyle = GetWindowLongPtrW(hwnd, GWL_STYLE);
    const LONG_PTR style =
        (oldStyle & ~WS_POPUP) | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_THICKFRAME;
    if (style != oldStyle)
    {
        SetWindowLongPtrW(hwnd, GWL_STYLE, style);
        SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }
#endif
}
bool MainWindow::nativeEvent(const QByteArray &eventType, void *message, qintptr *result)
{
#if defined(Q_OS_WIN)
    const auto *msg = static_cast<MSG *>(message);
    if (extendedHeader_ && !isFullScreen() && header_ && header_->maximizeButton())
    {
        auto *maximize = header_->maximizeButton();
        if (msg->message == WM_SYSCOMMAND)
        {
            const auto command = msg->wParam & 0xFFF0;
            if (command == SC_MINIMIZE || command == SC_MAXIMIZE || command == SC_RESTORE)
            {
                // DefWindowProc performs the system action; WM_SIZE still flows
                // through Qt and updates its window state and our restore glyph.
                *result = DefWindowProcW(msg->hwnd, msg->message, msg->wParam, msg->lParam);
                return true;
            }
        }
        if (msg->message == WM_NCHITTEST)
        {
            POINT nativePoint{GET_X_LPARAM(msg->lParam), GET_Y_LPARAM(msg->lParam)};
            ScreenToClient(msg->hwnd, &nativePoint);
            const qreal scale = devicePixelRatioF();
            const QPoint point(qFloor(nativePoint.x / scale), qFloor(nativePoint.y / scale));
            const UINT dpi = GetDpiForWindow(msg->hwnd);
            const int border = IsZoomed(msg->hwnd) ? 0
                                                   : GetSystemMetricsForDpi(SM_CXSIZEFRAME, dpi) +
                                                         GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
            const bool left = nativePoint.x < border;
            const bool right = nativePoint.x >= width() * scale - border;
            const bool top = nativePoint.y < border;
            const bool bottom = nativePoint.y >= height() * scale - border;
            if (border && (left || right || top || bottom))
            {
                *result = left    ? (top      ? HTTOPLEFT
                                     : bottom ? HTBOTTOMLEFT
                                              : HTLEFT)
                          : right ? (top      ? HTTOPRIGHT
                                     : bottom ? HTBOTTOMRIGHT
                                              : HTRIGHT)
                          : top   ? HTTOP
                                  : HTBOTTOM;
                return true;
            }
            // Use actual widget geometry, not inferred caption-slot widths.
            for (auto *button : header_->findChildren<QToolButton *>())
                if (button->isVisible() &&
                    QRect(button->mapTo(this, QPoint()), button->size()).contains(point))
                {
                    *result = button == maximize ? HTMAXBUTTON : HTCLIENT;
                    return true;
                }
            const QPoint headerPoint = header_->mapFrom(this, point);
            if (header_->rect().contains(headerPoint))
            {
                const auto *child = header_->childAt(headerPoint);
                const bool interactive = child && (child == appMenu_ || appMenu_->isAncestorOf(child) ||
                                                   child == search_ || search_->isAncestorOf(child));
                *result = interactive ? HTCLIENT : HTCAPTION;
                return true;
            }
            *result = HTCLIENT;
            return true;
        }
        if (msg->message == WM_NCLBUTTONDBLCLK && msg->wParam == HTCAPTION)
        {
            header_->requestMaximizeRestore();
            *result = 0;
            return true;
        }
        if (msg->message == WM_NCLBUTTONDOWN && msg->wParam == HTCAPTION)
        {
            *result = DefWindowProcW(msg->hwnd, msg->message, msg->wParam, msg->lParam);
            return true;
        }
        if ((msg->message == WM_NCLBUTTONDOWN || msg->message == WM_NCLBUTTONDBLCLK) &&
            msg->wParam == HTMAXBUTTON)
        {
            maximize->setDown(true);
            *result = 0;
            return true;
        }
        if (msg->message == WM_NCMOUSEMOVE && msg->wParam == HTMAXBUTTON && !maximize->underMouse())
        {
            maximize->setAttribute(Qt::WA_UnderMouse, true);
            maximize->update();
            TRACKMOUSEEVENT tracking{sizeof(TRACKMOUSEEVENT), TME_LEAVE | TME_NONCLIENT, msg->hwnd, 0};
            TrackMouseEvent(&tracking);
        }
        if (msg->message == WM_NCLBUTTONUP && msg->wParam == HTMAXBUTTON)
        {
            const bool pressed = maximize->isDown();
            maximize->setDown(false);
            if (pressed)
                header_->requestMaximizeRestore();
            *result = 0;
            return true;
        }
        if (msg->message == WM_NCMOUSELEAVE || msg->message == WM_CANCELMODE || msg->message == WM_LBUTTONUP)
        {
            maximize->setDown(false);
            maximize->setAttribute(Qt::WA_UnderMouse, false);
            maximize->update();
        }
    }
#endif
    return QMainWindow::nativeEvent(eventType, message, result);
}
} // namespace cmp
