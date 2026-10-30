#include "cmp/CircuitView.h"
#include "cmp/ComponentLibrary.h"
#include "cmp/ExpandedLibrary.h"
#include "cmp/PackageArtwork.h"
#include "cmp/ResistorBands.h"
#include "cmp/Theme.h"
#include "cmp/WireRouting.h"
#include <QtWidgets>
#include <algorithm>
#include <limits>
#include <optional>
namespace cmp
{
static const QJsonObject &physicalArtwork()
{
    static const QJsonObject data = []
    {
        QFile f(":/cmp/physical-pins.json");
        f.open(QIODevice::ReadOnly);
        return QJsonDocument::fromJson(f.readAll()).object();
    }();
    return data;
}
static bool packageIC(const Part &p)
{
    return p.kind == "555" || p.kind == "LM339";
}
static QRectF schematicBody(const Part &p)
{
    if (expanded(p.kind))
        return expandedBody(p, false);
    return p.kind == "LM339" ? QRectF(-95, -105, 190, 210)
           : p.kind == "555" ? QRectF(-65, -60, 130, 120)
                             : QRectF(-30, -30, 60, 60);
}
static QRectF artworkRect(const QString &kind = {})
{
    if (expanded(kind))
    {
        Part p;
        p.kind = kind;
        return expandedBody(p, true).adjusted(-20, -20, 20, 20);
    }
    const auto custom = physicalArtwork()["artworkRects"].toObject();
    const auto a = (custom.contains(kind) ? custom[kind] : physicalArtwork()["artworkRect"]).toArray();
    return {a[0].toDouble(), a[1].toDouble(), a[2].toDouble(), a[3].toDouble()};
}
static QPointF localPin(const Part &p, int i, bool physical)
{
    if (expanded(p.kind))
        return expandedPin(p, i, physical);
    if (physical)
    {
        const auto a = physicalArtwork()["pins"].toObject()[p.kind].toArray()[i].toArray();
        const auto r = artworkRect(p.kind);
        return {r.left() + a[0].toDouble() * r.width(), r.top() + a[1].toDouble() * r.height()};
    }
    if (p.kind == "GND")
        return {0, -28};
    if (p.kind == "LM339")
        return i < 7 ? QPointF(-115, -90 + i * 30) : QPointF(115, 90 - (i - 7) * 30);
    if (p.kind == "555")
    {
        return i < 4 ? QPointF(-85, -45 + i * 30) : QPointF(85, 45 - (i - 4) * 30);
    }
    if (p.kind == "NPN")
        return (i == 0 ? QPointF(35, -40) : i == 1 ? QPointF(-45, 0) : QPointF(35, 40));
    return {0, i == 0 ? -48. : 48.};
}
QPointF pinPosition(const Part &p, int i, bool physical)
{
    QTransform t;
    t.rotate(p.rotation);
    return p.position + t.map(localPin(p, i, physical));
}
static QString componentCaption(const Part &part)
{
    QString text = part.id;
    if (part.kind == "R" || part.kind == "C" || part.kind == "CP" || part.kind == "V" || part.kind == "PULSE")
        text += " · " + quantity(part.value, part.kind);
    else if (packageIC(part))
        text += part.kind == "555" ? " · NE555" : " · LM339";
    else if (expanded(part.kind))
        text += " · " +
                (!spec(part.kind).unit.isEmpty() ? quantity(part.value, part.kind) : spec(part.kind).name);
    return text;
}
static QRectF referenceRect(const Part &part)
{
    const double width = QFontMetricsF(QFont("Segoe UI", 9)).horizontalAdvance(componentCaption(part)) + 2;
    if (expanded(part.kind))
        return spec(part.kind).pins.size() > 3
                   ? QRectF(-width / 2, expandedBody(part, false).top() - 28, width, 18)
                   : QRectF(spec(part.kind).pins.size() == 3 ? 72 : 36, -9, width, 18);
    if (packageIC(part))
        return {-width / 2, part.kind == "LM339" ? -136. : -90., width, 18};
    const double x = part.kind == "R" ? 20 : part.kind == "NPN" || part.kind == "LED" ? 58 : 36;
    return {x, -9, width, 18};
}
static QRectF pinLabelRect(const Part &part, int index)
{
    const auto pin = localPin(part, index, false);
    if (expanded(part.kind) && spec(part.kind).pins.size() > 3)
        return {pin.x() < 0 ? pin.x() + 24 : pin.x() - 90, pin.y() - 9, 66, 18};
    if (part.kind == "LM339")
        return {index < 7 ? -87. : 17., pin.y() - 9, 70, 18};
    if (part.kind == "555")
        return {index < 4 ? -57. : 5., pin.y() - 9, 52, 18};
    const double width =
        QFontMetricsF(QFont("Segoe UI", 8)).horizontalAdvance(spec(part.kind).pins[index]) + 2;
    if (expanded(part.kind) && spec(part.kind).pins.size() <= 3 && pin.x() != 0)
        return {pin.x() - width / 2, pin.y() - 24, width, 16};
    if (part.kind == "NPN" && index == 1)
        return {pin.x() - width / 2, pin.y() - 24, width, 16};
    return {pin.x() + 9, pin.y() - 8, width, 16};
}
static QRectF sceneRectFor(const Part &part, QRectF local)
{
    QTransform transform;
    transform.translate(part.position.x(), part.position.y());
    transform.rotate(part.rotation);
    return transform.mapRect(local);
}
static QRectF bodyRect(const Part &part, bool physical)
{
    QRectF rect = physical ? artworkRect(part.kind) : schematicBody(part);
    QTransform transform;
    transform.translate(part.position.x(), part.position.y());
    transform.rotate(part.rotation);
    return transform.mapRect(rect).adjusted(-10, -10, 10, 10);
}
static QPointF pinExit(const Part &part, int index, bool physical)
{
    QPointF pin = localPin(part, index, physical), exit = pin;
    if (part.kind == "BOARD")
    {
        QTransform t;
        t.rotate(part.rotation);
        return part.position + t.map(pin + QPointF(0, 8));
    }
    if (expanded(part.kind))
    {
        const auto body = expandedBody(part, physical);
        if (physical && pin.y() > body.bottom() && std::abs(pin.x()) <= body.width() / 2)
            exit.setY(body.bottom() + 40);
        else if (spec(part.kind).pins.size() > 3 || pin.x() != 0)
            exit.setX(pin.x() < 0 ? body.left() - 40 : body.right() + 40);
        else
            exit.setY(pin.y() < 0 ? body.top() - 40 : body.bottom() + 40);
    }
    else if (part.kind == "LM339")
        exit.setX((index < 7 ? -1 : 1) * ((physical ? 65 : 140) + (index < 7 ? index : 13 - index) * 10));
    else if (!physical && part.kind == "555")
        exit.setX((index < 4 ? -1 : 1) * (110 + (index < 4 ? index : 7 - index) * 10));
    else if (!physical && part.kind == "NPN" && index == 1)
        exit.setX(-68);
    else
    {
        int rank = physical && part.kind == "555" ? (index < 4 ? index : 7 - index) : index;
        const bool top = part.kind == "GND" || pin.y() < 0;
        exit.setY((top ? -1 : 1) * (65 + rank * 10));
    }
    QTransform transform;
    transform.rotate(part.rotation);
    return part.position + transform.map(exit);
}
static QHash<QString, QColor> netColors(const Circuit &circuit)
{
    const auto map = circuit.nodes();
    auto nets = map.values();
    nets.removeDuplicates();
    nets.sort();
    QHash<QString, QColor> colors;
    int index = 0;
    for (const auto &net : nets)
        colors[net] = net == "0" ? theme().ground : theme().wires[index++ % theme().wires.size()];
    for (const auto &part : circuit.parts)
        if (part.kind == "V" && map.value(Pin{part.id, 0}.key()) != "0")
        {
            colors[map.value(Pin{part.id, 0}.key())] = theme().supply;
            break;
        }
    return colors;
}
class WireItem final : public QGraphicsPathItem
{
  public:
    CircuitView *view;
    QString net;
    QColor color;
    QPointF from, to;
    WireItem(const QPolygonF &points, CircuitView *canvas, QString node, QColor ink, QString description,
             int index)
        : view(canvas), net(std::move(node)), color(ink), from(points.front()), to(points.back())
    {
        QPainterPath path(from);
        for (int i = 1; i < points.size(); ++i)
            path.lineTo(points[i]);
        setPath(path);
        setPen(QPen(color, 2));
        setFlags(ItemIsSelectable);
        setData(1, index);
        setData(2, net);
        setToolTip(description);
        setZValue(-1);
    }
    QRectF boundingRect() const override
    {
        return path().boundingRect().adjusted(-8, -8, 8, 8);
    }
    QPainterPath shape() const override
    {
        QPainterPathStroker stroke;
        stroke.setWidth(10);
        return stroke.createStroke(path());
    }
    void paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *) override
    {
        const bool active = view->highlightedNet == net || isSelected();
        // A background halo makes a crossing read as an overpass rather than a junction.
        p->setPen(QPen(theme().canvas, active ? 7 : 5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p->drawPath(path());
        p->setPen(QPen(color, pen().widthF(), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p->drawPath(path());
        if (active)
        {
            p->setBrush(Qt::NoBrush);
            p->setPen(QPen(color, 2));
            p->drawEllipse(from, 6, 6);
            p->drawEllipse(to, 6, 6);
        }
    }
};
class AnnotationItem final : public QGraphicsTextItem
{
  public:
    explicit AnnotationItem(const QString &text) : QGraphicsTextItem(text)
    {
        setAcceptedMouseButtons(Qt::NoButton);
        setZValue(2);
    }
    void paint(QPainter *p, const QStyleOptionGraphicsItem *option, QWidget *widget) override
    {
        p->fillRect(boundingRect(), theme().canvas);
        QGraphicsTextItem::paint(p, option, widget);
    }
};
static QPointF annotationPosition(QPointF exit, QSizeF size, const QVector<QRectF> &occupied)
{
    QPointF fallback = exit + QPointF(12, 8);
    double bestPenalty = std::numeric_limits<double>::infinity();
    for (double gap : {12., 28., 48., 72., 110.})
        for (const auto &offset :
             {QPointF(gap, -size.height() - gap), QPointF(gap, gap),
              QPointF(-size.width() - gap, -size.height() - gap), QPointF(-size.width() - gap, gap),
              QPointF(-size.width() / 2, -size.height() - gap), QPointF(-size.width() / 2, gap)})
        {
            const QPointF position = exit + offset;
            const QRectF rect = QRectF(position, size).adjusted(-4, -4, 4, 4);
            double penalty = 0;
            for (const auto &other : occupied)
            {
                const auto intersection = rect.intersected(other);
                penalty += intersection.width() * intersection.height();
            }
            if (penalty == 0)
                return position;
            if (penalty < bestPenalty)
            {
                bestPenalty = penalty;
                fallback = position;
            }
        }
    return fallback;
}
class PartItem : public QGraphicsObject
{
  public:
    Part part;
    CircuitView *view;
    bool lit = false;
    double ledCurrent = 0, simulationTime = 0;
    QMap<QString, double> deviceStates;
    PartItem(Part p, CircuitView *v) : part(p), view(v)
    {
        setPos(p.position);
        if (p.kind == "BOARD")
            setZValue(-3);
        setRotation(p.rotation);
        setFlags(ItemIsMovable | ItemIsSelectable | ItemSendsGeometryChanges);
        setData(0, p.id);
        setToolTip(p.id + " · " + spec(p.kind).name);
        if (p.kind == "R")
            setToolTip(p.id + " · " + quantity(p.value, "R") + "\n" + resistorBandDescription(p.value));
    }
    QRectF boundingRect() const override
    {
        QRectF bounds = part.kind == "555"
                            ? QRectF(-116, view->physical ? -120 : -78, 232, view->physical ? 210 : 156)
                            : QRectF(-70, -72, 140, 154);
        if (part.kind == "LM339")
            bounds = QRectF(-150, -145, 300, 290);
        if (expanded(part.kind))
        {
            bounds = expandedBody(part, view->physical).adjusted(-75, -45, 75, 45);
            for (int pin = 0; pin < spec(part.kind).pins.size(); ++pin)
                bounds = bounds.united(
                    QRectF(localPin(part, pin, view->physical) - QPointF(25, 25), QSizeF(50, 50)));
        }
        if (!view->physical)
        {
            bounds = bounds.united(referenceRect(part).adjusted(-4, -4, 4, 4));
            for (int index = 0; index < spec(part.kind).pins.size(); ++index)
                bounds = bounds.united(pinLabelRect(part, index).adjusted(-2, -2, 2, 2));
        }
        return bounds;
    }
    QPainterPath shape() const override
    {
        QPainterPath path;
        path.setFillRule(Qt::WindingFill);
        if (part.kind == "BOARD")
        {
            path.addRect(expandedBody(part, view->physical));
            return path;
        }
        path.addRect(view->physical ? artworkRect(part.kind) : schematicBody(part));
        for (int i = 0; i < spec(part.kind).pins.size(); ++i)
            path.addEllipse(localPin(part, i, view->physical), 18, 18);
        return path.simplified();
    }
    QRectF selectionBody() const
    {
        if (expanded(part.kind))
            return expandedBody(part, view->physical);
        if (view->physical)
        {
            static const QHash<QString, QRectF> bodies = {
                {"R", {-11, -22, 19, 44}},   {"C", {-24, -25, 48, 45}},   {"CP", {-19, -35, 38, 57}},
                {"GND", {-27, -26, 54, 55}}, {"V", {-39, -20, 78, 44}},   {"PULSE", {-39, -20, 78, 44}},
                {"D", {-10, -18, 20, 36}},   {"LED", {-17, -35, 34, 41}}, {"NPN", {-18, -33, 36, 31}},
                {"555", {-35, -20, 70, 40}}};
            if (part.kind == "LM339")
                return dipBodyRect(14);
            return bodies.value(part.kind, artworkRect(part.kind));
        }
        if (packageIC(part))
            return schematicBody(part);
        if (part.kind == "R")
            return {-8, -25, 16, 50};
        if (part.kind == "C" || part.kind == "CP")
            return {-22, -5, 44, 10};
        if (part.kind == "GND")
            return {-20, -28, 40, 44};
        if (part.kind == "V" || part.kind == "PULSE")
            return {-25, -25, 50, 50};
        if (part.kind == "NPN")
            return {-12, -40, 50, 80};
        return {-19, -15, 38, 30};
    }
    void paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *) override
    {
        p->setRenderHint(QPainter::Antialiasing);
        QColor ink = theme().text;
        p->setPen(QPen(ink, 1.7));
        p->setBrush(theme().base);
        if (isSelected())
        {
            p->save();
            const QRectF body = selectionBody().adjusted(-5, -5, 5, 5);
            const double length = qMin(8., qMin(body.width(), body.height()) * .3);
            QPen pen(theme().accent, 2, Qt::SolidLine, Qt::SquareCap);
            pen.setCosmetic(true);
            p->setPen(pen);
            p->setBrush(Qt::NoBrush);
            for (const auto &corner :
                 {body.topLeft(), body.topRight(), body.bottomLeft(), body.bottomRight()})
            {
                const double dx = corner.x() == body.left() ? length : -length;
                const double dy = corner.y() == body.top() ? length : -length;
                p->drawPolyline(QPolygonF{corner + QPointF(dx, 0), corner, corner + QPointF(0, dy)});
            }
            p->restore();
        }
        if (expanded(part.kind))
        {
            Part display = part;
            for (auto i = deviceStates.cbegin(); i != deviceStates.cend(); ++i)
                display.parameters["__view_" + i.key()] = i.value();
            if (spec(part.kind).behavior == "switch" && parameter(part, "scheduled") &&
                simulationTime >= parameter(part, "on") && simulationTime < parameter(part, "off"))
                display.parameters["state"] = 1 - parameter(part, "state");
            paintExpanded(*p, display, view->physical, ledCurrent);
        }
        else if (view->physical)
        {
            static const QPixmap atlas(":/cmp/components.png");
            int i = spec(part.kind).sprite;
            int x0 = (i % 5) * atlas.width() / 5, x1 = (i % 5 + 1) * atlas.width() / 5,
                y0 = (i / 5) * atlas.height() / 2, y1 = (i / 5 + 1) * atlas.height() / 2;
            if (part.kind == "LED" && !lit)
                p->setOpacity(.45);
            if (part.kind == "LM339")
                paintDipPackage(*p, "LM339", 14);
            else if (part.kind == "R")
                paintResistor(*p, part.value);
            else if (part.kind == "GND")
            {
                static QHash<QString, QPixmap> icons;
                const QString key = theme().text.name();
                if (!icons.contains(key))
                    icons[key] = tintedPixmap(atlas.copy(x0, y0, x1 - x0, y1 - y0), theme().text);
                p->drawPixmap(artworkRect(), icons[key], QRectF(icons[key].rect()));
            }
            else
                p->drawPixmap(artworkRect(), atlas, QRectF(x0, y0, x1 - x0, y1 - y0));
            p->setOpacity(1);
        }
        else if (packageIC(part))
        {
            p->setBrush(theme().panel);
            const auto body = schematicBody(part);
            const int count = spec(part.kind).pins.size();
            p->drawRect(body);
            for (int index = 0; index < count; ++index)
            {
                const auto pin = localPin(part, index, false);
                p->drawLine(pin, QPointF(index < count / 2 ? body.left() : body.right(), pin.y()));
            }
            p->setFont(QFont("Segoe UI", 8));
            p->save();
            if (part.kind == "LM339")
                p->rotate(-90);
            p->drawText(QRectF(-32, -9, 64, 18), Qt::AlignCenter, part.kind == "555" ? "555" : "LM339");
            p->restore();
        }
        else if (part.kind == "R")
        {
            p->drawLine(0, -48, 0, -25);
            p->drawRect(-8, -25, 16, 50);
            p->drawLine(0, 25, 0, 48);
        }
        else if (part.kind == "C" || part.kind == "CP")
        {
            p->drawLine(0, -48, 0, -5);
            p->drawLine(-22, -5, 22, -5);
            p->drawLine(-22, 5, 22, 5);
            p->drawLine(0, 5, 0, 48);
            if (part.kind == "CP")
                p->drawText(-35, -10, "+");
        }
        else if (part.kind == "GND")
        {
            p->drawLine(0, -28, 0, 0);
            p->drawLine(-20, 0, 20, 0);
            p->drawLine(-13, 7, 13, 7);
            p->drawLine(-6, 14, 6, 14);
        }
        else if (part.kind == "V" || part.kind == "PULSE")
        {
            p->drawLine(0, -48, 0, -25);
            p->drawEllipse(QRectF(-25, -25, 50, 50));
            p->drawText(QRectF(-20, -25, 40, 50), Qt::AlignCenter, part.kind == "V" ? "+\n−" : "∿");
            p->drawLine(0, 25, 0, 48);
        }
        else if (part.kind == "NPN")
        {
            p->drawLine(-45, 0, -8, 0);
            p->drawLine(-8, -20, -8, 20);
            p->drawLine(-8, -12, 35, -40);
            p->drawLine(-8, 12, 35, 40);
            p->drawText(18, 29, "›");
        }
        else
        {
            p->drawLine(0, -48, 0, -15);
            QPolygonF tri;
            tri << QPointF(-17, -15) << QPointF(17, -15) << QPointF(0, 15);
            p->drawPolygon(tri);
            p->drawLine(-19, 15, 19, 15);
            p->drawLine(0, 15, 0, 48);
            if (part.kind == "LED")
                p->drawText(23, -8, lit ? "↗ ↗" : "↗");
        }
        p->setFont(QFont("Segoe UI", 8));
        for (int i = 0; i < spec(part.kind).pins.size(); i++)
        {
            QPointF pos = localPin(part, i, view->physical);
            p->setPen(QPen(theme().accent, 1.4));
            p->setBrush(theme().base);
            const qreal radius = part.kind == "BOARD" ? 1.5 : view->physical ? 3 : 4;
            p->drawEllipse(pos, radius, radius);
            p->setPen(ink);
            QString label = spec(part.kind).pins[i];
            if (part.kind == "BOARD")
                continue;
            if (!view->physical)
            {
                const auto rect = pinLabelRect(part, i);
                if (!packageIC(part) && part.kind != "BOARD")
                    p->fillRect(rect.adjusted(-2, -1, 2, 1), theme().canvas);
                p->drawText(rect,
                            Qt::AlignVCenter |
                                (packageIC(part) && i >= spec(part.kind).pins.size() / 2 ? Qt::AlignRight
                                                                                         : Qt::AlignLeft),
                            label);
            }
            else if (part.kind == "LM339")
            {
                p->setPen(QColor("#eef2f6"));
                p->drawText(QRectF(i < 7 ? -20 : 7, pos.y() - 7, 13, 14), Qt::AlignCenter,
                            QString::number(i + 1));
            }
            else if (part.kind == "555" && view->physical)
                p->drawText(QRectF(pos.x() + 4, pos.y() + (pos.y() < 0 ? -17 : 4), 9, 14), Qt::AlignLeft,
                            label.section(' ', 0, 0));
            else
                p->drawText(QRectF(pos.x() - 24, pos.y() + (pos.y() < 0 ? -18 : 5), 48, 17), Qt::AlignCenter,
                            label.section(' ', 0, 0));
        }
        p->setFont(QFont("Segoe UI", 9));
        p->setPen(ink);
        const QString text = componentCaption(part);
        if (!view->physical)
        {
            const auto rect = referenceRect(part);
            p->fillRect(rect.adjusted(-2, -2, 2, 2), theme().canvas);
            p->drawText(rect, Qt::AlignVCenter | (packageIC(part) ? Qt::AlignHCenter : Qt::AlignLeft), text);
        }
        else
            p->drawText(QRectF(-110,
                               part.kind == "LM339" ? -112
                               : part.kind == "555" ? -116
                                                    : -76,
                               220, 18),
                        Qt::AlignCenter, text);
    }

  protected:
    void mousePressEvent(QGraphicsSceneMouseEvent *e) override
    {
        if (view->tool == "wire" || view->tool == "probe" || view->tool == "label")
        {
            int nearest = -1;
            double distance = 18;
            for (int i = 0; i < spec(part.kind).pins.size(); i++)
            {
                double d = QLineF(e->pos(), localPin(part, i, view->physical)).length();
                if (d < distance)
                {
                    nearest = i;
                    distance = d;
                }
            }
            if (nearest >= 0)
            {
                auto *canvas = view;
                Pin pin{part.id, nearest};
                // A document edit rebuilds scene items. Defer it until this mouse event has returned.
                QTimer::singleShot(0, canvas, [canvas, pin] { canvas->pinPicked(pin); });
                e->accept();
                return;
            }
            e->accept();
            return;
        }
        QGraphicsObject::mousePressEvent(e);
    }
    void mouseReleaseEvent(QGraphicsSceneMouseEvent *e) override
    {
        QGraphicsObject::mouseReleaseEvent(e);
        auto id = part.id;
        QPointF point(qRound(pos().x() / 10) * 10, qRound(pos().y() / 10) * 10);
        if (point != part.position)
        {
            auto *doc = view->document;
            QTimer::singleShot(0, doc,
                               [doc, id, point]
                               {
                                   doc->edit("Move " + id,
                                             [=](Circuit &c)
                                             {
                                                 if (auto *p = c.part(id))
                                                 {
                                                     Part next = *p;
                                                     next.position = point;
                                                     p->position = snappedToBoard(c, next);
                                                 }
                                             });
                               });
        }
    }
};
CircuitView::CircuitView(Document *d, bool physical, QWidget *parent)
    : QGraphicsView(parent), document(d), physical(physical)
{
    setScene(new QGraphicsScene(this));
    setFrameShape(QFrame::NoFrame);
    setRenderHint(QPainter::Antialiasing);
    setBackgroundBrush(theme().canvas);
    setDragMode(RubberBandDrag);
    setTransformationAnchor(AnchorUnderMouse);
    setAcceptDrops(true);
    setMouseTracking(true);
    connect(d, &Document::changed, this, &CircuitView::rebuild);
    connect(scene(), &QGraphicsScene::selectionChanged, this,
            [this]
            {
                highlightNet(hoveredNet);
                for (auto *i : scene()->selectedItems())
                    if (auto *p = dynamic_cast<PartItem *>(i))
                    {
                        emit partSelected(p->part.id);
                        return;
                    }
                emit partSelected({});
            });
    rebuild();
}
CircuitView::~CircuitView()
{
    disconnect(scene(), nullptr, this, nullptr);
}
void CircuitView::highlightNet(QString net)
{
    if (rebuilding)
        return;
    if (net.isEmpty())
        for (auto *item : scene()->selectedItems())
            if (auto *wire = dynamic_cast<WireItem *>(item))
            {
                net = wire->net;
                break;
            }
    highlightedNet = net;
    for (auto *item : scene()->items())
        if (item->data(2).isValid())
        {
            const bool active = item->data(2).toString() == net;
            item->setOpacity(net.isEmpty() || active ? 1 : .25);
            if (auto *wire = dynamic_cast<WireItem *>(item))
                wire->setPen(QPen(wire->color, active ? 3.2 : 2));
            item->update();
        }
}
void CircuitView::updateWireHover(QPoint point)
{
    WireItem *hit = nullptr;
    if (!panning && viewport()->rect().contains(point))
        for (auto *item : items(point))
        {
            if ((hit = dynamic_cast<WireItem *>(item)))
                break;
            // Non-interactive junction dots do not hide the wire underneath.
            if (item->acceptedMouseButtons() != Qt::NoButton || item->acceptHoverEvents())
                break;
        }
    const int index = hit ? hit->data(1).toInt() : -1;
    const QString net = hit ? hit->net : QString();
    if (index == hoveredWire && net == hoveredNet)
        return;
    hoveredWire = index;
    hoveredNet = net;
    highlightNet(net);
    if (hit)
        emit notice(hit->toolTip());
}
void CircuitView::rebuild()
{
    QScopedValueRollback<bool> rebuildingGuard(rebuilding, true);
    hoveredNet.clear();
    hoveredWire = -1;
    highlightedNet.clear();
    QString selected;
    for (auto *i : scene()->selectedItems())
        if (auto *p = dynamic_cast<PartItem *>(i))
            selected = p->part.id;
    scene()->clear();
    preview = nullptr;
    first.reset();
    auto map = document->circuit.nodes();
    const auto colors = netColors(document->circuit);
    QVector<QRectF> obstacles;
    for (const auto &part : document->circuit.parts)
    {
        if (part.kind != "BOARD")
            obstacles.append(bodyRect(part, physical));
        if (!physical)
        {
            obstacles.append(sceneRectFor(part, referenceRect(part)).adjusted(-5, -5, 5, 5));
            if (!packageIC(part))
                for (int pin = 0; pin < spec(part.kind).pins.size(); ++pin)
                    obstacles.append(sceneRectFor(part, pinLabelRect(part, pin)).adjusted(-3, -3, 3, 3));
        }
    }
    QVector<QRectF> annotationZones = obstacles;
    if (physical)
        for (const auto &part : document->circuit.parts)
            if (part.kind == "LM339" || expanded(part.kind))
            {
                annotationZones.append(
                    sceneRectFor(part, QRectF(-110, -112, 220, 18)).adjusted(-4, -4, 4, 4));
                for (int index = 0; index < spec(part.kind).pins.size(); ++index)
                    annotationZones.append(QRectF(pinPosition(part, index, true), pinExit(part, index, true))
                                               .normalized()
                                               .adjusted(-4, -4, 4, 4));
            }

    if (!physical)
        for (const auto &part : document->circuit.parts)
            for (int index = 0; index < spec(part.kind).pins.size(); ++index)
                annotationZones.append(QRectF(pinPosition(part, index, false), pinExit(part, index, false))
                                           .normalized()
                                           .adjusted(-4, -4, 4, 4));
    auto annotation = [&](const Part &part, int pin, const QString &text, QFont font, const QString &role,
                          QPointF physicalOffset)
    {
        QGraphicsTextItem *item;
        const bool routed = !physical || part.kind == "LM339" || expanded(part.kind);
        if (!routed)
            item = scene()->addText(text, font);
        else
        {
            item = new AnnotationItem(text);
            item->setFont(font);
            scene()->addItem(item);
        }
        item->setData(5, role);
        item->setData(6, Pin{part.id, pin}.key());
        item->setDefaultTextColor(role == "probe" ? theme().probe : theme().accent);
        item->setZValue(2);
        if (!routed)
            item->setPos(pinPosition(part, pin, true) + physicalOffset);
        else
        {
            item->setPos(annotationPosition(pinPosition(part, pin, physical), item->boundingRect().size(),
                                            annotationZones));
            const auto reserved = item->sceneBoundingRect().adjusted(-4, -4, 4, 4);
            obstacles.append(reserved);
            annotationZones.append(reserved);
        }
        item->setToolTip(text + " · " + part.id + " pin " + spec(part.kind).pins.value(pin));
    };
    for (auto it = document->circuit.probes.cbegin(); it != document->circuit.probes.cend(); ++it)
        if (const auto *part = document->circuit.part(it.value().part))
        {
            const QString endpoint =
                part->id + "." + spec(part->kind).pins.value(it.value().index).section(' ', 0, 0);
            annotation(*part, it.value().index,
                       "● " + it.key() +
                           (physical && part->kind != "LM339" && !expanded(part->kind) ? QString()
                                                                                       : " · " + endpoint),
                       QFont("Segoe UI", 8), "probe", {8, -26});
        }
    for (const auto &label : document->circuit.labels)
        if (const auto *part = document->circuit.part(label.pin.part))
            annotation(*part, label.pin.index, label.name, QFont("Segoe UI", 9), "label", {8, 8});
    QVector<WireRoute> routes;
    for (int i = 0; i < document->circuit.wires.size(); i++)
    {
        auto &w = document->circuit.wires[i];
        auto *a = document->circuit.part(w.a.part);
        auto *b = document->circuit.part(w.b.part);
        if (!a || !b)
            continue;
        auto p = pinPosition(*a, w.a.index, physical), q = pinPosition(*b, w.b.index, physical);
        const QString net = map.value(w.a.key());
        const auto points = routeWire(p, pinExit(*a, w.a.index, physical), pinExit(*b, w.b.index, physical),
                                      q, obstacles, routes, net);
        auto endpoint = [](const Part &part, int pin)
        {
            const QString label = spec(part.kind).pins[pin];
            return part.id + " pin " + label;
        };
        scene()->addItem(new WireItem(points, this, net, colors.value(net),
                                      (net == "0" ? "GND" : net) + " · " + endpoint(*a, w.a.index) + " ↔ " +
                                          endpoint(*b, w.b.index),
                                      i));
        routes.append({points, net});
    }
    // Dots denote shared electrical nets. Geometric crossings of distinct nets
    // retain a halo gap and never acquire a junction or alter the document.
    QSet<QString> junctions;
    for (int i = 0; i < routes.size(); ++i)
        for (int j = 0; j < i; ++j)
            if (routes[i].net == routes[j].net)
                for (int a = 1; a < routes[i].points.size(); ++a)
                    for (int b = 1; b < routes[j].points.size(); ++b)
                    {
                        QPointF point;
                        if (QLineF(routes[i].points[a - 1], routes[i].points[a])
                                .intersects(QLineF(routes[j].points[b - 1], routes[j].points[b]), &point) !=
                            QLineF::BoundedIntersection)
                            continue;
                        const QString key = routes[i].net + '/' + QString::number(point.x(), 'f', 3) + '/' +
                                            QString::number(point.y(), 'f', 3);
                        if (junctions.contains(key))
                            continue;
                        junctions.insert(key);
                        auto *dot = scene()->addEllipse(QRectF(point.x() - 3, point.y() - 3, 6, 6), Qt::NoPen,
                                                        colors.value(routes[i].net));
                        dot->setZValue(-.5);
                        dot->setAcceptedMouseButtons(Qt::NoButton);
                        dot->setData(2, routes[i].net);
                        dot->setData(3, true);
                    }
    for (auto &p : document->circuit.parts)
    {
        auto *item = new PartItem(p, this);
        scene()->addItem(item);
        if (p.id == selected)
            item->setSelected(true);
    }
    scene()->setSceneRect(scene()->itemsBoundingRect().adjusted(-25, -25, 25, 25));
    if (!sceneRect().contains(scene()->sceneRect().adjusted(-1000, -1000, 1000, 1000)))
        setSceneRect(sceneRect().united(scene()->sceneRect().adjusted(-5000, -5000, 5000, 5000)));
    rebuilding = false;
    highlightNet({});
}
void CircuitView::refreshTheme()
{
    setBackgroundBrush(theme().canvas);
    const auto colors = netColors(document->circuit);
    for (auto *item : scene()->items())
    {
        if (auto *wire = dynamic_cast<WireItem *>(item))
        {
            wire->color = colors.value(wire->net);
            wire->setPen(QPen(wire->color, wire->pen().widthF()));
        }
        else if (item->data(3).toBool())
            static_cast<QGraphicsEllipseItem *>(item)->setBrush(colors.value(item->data(2).toString()));
        else if (auto *text = dynamic_cast<QGraphicsTextItem *>(item))
            text->setDefaultTextColor(item->data(5).toString() == "probe" ? theme().probe : theme().accent);
        item->update();
    }
    if (preview)
        preview->setPen(QPen(theme().accent, 1, Qt::DashLine));
    viewport()->update();
}
void CircuitView::fit()
{
    if (!isVisible())
        return;
    fitInView(scene()->sceneRect(), Qt::KeepAspectRatio);
}
void CircuitView::showEvent(QShowEvent *event)
{
    QGraphicsView::showEvent(event);
    QTimer::singleShot(0, this, &CircuitView::fit);
}
void CircuitView::setTool(QString name)
{
    if (locked)
        name = "select";
    tool = name;
    pending.clear();
    first.reset();
    if (preview)
    {
        scene()->removeItem(preview);
        delete preview;
        preview = nullptr;
    }
    setDragMode(tool == "pan" ? ScrollHandDrag : tool == "select" ? RubberBandDrag : NoDrag);
    updatePointer();
}
void CircuitView::place(QString kind)
{
    if (locked)
        return;
    setTool("place");
    pending = kind;
    updatePointer();
    if (isVisible())
        setFocus(Qt::OtherFocusReason);
    emit notice("Click the workspace to place " + spec(kind).name + ". Escape cancels.");
}
void CircuitView::pinPicked(Pin p)
{
    if (locked)
        return;
    if (tool == "label")
    {
        emit labelPicked(p);
        return;
    }
    if (tool == "probe")
    {
        emit probePicked(p);
        return;
    }
    if (!first)
    {
        first = p;
        emit notice("Wire start " + p.key() + " — select another pin.");
        return;
    }
    Pin start = *first;
    first.reset();
    if (preview)
    {
        scene()->removeItem(preview);
        delete preview;
        preview = nullptr;
    }
    if (start == p)
        return;
    for (auto &w : document->circuit.wires)
        if ((w.a == start && w.b == p) || (w.b == start && w.a == p))
            return;
    document->edit("Connect pins", [=](Circuit &c) { c.wires.append({start, p}); });
    emit notice("Connected " + start.key() + " to " + p.key());
}
void CircuitView::mousePressEvent(QMouseEvent *e)
{
    if (e->button() == Qt::RightButton || e->button() == Qt::MiddleButton ||
        (e->button() == Qt::LeftButton && (spaceHeld || tool == "pan")))
    {
        panning = true;
        hoveredWire = -1;
        hoveredNet.clear();
        highlightNet({}); // Retain explicit selection, but suppress transient hover.
        QToolTip::hideText();
        panMoved = false;
        panButton = e->button();
        panStart = lastPan = e->pos();
        setFocus(Qt::MouseFocusReason);
        updatePointer();
        e->accept();
        return;
    }
    if (tool == "place" && !pending.isEmpty() && e->button() == Qt::LeftButton)
    {
        placeAt(pending, mapToScene(e->pos()));
        return;
    }
    const auto before = scene()->selectedItems();
    QGraphicsView::mousePressEvent(e);
    // A click on an already-selected part (or already-empty canvas) must still
    // hand selection ownership back from the circuit tree to the viewport.
    if (tool == "select" && before == scene()->selectedItems())
    {
        QString id;
        for (auto *item : scene()->selectedItems())
            if (auto *part = dynamic_cast<PartItem *>(item))
            {
                id = part->part.id;
                break;
            }
        emit partSelected(id);
    }
}
QString CircuitView::placeAt(const QString &kind, QPointF pos)
{
    if (locked)
        return {};
    pos = {qRound(pos.x() / 10) * 10., qRound(pos.y() / 10) * 10.};
    QString created;
    document->edit("Place " + spec(kind).name,
                   [&](Circuit &c)
                   {
                       int i = 1;
                       do
                       {
                           created = spec(kind).prefix + QString::number(i++);
                       } while (std::any_of(c.parts.cbegin(), c.parts.cend(), [&](const Part &p)
                                            { return p.id.compare(created, Qt::CaseInsensitive) == 0; }));
                       Part added{created, kind, spec(kind).value, 0, pos, 0, {}, {}};
                       if (physical)
                           added.position = snappedToBoard(c, added);
                       c.parts.append(added);
                       if (kind == "LPROBE")
                           for (int pin = 0; pin < 8; ++pin)
                               c.probes[created + "_" + QString::number(pin)] = {created, pin};
                       if (kind == "TP")
                           c.probes[created] = {created, 0};
                   });
    return created;
}
void CircuitView::dragEnterEvent(QDragEnterEvent *e)
{
    if (!locked && !draggedComponent(e->mimeData(), this).isEmpty())
    {
        e->setDropAction(Qt::CopyAction);
        e->accept();
    }
    else
        e->ignore();
}
void CircuitView::dragMoveEvent(QDragMoveEvent *e)
{
    if (!locked && !draggedComponent(e->mimeData(), this).isEmpty())
    {
        e->setDropAction(Qt::CopyAction);
        e->accept();
    }
    else
        e->ignore();
}
void CircuitView::dropEvent(QDropEvent *e)
{
    QString kind = draggedComponent(e->mimeData(), this);
    if (locked || kind.isEmpty())
    {
        e->ignore();
        return;
    }
    QString id = placeAt(kind, mapToScene(e->position().toPoint()));
    if (id.isEmpty())
    {
        e->ignore();
        return;
    }
    setTool("select");
    emit toolRequested("select");
    setFocus(Qt::MouseFocusReason);
    selectPart(id);
    emit notice("Placed " + id + " · selection mode");
    e->setDropAction(Qt::CopyAction);
    e->accept();
}
void CircuitView::mouseMoveEvent(QMouseEvent *e)
{
    if (panning)
    {
        // OS mouse messages queued before SetCursorPos can arrive after the warp.
        // Sample the live cursor instead of interpreting those old coordinates as motion.
        const QPoint pos = panCursorPosition(e);
        if (!panMoved && (pos - panStart).manhattanLength() < QApplication::startDragDistance())
        {
            e->accept();
            return;
        }
        panMoved = true;
        updatePointer();
        QPoint delta = pos - lastPan;
        lastPan = pos;
        panBy(delta);
        wrapPanCursor(pos);
        e->accept();
        return;
    }
    QGraphicsView::mouseMoveEvent(e);
    // Scene hover uses its cached mouse position when scrollbars move. During
    // custom panning that position is stale, so the view owns wire hit testing.
    updateWireHover(panCursorPosition(e));
    if (first)
    {
        auto *part = document->circuit.part(first->part);
        if (!part)
            return;
        QPainterPath path(pinPosition(*part, first->index, physical));
        path.lineTo(mapToScene(e->pos()));
        if (!preview)
            preview = scene()->addPath(path, QPen(theme().accent, 1, Qt::DashLine));
        else
            preview->setPath(path);
    }
}
void CircuitView::panBy(QPoint delta)
{
    if (delta.isNull())
        return;
    // Pan in viewport pixels. Repeated centerOn(mapToScene(...)) roundtrips
    // truncate scrollbar positions at fractional zoom, losing slow movement
    // or introducing motion on the stationary axis.
    const int horizontal = horizontalScrollBar()->value() - delta.x();
    const int vertical = verticalScrollBar()->value() - delta.y();
    QRectF visible = mapToScene(viewport()->rect().translated(-delta)).boundingRect();
    if (!sceneRect().contains(visible.adjusted(-1000, -1000, 1000, 1000)))
        setSceneRect(sceneRect().united(visible.adjusted(-5000, -5000, 5000, 5000)));
    horizontalScrollBar()->setValue(horizontal);
    verticalScrollBar()->setValue(vertical);
}
bool CircuitView::warpCursor(QPoint target)
{
    QCursor::setPos(viewport()->mapToGlobal(target));
    return (viewport()->mapFromGlobal(QCursor::pos()) - target).manhattanLength() <= 3;
}
QPoint CircuitView::panCursorPosition(QMouseEvent *event) const
{
    return event->spontaneous() ? viewport()->mapFromGlobal(QCursor::pos()) : event->pos();
}
void CircuitView::wrapPanCursor(QPoint pos)
{
    const QRect inner = viewport()->rect().adjusted(4, 4, -4, -4);
    if (inner.width() < 8 || inner.height() < 8 || inner.contains(pos))
        return;
    auto wrap = [](int value, int low, int span) { return low + ((value - low) % span + span) % span; };
    QPoint target = pos;
    if (pos.x() < inner.left() || pos.x() > inner.right())
        target.setX(wrap(pos.x(), inner.left(), inner.width()));
    if (pos.y() < inner.top() || pos.y() > inner.bottom())
        target.setY(wrap(pos.y(), inner.top(), inner.height()));
    lastPan = target;
    if (!warpCursor(target))
    {
        lastPan = pos;
    }
}
void CircuitView::mouseReleaseEvent(QMouseEvent *e)
{
    if (panning && e->button() == panButton)
    {
        bool context = panButton == Qt::RightButton && !panMoved;
        panning = false;
        panButton = Qt::NoButton;
        updatePointer();
        updateWireHover(panCursorPosition(e));
        if (context)
            emit contextRequested(mapToScene(e->pos()));
        e->accept();
        return;
    }
    QGraphicsView::mouseReleaseEvent(e);
}
void CircuitView::contextMenuEvent(QContextMenuEvent *e)
{
    // Mouse context requests are emitted only on an undragged right-button release.
    if (e->reason() == QContextMenuEvent::Keyboard)
        emit contextRequested(mapToScene(e->pos()));
    e->accept();
}
void CircuitView::updatePointer()
{
    setCursor(panning && panMoved                          ? Qt::ClosedHandCursor
              : (!panning && (spaceHeld || tool == "pan")) ? Qt::OpenHandCursor
              : (tool == "wire" || tool == "probe" || tool == "label" || tool == "place") ? Qt::CrossCursor
                                                                                          : Qt::ArrowCursor);
    viewport()->setCursor(cursor());
}
void CircuitView::keyReleaseEvent(QKeyEvent *e)
{
    if (e->key() == Qt::Key_Space)
    {
        if (!e->isAutoRepeat())
        {
            spaceHeld = false;
            updatePointer();
        }
        e->accept();
        return;
    }
    QGraphicsView::keyReleaseEvent(e);
}
void CircuitView::focusOutEvent(QFocusEvent *e)
{
    panning = false;
    spaceHeld = false;
    panButton = Qt::NoButton;
    hoveredWire = -1;
    hoveredNet.clear();
    highlightNet({});
    updatePointer();
    QGraphicsView::focusOutEvent(e);
}
void CircuitView::leaveEvent(QEvent *event)
{
    hoveredWire = -1;
    hoveredNet.clear();
    highlightNet({});
    QGraphicsView::leaveEvent(event);
}
void CircuitView::deleteSelected()
{
    if (locked)
        return;
    QSet<QString> ids;
    QSet<int> wires;
    for (auto *item : scene()->selectedItems())
        if (auto *p = dynamic_cast<PartItem *>(item))
            ids.insert(p->part.id);
        else if (item->data(1).isValid())
            wires.insert(item->data(1).toInt());
    document->edit("Delete selection",
                   [=](Circuit &c)
                   {
                       for (int i = c.wires.size() - 1; i >= 0; i--)
                           if (wires.contains(i) || ids.contains(c.wires[i].a.part) ||
                               ids.contains(c.wires[i].b.part))
                               c.wires.remove(i);
                       for (int i = c.parts.size() - 1; i >= 0; i--)
                           if (ids.contains(c.parts[i].id))
                               c.parts.remove(i);
                       c.labels.removeIf([&](const NetLabel &label) { return ids.contains(label.pin.part); });
                       for (auto it = c.probes.begin(); it != c.probes.end();)
                           if (ids.contains(it.value().part))
                               it = c.probes.erase(it);
                           else
                               ++it;
                   });
}
void CircuitView::rotateSelected()
{
    if (locked)
        return;
    QSet<QString> ids;
    for (auto *item : scene()->selectedItems())
        if (auto *p = dynamic_cast<PartItem *>(item))
            ids.insert(p->part.id);
    document->edit("Rotate selection",
                   [=](Circuit &c)
                   {
                       for (auto &p : c.parts)
                           if (ids.contains(p.id))
                               p.rotation = (p.rotation + 90) % 360;
                   });
}
void CircuitView::selectPart(QString id)
{
    selectParts(id.isEmpty() ? QStringList{} : QStringList{id});
}
void CircuitView::selectParts(const QStringList &ids)
{
    QString selected;
    {
        QSignalBlocker blocker(scene());
        for (auto *item : scene()->items())
            if (auto *part = dynamic_cast<PartItem *>(item))
            {
                const bool match = ids.contains(part->part.id);
                part->setSelected(match);
                if (match && selected.isEmpty())
                    selected = part->part.id;
            }
            else if (item->flags().testFlag(QGraphicsItem::ItemIsSelectable))
                item->setSelected(false);
    }
    highlightNet(hoveredNet);
    emit partSelected(selected);
}
void CircuitView::setDeviceStates(const QMap<QString, double> &values)
{
    for (auto *item : scene()->items())
        if (auto *p = dynamic_cast<PartItem *>(item))
        {
            if (internalMeasurements(p->part).isEmpty())
                continue;
            p->deviceStates.clear();
            for (auto &name : internalMeasurements(p->part))
                p->deviceStates[name] = values.value(internalKey(p->part, name), parameter(p->part, name));
            p->update();
        }
}
void CircuitView::setSimulationTime(double time)
{
    for (auto *item : scene()->items())
        if (auto *p = dynamic_cast<PartItem *>(item))
            if (spec(p->part.kind).behavior == "switch")
            {
                p->simulationTime = time;
                p->update();
            }
}
void CircuitView::setLedCurrents(const QMap<QString, double> &currents)
{
    for (auto *item : scene()->items())
        if (auto *p = dynamic_cast<PartItem *>(item))
            if (p->part.kind == "LED" || spec(p->part.kind).behavior == "led")
            {
                p->ledCurrent = currents.value(p->part.id);
                p->lit = p->ledCurrent > .0005;
                p->update();
            }
}
void CircuitView::keyPressEvent(QKeyEvent *e)
{
    if (e->key() == Qt::Key_Space)
    {
        spaceHeld = true;
        updatePointer();
        e->accept();
        return;
    }
    if (e->key() == Qt::Key_Escape)
        emit toolRequested("select");
    else if (e->key() == Qt::Key_Delete)
        deleteSelected();
    else if (e->key() == Qt::Key_R)
        rotateSelected();
    else
        QGraphicsView::keyPressEvent(e);
}
void CircuitView::wheelEvent(QWheelEvent *e)
{
    double steps = e->angleDelta().y() / 120.;
    if (steps == 0)
        steps = e->pixelDelta().y() / 80.;
    if (steps != 0)
    {
        double current = transform().m11();
        double target = qBound(.15, current * std::pow(1.12, steps), 4.);
        scale(target / current, target / current);
    }
    updateWireHover(e->position().toPoint());
    e->accept();
}
void CircuitView::drawBackground(QPainter *p, const QRectF &r)
{
    QGraphicsView::drawBackground(p, r);
    if (transform().m11() < .4)
        return;
    p->setPen(theme().grid);
    for (int x = int(std::floor(r.left() / 20)) * 20; x < r.right(); x += 20)
        for (int y = int(std::floor(r.top() / 20)) * 20; y < r.bottom(); y += 20)
            p->drawPoint(x, y);
}
} // namespace cmp

namespace cmp
{
void CircuitView::setLocked(bool value)
{
    locked = value;
    setTool("select");
    for (auto *i : scene()->items())
        if (dynamic_cast<PartItem *>(i))
            i->setFlag(QGraphicsItem::ItemIsMovable, !value);
}
} // namespace cmp
