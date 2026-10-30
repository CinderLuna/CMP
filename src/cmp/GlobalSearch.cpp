#include "cmp/GlobalSearch.h"
#include <QtWidgets>
#include <algorithm>

namespace cmp
{
namespace
{
class SearchCompleter final : public QCompleter
{
  public:
    using QCompleter::QCompleter;
    QString pathFromIndex(const QModelIndex &) const override
    {
        return qobject_cast<QLineEdit *>(widget())->text();
    }
};
} // namespace
GlobalSearch::GlobalSearch(QWidget *parent) : QLineEdit(parent)
{
    setObjectName("globalSearch");
    setPlaceholderText("Search CMP…");
    shortcut = new QLabel("Ctrl+K", this);
    shortcut->setObjectName("globalSearchShortcut");
    shortcut->setAttribute(Qt::WA_TransparentForMouseEvents);
    shortcut->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    setAccessibleName("Global search");
    setToolTip("Search commands, components, circuit parts and documentation · Ctrl+K");
    setClearButtonEnabled(true);
    results = new QStandardItemModel(this);
    results->setObjectName("globalSearchResults");
    completion = new SearchCompleter(results, this);
    completion->setCompletionMode(QCompleter::UnfilteredPopupCompletion);
    completion->setMaxVisibleItems(8);
    completion->popup()->setMinimumWidth(400);
    setCompleter(completion);
    debounce = new QTimer(this);
    debounce->setSingleShot(true);
    debounce->setInterval(120);
    connect(this, &QLineEdit::textChanged, this,
            [this]
            {
                completion->popup()->hide();
                results->clear();
                matches.clear();
                layoutShortcut();
                debounce->start();
            });
    connect(debounce, &QTimer::timeout, this, &GlobalSearch::refreshResults);
    connect(completion, qOverload<const QModelIndex &>(&QCompleter::activated), this,
            &GlobalSearch::activateResult);
    connect(this, &QLineEdit::returnPressed, this,
            [this]
            {
                if (!completion->popup()->isVisible() && !text().trimmed().isEmpty())
                {
                    refreshResults();
                    activateResult(results->index(0, 0));
                }
            });
    layoutShortcut();
}
void GlobalSearch::layoutShortcut()
{
    if (!shortcut)
        return;
    shortcut->ensurePolished();
    const int clearWidth =
        isClearButtonEnabled() && !text().isEmpty() ? style()->pixelMetric(QStyle::PM_SmallIconSize) + 8 : 0;
    const int width = shortcut->sizeHint().width();
    shortcut->setGeometry(this->width() - 10 - clearWidth - width, 0, width, height());
    setTextMargins(0, 0, width + 10, 0);
}
void GlobalSearch::resizeEvent(QResizeEvent *event)
{
    QLineEdit::resizeEvent(event);
    layoutShortcut();
}
void GlobalSearch::changeEvent(QEvent *event)
{
    QLineEdit::changeEvent(event);
    if (event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange ||
        event->type() == QEvent::PaletteChange)
        layoutShortcut();
}
void GlobalSearch::refreshResults()
{
    debounce->stop();
    results->clear();
    matches.clear();
    const auto query = text().simplified();
    if (query.isEmpty() || !entries)
    {
        completion->popup()->hide();
        return;
    }
    const auto words = query.split(' ', Qt::SkipEmptyParts);
    auto candidates = entries();
    for (auto &entry : candidates)
    {
        const QString haystack = entry.title + ' ' + entry.category + ' ' + entry.keywords;
        bool match = true;
        for (const auto &word : words)
            match &= haystack.contains(word, Qt::CaseInsensitive);
        if (match)
            matches.append(std::move(entry));
    }
    auto rank = [&](const SearchEntry &entry)
    {
        return (entry.enabled ? 4 : 0) + (entry.title.startsWith(query, Qt::CaseInsensitive) ? 2 : 0) +
               (entry.title.contains(query, Qt::CaseInsensitive) ? 1 : 0);
    };
    std::stable_sort(matches.begin(), matches.end(),
                     [&](const auto &a, const auto &b) { return rank(a) > rank(b); });
    if (matches.size() > 30)
        matches.resize(30);
    for (const auto &entry : matches)
    {
        auto *item = new QStandardItem(entry.title + "\n" + entry.category);
        item->setEnabled(entry.enabled);
        item->setSizeHint(QSize(400, 48));
        results->appendRow(item);
    }
    if (matches.isEmpty())
    {
        auto *item = new QStandardItem("No matching commands, components, parts or documentation");
        item->setEnabled(false);
        results->appendRow(item);
    }
    if (hasFocus())
    {
        completion->setCompletionPrefix({});
        completion->complete();
    }
}
void GlobalSearch::activateResult(const QModelIndex &index)
{
    const int row = index.row();
    if (!index.isValid() || row < 0 || row >= matches.size() || !matches[row].enabled)
        return;
    auto action = matches[row].activate;
    completion->popup()->hide();
    debounce->stop();
    // QLineEdit's clear-button visibility also depends on textChanged. Let the
    // normal signal run, then cancel the search it schedules for the empty query.
    clear();
    debounce->stop();
    layoutShortcut();
    clearFocus();
    if (action)
        QTimer::singleShot(0, this, std::move(action));
}
} // namespace cmp
