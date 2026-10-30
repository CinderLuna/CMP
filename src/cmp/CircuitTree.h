#pragma once
#include "cmp/Circuit.h"
#include <QTreeWidget>
namespace cmp
{
struct CircuitTreeSelection
{
    QStringList parts, probes;
    bool empty() const
    {
        return parts.isEmpty() && probes.isEmpty();
    }
};
class CircuitTree final : public QTreeWidget
{
    Q_OBJECT
  public:
    explicit CircuitTree(Document *, QWidget *parent = nullptr);
    CircuitTreeSelection selection() const;
    void setSelectedParts(const QStringList &);
    void refresh();
    void deleteSelection();
  signals:
    void circuitSelectionChanged();
    void locatePin(cmp::Pin);

  protected:
    void keyPressEvent(QKeyEvent *) override;

  private:
    Document *document;
};
} // namespace cmp
