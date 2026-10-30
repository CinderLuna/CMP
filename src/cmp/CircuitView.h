#pragma once
#include "cmp/Circuit.h"
#include <QGraphicsView>
#include <optional>
namespace cmp
{
class CircuitView : public QGraphicsView
{
    Q_OBJECT
  public:
    CircuitView(Document *, bool physical, QWidget *p = nullptr);
    ~CircuitView() override;
    void rebuild();
    void fit();
    void setTool(QString);
    void place(QString);
    void deleteSelected();
    void rotateSelected();
    void selectPart(QString);
    void selectParts(const QStringList &);
    void setLedCurrents(const QMap<QString, double> &);
    void setSimulationTime(double);
    void setDeviceStates(const QMap<QString, double> &);
    void setLocked(bool);
    void refreshTheme();
  signals:
    void partSelected(QString);
    void notice(QString);
    void probePicked(cmp::Pin);
    void labelPicked(cmp::Pin);
    void toolRequested(QString);
    void contextRequested(QPointF);

  protected:
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    void keyReleaseEvent(QKeyEvent *) override;
    void focusOutEvent(QFocusEvent *) override;
    void leaveEvent(QEvent *) override;
    void contextMenuEvent(QContextMenuEvent *) override;
    void dragEnterEvent(QDragEnterEvent *) override;
    void dragMoveEvent(QDragMoveEvent *) override;
    void dropEvent(QDropEvent *) override;
    void wheelEvent(QWheelEvent *) override;
    void drawBackground(QPainter *, const QRectF &) override;
    void showEvent(QShowEvent *) override;
    virtual bool warpCursor(QPoint viewportPoint);
    virtual QPoint panCursorPosition(QMouseEvent *) const;

  private:
    Document *document;
    bool physical;
    bool locked = false;
    bool panning = false, spaceHeld = false, panMoved = false;
    Qt::MouseButton panButton = Qt::NoButton;
    QPoint lastPan, panStart;
    QString tool = "select", pending;
    std::optional<Pin> first;
    QGraphicsPathItem *preview = nullptr;
    void pinPicked(Pin);
    QString placeAt(const QString &, QPointF);
    void updatePointer();
    void panBy(QPoint);
    void wrapPanCursor(QPoint);
    QString hoveredNet, highlightedNet;
    int hoveredWire = -1;
    bool rebuilding = false;
    void highlightNet(QString);
    void updateWireHover(QPoint);
    friend class PartItem;
    friend class WireItem;
};
QPointF pinPosition(const Part &, int, bool physical);
} // namespace cmp
