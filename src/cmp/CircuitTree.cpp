#include "cmp/CircuitTree.h"
#include "cmp/ToolIcons.h"
#include <QtWidgets>

namespace cmp
{
CircuitTree::CircuitTree(Document *doc, QWidget *parent) : QTreeWidget(parent), document(doc)
{
    setObjectName("circuitTree");
    setAccessibleName("Circuit components and probes");
    setHeaderLabels({"Item", "Value / Pin"});
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    setEditTriggers(QAbstractItemView::NoEditTriggers);
    setUniformRowHeights(true);
    setIndentation(12);
    header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    header()->setSectionResizeMode(1, QHeaderView::Stretch);
    setContextMenuPolicy(Qt::CustomContextMenu);
    connect(this, &QTreeWidget::itemSelectionChanged, this, &CircuitTree::circuitSelectionChanged);
    connect(this, &QTreeWidget::customContextMenuRequested, this,
            [this](QPoint point)
            {
                auto *item = itemAt(point);
                if (item && !item->isSelected() && !item->data(0, Qt::UserRole).toString().isEmpty())
                {
                    clearSelection();
                    setCurrentItem(item);
                }
                QMenu menu(this);
                auto *remove = menu.addAction(toolIcon("delete"), "Delete selected");
                remove->setEnabled(!document->editsLocked && !selection().empty() &&
                                   (!item || !item->data(0, Qt::UserRole).toString().isEmpty()));
                connect(remove, &QAction::triggered, this, &CircuitTree::deleteSelection);
                menu.exec(viewport()->mapToGlobal(point));
            });
    connect(this, &QTreeWidget::itemActivated, this,
            [this](QTreeWidgetItem *item)
            {
                const auto kind = item->data(0, Qt::UserRole).toString();
                const auto id = item->data(0, Qt::UserRole + 1).toString();
                if (kind == "part")
                    emit locatePin(Pin{id, 0});
                else if (kind == "probe" && document->circuit.probes.contains(id))
                    emit locatePin(document->circuit.probes.value(id));
            });
    connect(document, &Document::changed, this, &CircuitTree::refresh);
    refresh();
}
CircuitTreeSelection CircuitTree::selection() const
{
    CircuitTreeSelection result;
    for (auto *item : selectedItems())
    {
        const auto kind = item->data(0, Qt::UserRole).toString();
        const auto id = item->data(0, Qt::UserRole + 1).toString();
        if (kind == "part")
            result.parts << id;
        else if (kind == "probe")
            result.probes << id;
    }
    return result;
}
void CircuitTree::setSelectedParts(const QStringList &ids)
{
    QSignalBlocker blocker(this);
    clearSelection();
    QTreeWidgetItem *first = nullptr;
    for (QTreeWidgetItemIterator it(this); *it; ++it)
        if ((*it)->data(0, Qt::UserRole).toString() == "part" &&
            ids.contains((*it)->data(0, Qt::UserRole + 1).toString()))
        {
            (*it)->setSelected(true);
            if (!first)
                first = *it;
        }
    if (first)
    {
        setCurrentItem(first, 0, QItemSelectionModel::NoUpdate);
        scrollToItem(first);
    }
}
void CircuitTree::refresh()
{
    const auto previous = selection();
    const bool partsExpanded = topLevelItemCount() ? topLevelItem(0)->isExpanded() : true;
    const bool probesExpanded = topLevelItemCount() > 1 ? topLevelItem(1)->isExpanded() : true;
    QSignalBlocker blocker(this);
    clear();
    auto *parts = new QTreeWidgetItem(this, {QString("Components (%1)").arg(document->circuit.parts.size())});
    auto *probes = new QTreeWidgetItem(this, {QString("Probes (%1)").arg(document->circuit.probes.size())});
    for (auto *group : {parts, probes})
    {
        group->setFlags(group->flags() & ~Qt::ItemIsSelectable);
        QFont font = group->font(0);
        font.setBold(true);
        group->setFont(0, font);
    }
    for (const auto &part : document->circuit.parts)
    {
        const bool scalar = part.kind == "R" || part.kind == "C" || part.kind == "CP" || part.kind == "V" ||
                            part.kind == "PULSE";
        auto *item = new QTreeWidgetItem(
            parts, {part.id, scalar ? quantity(part.value, part.kind) : spec(part.kind).name});
        item->setData(0, Qt::UserRole, "part");
        item->setData(0, Qt::UserRole + 1, part.id);
        item->setToolTip(0, part.id + " · " + spec(part.kind).name);
        item->setToolTip(1, item->text(1));
        item->setSelected(previous.parts.contains(part.id));
    }
    for (auto it = document->circuit.probes.cbegin(); it != document->circuit.probes.cend(); ++it)
    {
        const auto *part = document->circuit.part(it.value().part);
        const QString pin =
            part ? part->id + " · " + spec(part->kind).pins.value(it.value().index) : it.value().key();
        auto *item = new QTreeWidgetItem(probes, {it.key(), pin});
        item->setData(0, Qt::UserRole, "probe");
        item->setData(0, Qt::UserRole + 1, it.key());
        item->setToolTip(0, "Voltage probe " + it.key() + " · " + pin);
        item->setToolTip(1, pin);
        item->setSelected(previous.probes.contains(it.key()));
    }
    parts->setExpanded(partsExpanded);
    probes->setExpanded(probesExpanded);
}
void CircuitTree::deleteSelection()
{
    const auto selected = selection();
    if (document->editsLocked || selected.empty())
        return;
    document->edit(
        "Delete circuit items",
        [selected](Circuit &c)
        {
            c.wires.removeIf(
                [&](const Wire &w)
                { return selected.parts.contains(w.a.part) || selected.parts.contains(w.b.part); });
            c.labels.removeIf([&](const NetLabel &label) { return selected.parts.contains(label.pin.part); });
            c.parts.removeIf([&](const Part &part) { return selected.parts.contains(part.id); });
            for (auto it = c.probes.begin(); it != c.probes.end();)
                if (selected.probes.contains(it.key()) || selected.parts.contains(it.value().part))
                    it = c.probes.erase(it);
                else
                    ++it;
        });
}
void CircuitTree::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Delete)
    {
        deleteSelection();
        event->accept();
        return;
    }
    QTreeWidget::keyPressEvent(event);
}
} // namespace cmp
