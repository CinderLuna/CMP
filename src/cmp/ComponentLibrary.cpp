#include "cmp/ComponentLibrary.h"
#include "cmp/Circuit.h"
#include "cmp/Theme.h"
#include <QApplication>
#include <QDrag>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QScopedValueRollback>
#include <QStyledItemDelegate>
namespace cmp
{
class LibraryDelegate final : public QStyledItemDelegate
{
  public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter *p, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        if (!index.data(ComponentLibrary::HeaderRole).toBool())
        {
            QStyledItemDelegate::paint(p, option, index);
            return;
        }
        p->save();
        p->fillRect(option.rect, theme().panel);
        p->setPen(theme().border);
        p->drawLine(option.rect.bottomLeft(), option.rect.bottomRight());
        p->setPen(theme().text);
        auto font = option.font;
        font.setBold(true);
        p->setFont(font);
        const auto marker = (index.data(ComponentLibrary::CollapsedRole).toBool() &&
                             !index.data(ComponentLibrary::FilteredRole).toBool())
                                ? QStringLiteral("▸ ")
                                : QStringLiteral("▾ ");
        p->drawText(option.rect.adjusted(7, 0, -5, 0), Qt::AlignVCenter | Qt::AlignLeft,
                    marker + index.data().toString());
        p->restore();
    }
};
QString draggedComponent(const QMimeData *mime, const QWidget *target)
{
    auto *data = dynamic_cast<const ComponentDragData *>(mime);
    if (!data || !data->source || data->source->window() != target->window())
        return {};
    for (const auto &part : catalog())
        if (part.kind == data->kind)
            return data->kind;
    return {};
}
ComponentLibrary::ComponentLibrary(QWidget *p) : QListWidget(p)
{
    setObjectName("componentLibrary");
    setItemDelegate(new LibraryDelegate(this));
    setUniformItemSizes(false);
    QListWidget::setGridSize({});
    setDragEnabled(false); // The click and drag gestures have independent completion paths.
    // A stable gutter prevents scrollbar appearance from changing the column
    // count, which would in turn change the height and hide the scrollbar again.
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
}
void ComponentLibrary::addCategory(const QString &name)
{
    auto *header = new QListWidgetItem(name, this);
    header->setData(HeaderRole, true);
    header->setData(CategoryRole, name);
    header->setFlags(Qt::ItemIsEnabled);
    header->setSizeHint(QSize(qMax(1, viewport()->width() - 2), 30));
}
void ComponentLibrary::setGridSize(const QSize &size)
{
    cell = size;
    QListWidget::setGridSize({});
    arrange();
}
void ComponentLibrary::arrange()
{
    if (arranging)
        return;
    QScopedValueRollback<bool> guard(arranging, true);
    setUniformItemSizes(false);
    constexpr int minimumCell = 88, growMargin = 24;
    const int available = qMax(1, viewport()->width() - 2 * frameWidth() - 1);
    if (!columns)
        columns = qMax(1, available / minimumCell);
    while (columns > 1 && available < columns * minimumCell)
        --columns;
    while (available >= (columns + 1) * minimumCell + growMargin)
        ++columns;
    cell = QSize(qMax(1, available / columns), 112);
    for (int row = 0; row < count(); ++row)
        item(row)->setSizeHint(item(row)->data(HeaderRole).toBool() ? QSize(available, 30)
                                                                    : QSize(cell.width(), 106));
    doItemsLayout();
}
void ComponentLibrary::filterComponents(const QString &text)
{
    query = text.trimmed();
    QListWidgetItem *header = nullptr;
    bool any = false, collapsed = false;
    for (int row = 0; row < count(); ++row)
    {
        auto *entry = item(row);
        if (entry->data(HeaderRole).toBool())
        {
            if (header)
                header->setHidden(!any);
            entry->setData(FilteredRole, !query.isEmpty());
            header = entry;
            any = false;
            collapsed = entry->data(CollapsedRole).toBool();
            continue;
        }
        const auto category = entry->data(CategoryRole).toString();
        const bool match = query.isEmpty() || entry->text().contains(query, Qt::CaseInsensitive) ||
                           category.contains(query, Qt::CaseInsensitive) ||
                           entry->data(Qt::UserRole).toString().contains(query, Qt::CaseInsensitive);
        any |= match;
        entry->setHidden(!match || (query.isEmpty() && collapsed));
    }
    if (header)
        header->setHidden(!any);
    arrange();
}
void ComponentLibrary::resizeEvent(QResizeEvent *event)
{
    QListWidget::resizeEvent(event);
    arrange();
}
QMimeData *ComponentLibrary::mimeDataForKind(const QString &kind)
{
    auto *data = new ComponentDragData;
    data->source = this;
    data->kind = kind;
    data->setData("application/x-cmp-component", kind.toUtf8());
    return data;
}
void ComponentLibrary::mousePressEvent(QMouseEvent *e)
{
    dragged = false;
    pressPosition = e->pos();
    pressedIndex = e->button() == Qt::LeftButton ? indexAt(e->pos()) : QModelIndex();
    if (auto *entry = itemAt(e->pos()); entry && entry->data(HeaderRole).toBool())
    {
        pressedIndex = QModelIndex();
        if (e->button() == Qt::LeftButton)
        {
            entry->setData(CollapsedRole, !entry->data(CollapsedRole).toBool());
            filterComponents(query);
        }
        return;
    }
    QListWidget::mousePressEvent(e);
}
void ComponentLibrary::mouseMoveEvent(QMouseEvent *e)
{
    if (!dragged && (e->buttons() & Qt::LeftButton) && pressedIndex.isValid() &&
        (e->pos() - pressPosition).manhattanLength() >= QApplication::startDragDistance())
    {
        auto *item = itemFromIndex(pressedIndex);
        if (!item)
            return;
        QString kind = item->data(Qt::UserRole).toString();
        auto icon = item->icon();
        dragged = true;
        pressedIndex = QModelIndex();
        emit componentDragStarted();
        QPointer<ComponentLibrary> guard(this);
        auto *drag = new QDrag(this);
        drag->setMimeData(mimeDataForKind(kind));
        drag->setPixmap(icon.pixmap(64, 64));
        drag->setHotSpot({32, 32});
        drag->exec(Qt::CopyAction, Qt::CopyAction);
        if (guard)
            drag->deleteLater();
        return;
    }
    QListWidget::mouseMoveEvent(e);
}
void ComponentLibrary::mouseReleaseEvent(QMouseEvent *e)
{
    auto released = indexAt(e->pos());
    bool activate =
        e->button() == Qt::LeftButton && !dragged && pressedIndex.isValid() && pressedIndex == released;
    QString kind = activate ? released.data(Qt::UserRole).toString() : QString();
    QListWidget::mouseReleaseEvent(e);
    pressedIndex = QModelIndex();
    if (activate && !kind.isEmpty())
        emit componentClicked(kind);
}
void ComponentLibrary::keyPressEvent(QKeyEvent *e)
{
    if ((e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter || e->key() == Qt::Key_Space) &&
        currentItem() && !currentItem()->isHidden() &&
        !currentItem()->data(Qt::UserRole).toString().isEmpty())
    {
        emit componentClicked(currentItem()->data(Qt::UserRole).toString());
        e->accept();
        return;
    }
    QListWidget::keyPressEvent(e);
}
} // namespace cmp
