#include "cmp/WorkspaceTabs.h"
#include "cmp/Theme.h"
#include "cmp/ToolIcons.h"
#include <QtWidgets>
namespace cmp
{
namespace
{
class TabData final : public QMimeData
{
  public:
    QPointer<WorkspaceTabs> source;
    QPointer<QWidget> page;
};
} // namespace
WorkspaceTabs::WorkspaceTabs(QWidget *parent) : QTabWidget(parent)
{
    setDocumentMode(true);
    setMovable(true);
    setAcceptDrops(true);
    setMinimumWidth(240);
    tabBar()->setAcceptDrops(true);
    tabBar()->installEventFilter(this);
    tabBar()->setToolTip("Drag tabs to reorder. Drag below the tab bar and hover a pane edge to preview a "
                         "split, or its center to merge.");
    overlay_ = new QFrame(this);
    overlay_->setObjectName("workspaceDropOverlay");
    overlay_->setAttribute(Qt::WA_TransparentForMouseEvents);
    overlay_->setAttribute(Qt::WA_StyledBackground);
    auto *layout = new QVBoxLayout(overlay_);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->addStretch();
    icon_ = new QLabel;
    icon_->setAlignment(Qt::AlignCenter);
    layout->addWidget(icon_);
    hint_ = new QLabel;
    hint_->setAlignment(Qt::AlignCenter);
    hint_->setWordWrap(true);
    layout->addWidget(hint_);
    layout->addStretch();
    overlay_->hide();
}
QMimeData *WorkspaceTabs::mimeDataForTab(int index) const
{
    if (index < 0 || index >= count())
        return nullptr;
    auto *data = new TabData;
    data->source = const_cast<WorkspaceTabs *>(this);
    data->page = widget(index);
    data->setData("application/x-cmp-workspace-tab", "tab");
    return data;
}
void WorkspaceTabs::tabInserted(int index)
{
    QTabWidget::tabInserted(index);
    auto *page = widget(index);
    if (!page)
        return;
    page->installEventFilter(this);
    for (auto *child : page->findChildren<QWidget *>())
        child->installEventFilter(this);
}
Qt::Edge WorkspaceTabs::dropEdge(QPoint point) const
{
    if (tabBar()->geometry().contains(point))
        return static_cast<Qt::Edge>(0);
    if (point.x() < 65)
        return Qt::LeftEdge;
    if (point.x() > width() - 65)
        return Qt::RightEdge;
    if (point.y() < 65)
        return Qt::TopEdge;
    if (point.y() > height() - 65)
        return Qt::BottomEdge;
    return static_cast<Qt::Edge>(0);
}
void WorkspaceTabs::updateOverlayGeometry()
{
    QRect target = rect().adjusted(3, tabBar()->geometry().bottom() + 4, -3, -3);
    if (overlayEdge_ == Qt::LeftEdge)
        target.setWidth(target.width() / 2);
    else if (overlayEdge_ == Qt::RightEdge)
        target.setLeft(target.center().x());
    else if (overlayEdge_ == Qt::TopEdge)
        target.setHeight(target.height() / 2);
    else if (overlayEdge_ == Qt::BottomEdge)
        target.setTop(target.center().y());
    overlay_->setGeometry(target);
}
void WorkspaceTabs::showDropOverlay(Qt::Edge edge, bool valid, const QString &text)
{
    overlayEdge_ = edge;
    updateOverlayGeometry();
    const QColor color = valid ? theme().accent : theme().error;
    overlay_->setStyleSheet(QString("QFrame#workspaceDropOverlay{background:rgba(%1,%2,%3,50);"
                                    "border:2px dashed %4;border-radius:4px;}"
                                    "QLabel{background:transparent;color:%5;border:0;font-weight:600;}")
                                .arg(color.red())
                                .arg(color.green())
                                .arg(color.blue())
                                .arg(color.name())
                                .arg(theme().text.name()));
    hint_->setText(text);
    icon_->setPixmap(toolIcon(edge ? "split" : "merge").pixmap(28, 28));
    overlay_->show();
    overlay_->raise();
}
void WorkspaceTabs::hideDropOverlay()
{
    overlay_->hide();
}
void WorkspaceTabs::resizeEvent(QResizeEvent *e)
{
    QTabWidget::resizeEvent(e);
    if (overlay_ && !overlay_->isHidden())
        updateOverlayGeometry();
}
bool WorkspaceTabs::routeDrag(QEvent *event, QWidget *receiver)
{
    if (event->type() == QEvent::DragLeave)
    {
        hideDropOverlay();
        return false;
    }
    const QMimeData *mime = nullptr;
    QPoint point;
    if (event->type() == QEvent::DragEnter || event->type() == QEvent::DragMove)
    {
        auto *e = static_cast<QDragMoveEvent *>(event);
        mime = e->mimeData();
        point = receiver->mapTo(this, e->position().toPoint());
    }
    else if (event->type() == QEvent::Drop)
    {
        auto *e = static_cast<QDropEvent *>(event);
        mime = e->mimeData();
        point = receiver->mapTo(this, e->position().toPoint());
    }
    else
        return false;
    auto *data = dynamic_cast<const TabData *>(mime);
    if (!data)
        return false;
    auto *e = static_cast<QDropEvent *>(event);
    if (!data->source || !data->page || data->source->window() != window() ||
        data->source->indexOf(data->page) < 0)
    {
        hideDropOverlay();
        e->ignore();
        return true;
    }
    auto edge = dropEdge(point);
    bool valid = !(edge && data->source == this && count() < 2);
    if (canDock)
        valid &= canDock(data->source, data->page, this, edge);
    if (event->type() == QEvent::Drop)
    {
        hideDropOverlay();
        if (valid && dockRequested)
        {
            dockRequested(data->source, data->page, this, edge);
            e->setDropAction(Qt::MoveAction);
            e->accept();
        }
        else
            e->ignore();
        return true;
    }
    QString text = edge == Qt::LeftEdge     ? "Dock left"
                   : edge == Qt::RightEdge  ? "Dock right"
                   : edge == Qt::TopEdge    ? "Dock above"
                   : edge == Qt::BottomEdge ? "Dock below"
                   : data->source == this   ? "Keep tab in this pane"
                                            : "Move tab into this pane";
    showDropOverlay(edge, valid, valid ? text : "Move to another pane or its center");
    if (valid)
    {
        e->setDropAction(Qt::MoveAction);
        e->accept();
    }
    else
        e->ignore();
    return true;
}
bool WorkspaceTabs::eventFilter(QObject *object, QEvent *event)
{
    if (auto *widget = qobject_cast<QWidget *>(object))
        if (routeDrag(event, widget))
            return true;
    if (object == tabBar())
    {
        if (event->type() == QEvent::MouseButtonPress)
        {
            auto *m = static_cast<QMouseEvent *>(event);
            start_ = m->position().toPoint();
            pressed_ = m->button() == Qt::LeftButton ? widget(tabBar()->tabAt(start_)) : nullptr;
        }
        else if (event->type() == QEvent::MouseButtonRelease)
            pressed_ = nullptr;
        else if (event->type() == QEvent::MouseMove && pressed_)
        {
            auto *m = static_cast<QMouseEvent *>(event);
            if ((m->buttons() & Qt::LeftButton) &&
                (m->position().toPoint() - start_).manhattanLength() > QApplication::startDragDistance() &&
                !tabBar()->rect().contains(m->position().toPoint()))
            {
                int tab = indexOf(pressed_);
                auto *mime = mimeDataForTab(tab);
                auto picture = tabBar()->grab(tabBar()->tabRect(tab));
                pressed_ = nullptr;
                if (!mime)
                    return true;
                QPointer<QWidget> host(window());
                QPointer<WorkspaceTabs> guard(this);
                auto *drag = new QDrag(this);
                drag->setMimeData(mime);
                drag->setPixmap(picture);
                drag->exec(Qt::MoveAction);
                if (host)
                    for (auto *pane : host->findChildren<WorkspaceTabs *>())
                        pane->hideDropOverlay();
                if (guard)
                    drag->deleteLater();
                return true;
            }
        }
    }
    return QTabWidget::eventFilter(object, event);
}
void WorkspaceTabs::dragEnterEvent(QDragEnterEvent *e)
{
    if (!routeDrag(e, this))
        e->ignore();
}
void WorkspaceTabs::dragMoveEvent(QDragMoveEvent *e)
{
    if (!routeDrag(e, this))
        e->ignore();
}
void WorkspaceTabs::dragLeaveEvent(QDragLeaveEvent *e)
{
    hideDropOverlay();
    e->accept();
}
void WorkspaceTabs::dropEvent(QDropEvent *e)
{
    if (!routeDrag(e, this))
        e->ignore();
}
} // namespace cmp
