#pragma once
#include <QMainWindow>
#include <memory>
class QMenuBar;
class QLineEdit;
namespace cmp
{
class WindowHeader;
class MainWindow : public QMainWindow
{
  public:
    MainWindow();
    ~MainWindow() override;
    void smokeTest();
    void lifecycleTest(bool paused);
    void recoveryTest();

  protected:
    bool nativeEvent(const QByteArray &, void *, qintptr *) override;
    void showEvent(QShowEvent *) override;
    void changeEvent(QEvent *) override;
    void closeEvent(QCloseEvent *) override;

  private:
    void syncWindowsChrome();
    void setupGlobalSearch();
    void showDocumentation(const QString &path, const QString &query = {});
    void applyAppearance();
    void setAppearance(const QString &);
    QString appearance_ = "system";
    bool extendedHeader_ = false;
    WindowHeader *header_ = nullptr;
    QMenuBar *appMenu_ = nullptr;
    QLineEdit *search_ = nullptr;
    struct State;
    std::unique_ptr<State> state;
};
} // namespace cmp
