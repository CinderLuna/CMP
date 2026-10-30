#pragma once
#include <QListWidget>
#include <QMimeData>
#include <QPointer>
namespace cmp
{
class ComponentLibrary;
class ComponentDragData final : public QMimeData
{
  public:
    QPointer<ComponentLibrary> source;
    QString kind;
};
QString draggedComponent(const QMimeData *, const QWidget *target);
class ComponentLibrary : public QListWidget
{
    Q_OBJECT
  public:
    explicit ComponentLibrary(QWidget *parent = nullptr);
    QMimeData *mimeDataForKind(const QString &kind);
    enum Roles
    {
        CategoryRole = Qt::UserRole + 10,
        HeaderRole,
        CollapsedRole,
        FilteredRole
    };
    void addCategory(const QString &);
    void filterComponents(const QString &);
    QSize gridSize() const
    {
        return cell;
    }
    void setGridSize(const QSize &size);
    void arrange();
  signals:
    void componentClicked(QString);
    void componentDragStarted();

  protected:
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    void resizeEvent(QResizeEvent *) override;

  private:
    QPoint pressPosition;
    QPersistentModelIndex pressedIndex;
    bool dragged = false;
    int columns = 0;
    bool arranging = false;
    QSize cell{88, 112};
    QString query;
};
} // namespace cmp
