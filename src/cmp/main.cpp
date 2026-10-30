#include "cmp/MainWindow.h"
#include <QApplication>
#include <QDir>
#include <QTimer>
int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setApplicationName("CMP");
    app.setOrganizationName("Cinderluna");
    app.setApplicationVersion("0.1.0");
    app.setStyle("Fusion");
    app.setFont(QFont("Segoe UI", 10));
    cmp::MainWindow window;
    window.show();
    if (app.arguments().contains("--smoke"))
        QTimer::singleShot(500, &window, &cmp::MainWindow::smokeTest);
    else if (app.arguments().contains("--recovery-smoke"))
        QTimer::singleShot(100, &window, &cmp::MainWindow::recoveryTest);
    else if (app.arguments().contains("--close-running") || app.arguments().contains("--close-paused"))
        QTimer::singleShot(100, &window,
                           [&] { window.lifecycleTest(app.arguments().contains("--close-paused")); });
    else if (app.arguments().contains("--capture"))
        QTimer::singleShot(800, &window,
                           [&]
                           {
                               QDir().mkpath("work");
                               window.grab().save(app.arguments().contains("--example-lm339")
                                                      ? "work/lm339-example.png"
                                                      : "work/cmp-ui.png");
                               app.quit();
                           });
    return app.exec();
}
