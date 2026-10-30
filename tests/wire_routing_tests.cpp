#include "cmp/WireRouting.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTextStream>
#include <cmath>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QVector<QRectF> obstacles = {QRectF(-25, -30, 50, 60)};
    const auto path = cmp::routeWire({-120, 0}, {-90, 0}, {90, 0}, {120, 0}, obstacles, {}, "a");
    bool pass = path.front() == QPointF(-120, 0) && path.back() == QPointF(120, 0);
    for (int i = 1; i < path.size(); ++i)
    {
        const auto a = path[i - 1], b = path[i];
        pass &= a.x() == b.x() || a.y() == b.y();
        // Sample interiors as well as vertices: a straight line through a body is invalid.
        for (int step = 1; step < 100; ++step)
        {
            const auto point = a + (b - a) * (step / 100.);
            pass &= !obstacles[0].adjusted(.001, .001, -.001, -.001).contains(point);
        }
    }
    pass &= cmp::routeWire({-120, 0}, {-90, 0}, {90, 0}, {120, 0}, obstacles, {}, "a") == path;
    const cmp::WireRoute existing{QPolygonF{{-100, 0}, {100, 0}}, "a"};
    const auto separate = cmp::routeWire({-120, 0}, {-90, 0}, {90, 0}, {120, 0}, {}, {existing}, "b");
    bool separateLane = false;
    for (const auto &point : separate)
        separateLane |= std::abs(point.y()) >= 10;
    pass &= separateLane;
    const auto overlapping =
        cmp::routeWire({0, 0}, {0, 10}, {0, 20}, {0, 30}, {QRectF(-50, -50, 100, 100)}, {}, "a");
    pass &= !overlapping.isEmpty() && overlapping.front() == QPointF(0, 0) &&
            overlapping.back() == QPointF(0, 30);
    QVector<QRectF> dense;
    for (int i = 0; i < 400; ++i)
        dense << QRectF(100 + i * 11, 100 + i * 13, 8, 9);
    QElapsedTimer timer;
    timer.start();
    const auto bounded = cmp::routeWire({-120, 0}, {-90, 0}, {90, 0}, {120, 0}, dense, {}, "dense");
    pass &= !bounded.isEmpty() && bounded.front() == QPointF(-120, 0) && bounded.back() == QPointF(120, 0) &&
            timer.elapsed() < 3000;
    QTextStream(stdout)
        << "routing endpoints/orthogonality/body-clearance/determinism/lane-separation/fallback=" << pass
        << '\n';
    return pass ? 0 : 1;
}
