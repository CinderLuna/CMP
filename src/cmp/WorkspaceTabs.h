#pragma once
#include <QPointer>
#include <QTabWidget>
#include <functional>
class QFrame;
class QLabel;
class QMimeData;
namespace cmp
{
class WorkspaceTabs : public QTabWidget
{
    Q_OBJECT
  public:
    explicit WorkspaceTabs(QWidget *parent = nullptr);
    QMimeData *mimeDataForTab(int index) const;
    std::function<void(WorkspaceTabs *, QWidget *, WorkspaceTabs *, Qt::Edge)> dockRequested;
    std::function<bool(WorkspaceTabs *, QWidget *, WorkspaceTabs *, Qt::Edge)> canDock;
    void hideDropOverlay();

  protected:
    bool eventFilter(QObject *, QEvent *) override;
    void dragEnterEvent(QDragEnterEvent *) override;
    void dragMoveEvent(QDragMoveEvent *) override;
    void dragLeaveEvent(QDragLeaveEvent *) override;
    void dropEvent(QDropEvent *) override;
    void tabInserted(int) override;
    void resizeEvent(QResizeEvent *) override;

  private:
    QPoint start_;
    QPointer<QWidget> pressed_;
    QFrame *overlay_ = nullptr;
    QLabel *hint_ = nullptr;
    QLabel *icon_ = nullptr;
    Qt::Edge overlayEdge_ = static_cast<Qt::Edge>(0);
    Qt::Edge dropEdge(QPoint) const;
    bool routeDrag(QEvent *, QWidget *receiver);
    void showDropOverlay(Qt::Edge, bool, const QString &);
    void updateOverlayGeometry();
};
} // namespace cmp
