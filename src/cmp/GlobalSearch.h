#pragma once
#include <QLineEdit>
#include <functional>
class QCompleter;
class QStandardItemModel;
class QTimer;
class QLabel;
namespace cmp
{
struct SearchEntry
{
    QString title, category, keywords;
    std::function<void()> activate;
    bool enabled = true;
};
class GlobalSearch final : public QLineEdit
{
    Q_OBJECT
  public:
    explicit GlobalSearch(QWidget *parent = nullptr);
    std::function<QVector<SearchEntry>()> entries;
    void refreshResults();

  protected:
    void resizeEvent(QResizeEvent *) override;
    void changeEvent(QEvent *) override;

  private:
    QLabel *shortcut = nullptr;
    void layoutShortcut();
    QCompleter *completion;
    QStandardItemModel *results;
    QTimer *debounce;
    QVector<SearchEntry> matches;
    void activateResult(const QModelIndex &);
};
} // namespace cmp
