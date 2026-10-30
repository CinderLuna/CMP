#include "cmp/WireRouting.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>

namespace cmp
{
namespace
{
constexpr double epsilon = .001;
double distance(QPointF a, QPointF b)
{
    return std::abs(a.x() - b.x()) + std::abs(a.y() - b.y());
}
bool blocked(QPointF a, QPointF b, const QVector<QRectF> &obstacles)
{
    for (const auto &r : obstacles)
        if (std::abs(a.y() - b.y()) < epsilon)
        {
            if (a.y() > r.top() + epsilon && a.y() < r.bottom() - epsilon &&
                qMax(a.x(), b.x()) > r.left() + epsilon && qMin(a.x(), b.x()) < r.right() - epsilon)
                return true;
        }
        else if (a.x() > r.left() + epsilon && a.x() < r.right() - epsilon &&
                 qMax(a.y(), b.y()) > r.top() + epsilon && qMin(a.y(), b.y()) < r.bottom() - epsilon)
            return true;
    return false;
}
double interference(QPointF a, QPointF b, const QVector<WireRoute> &existing, const QString &net)
{
    double penalty = 0;
    const bool horizontal = std::abs(a.y() - b.y()) < epsilon;
    for (const auto &route : existing)
        for (int i = 1; i < route.points.size(); ++i)
        {
            const QPointF c = route.points[i - 1], d = route.points[i];
            const bool otherHorizontal = std::abs(c.y() - d.y()) < epsilon;
            if (horizontal == otherHorizontal)
            {
                const double separation = horizontal ? std::abs(a.y() - c.y()) : std::abs(a.x() - c.x());
                const double overlap = horizontal ? qMin(qMax(a.x(), b.x()), qMax(c.x(), d.x())) -
                                                        qMax(qMin(a.x(), b.x()), qMin(c.x(), d.x()))
                                                  : qMin(qMax(a.y(), b.y()), qMax(c.y(), d.y())) -
                                                        qMax(qMin(a.y(), b.y()), qMin(c.y(), d.y()));
                if (route.net != net && separation < 10 && overlap > 0)
                    penalty += overlap * (10 - separation) * 4;
            }
            else if (route.net != net)
            {
                const QPointF crossing = horizontal ? QPointF(c.x(), a.y()) : QPointF(a.x(), c.y());
                if (distance(a, crossing) + distance(crossing, b) <= distance(a, b) + epsilon &&
                    distance(c, crossing) + distance(crossing, d) <= distance(c, d) + epsilon)
                    penalty += 24;
            }
        }
    return penalty;
}
QPolygonF simplify(const QPolygonF &points)
{
    QPolygonF result;
    for (auto point : points)
    {
        if (!result.isEmpty() && distance(result.back(), point) < epsilon)
            continue;
        if (result.size() >= 2)
        {
            const auto a = result[result.size() - 2], b = result.back();
            if ((std::abs(a.x() - b.x()) < epsilon && std::abs(b.x() - point.x()) < epsilon) ||
                (std::abs(a.y() - b.y()) < epsilon && std::abs(b.y() - point.y()) < epsilon))
            {
                // Collapse straight runs, but preserve a reversal at an exit stub.
                if (distance(a, b) + distance(b, point) <= distance(a, point) + epsilon)
                    result.removeLast();
            }
        }
        result.append(point);
    }
    return result;
}
} // namespace
QPolygonF routeWire(QPointF start, QPointF startExit, QPointF endExit, QPointF end,
                    const QVector<QRectF> &obstacles, const QVector<WireRoute> &existing, const QString &net)
{
    QVector<double> xs, ys;
    for (auto point : {startExit, endExit})
    {
        xs << point.x() - 12 << point.x() << point.x() + 12;
        ys << point.y() - 12 << point.y() << point.y() + 12;
    }
    for (const auto &r : obstacles)
    {
        xs << r.left() - 12 << r.left() << r.right() << r.right() + 12;
        ys << r.top() - 12 << r.top() << r.bottom() << r.bottom() + 12;
    }
    auto unique = [](QVector<double> &values)
    {
        std::sort(values.begin(), values.end());
        values.erase(std::unique(values.begin(), values.end(),
                                 [](double a, double b) { return std::abs(a - b) < epsilon; }),
                     values.end());
    };
    unique(xs);
    unique(ys);
    const int width = xs.size(), height = ys.size();
    auto fallback = [&]
    {
        // Overlapping components or extremely large scenes may have no free exit.
        // Keep the explicit terminal stubs even when a collision-free route is impossible.
        return simplify(QPolygonF{start, startExit, QPointF(startExit.x(), endExit.y()), endExit, end});
    };
    if (qint64(width) * height > 65536)
        return fallback();
    auto index = [](const QVector<double> &values, double value)
    { return int(std::lower_bound(values.cbegin(), values.cend(), value - epsilon) - values.cbegin()); };
    const int origin = index(ys, startExit.y()) * width + index(xs, startExit.x());
    const int target = index(ys, endExit.y()) * width + index(xs, endExit.x());
    const double infinity = std::numeric_limits<double>::infinity();
    QVector<double> cost(width * height * 3, infinity);
    QVector<int> previous(cost.size(), -1);
    // Cache edge obstruction and overlap costs across directional A* states.
    QVector<double> edges(width * height * 2, -1);
    struct Visit
    {
        double score, cost;
        int state;
        bool operator<(const Visit &other) const
        {
            return score > other.score;
        }
    };
    std::priority_queue<Visit> queue;
    cost[origin * 3] = 0;
    queue.push({distance(startExit, endExit), 0, origin * 3});
    int finish = -1;
    while (!queue.empty())
    {
        const auto visit = queue.top();
        queue.pop();
        if (visit.cost != cost[visit.state])
            continue;
        const int cell = visit.state / 3, direction = visit.state % 3;
        if (cell == target)
        {
            finish = visit.state;
            break;
        }
        const int x = cell % width, y = cell / width;
        const QPointF a(xs[x], ys[y]);
        for (auto offset : {QPoint(-1, 0), QPoint(1, 0), QPoint(0, -1), QPoint(0, 1)})
        {
            const int nx = x + offset.x(), ny = y + offset.y();
            if (nx < 0 || nx >= width || ny < 0 || ny >= height)
                continue;
            const int next = ny * width + nx, axis = offset.x() ? 1 : 2;
            const QPointF b(xs[nx], ys[ny]);
            if ((cell == origin && QPointF::dotProduct(b - a, startExit - start) < -epsilon) ||
                (next == target && QPointF::dotProduct(b - a, end - endExit) < -epsilon))
                continue;
            auto &edge = edges[qMin(cell, next) * 2 + axis - 1];
            if (edge < 0)
                edge =
                    blocked(a, b, obstacles) ? infinity : distance(a, b) + interference(a, b, existing, net);
            const double candidate = visit.cost + edge + (direction && direction != axis ? 20 : 0);
            const int state = next * 3 + axis;
            if (candidate < cost[state])
            {
                cost[state] = candidate;
                previous[state] = visit.state;
                queue.push({candidate + distance(b, endExit), candidate, state});
            }
        }
    }
    if (finish < 0)
        return fallback();
    QPolygonF middle;
    for (int state = finish; state >= 0; state = previous[state])
    {
        const int cell = state / 3;
        middle.prepend(QPointF(xs[cell % width], ys[cell / width]));
    }
    QPolygonF result;
    result << start;
    result += middle;
    result << end;
    return simplify(result);
}
} // namespace cmp
