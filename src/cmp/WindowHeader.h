#pragma once
#include <QWidget>
class QHBoxLayout;
class QToolButton;
namespace cmp
{
// Only unused header space initiates window movement; child controls retain their input.
class WindowHeader : public QWidget
{
    Q_OBJECT
  public:
    explicit WindowHeader(QWidget *parent = nullptr);
    static bool supportsExpandedTitleBar();
    QHBoxLayout *row() const
    {
        return row_;
    }
    void updateCaptionSpace();
    void installCaptionControls();
    QToolButton *maximizeButton() const
    {
        return maximize_;
    }
    void requestMaximizeRestore();

  protected:
    bool event(QEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void showEvent(QShowEvent *event) override;

  private:
    QHBoxLayout *row_;
    QWidget *controls_ = nullptr;
    QToolButton *maximize_ = nullptr;
};
} // namespace cmp
