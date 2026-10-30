#pragma once
#include <QSizeGrip>
namespace cmp
{
class WindowSizeGrip final : public QSizeGrip
{
  public:
    explicit WindowSizeGrip(QWidget *parent = nullptr);

  protected:
    void paintEvent(QPaintEvent *) override;
};
} // namespace cmp
