#pragma once
#include <QUndoStack>
#include <QtCore>
#include <functional>
namespace cmp
{
struct ComparatorChannel
{
    int output, negative, positive; // zero-based physical package pin indices
};
struct ParameterSpec
{
    QString key, label, unit;
    double initial = 0, minimum = -1e12, maximum = 1e12;
};
struct PartSpec
{
    QString kind, name, prefix;
    QStringList pins;
    double value;
    int sprite;
    QVector<ComparatorChannel> comparators = {};
    QString model = {}, package = {};
    QString category = {}, behavior = {}, unit = {};
    QVector<ParameterSpec> settings = {};
    QString limitations = {}, dataKind = {};
};
const QVector<PartSpec> &catalog();
const PartSpec &spec(const QString &kind);
QString componentCategory(const QString &kind);
struct Part
{
    QString id, kind;
    double value = 0, initial = 0;
    QPointF position;
    int rotation = 0;
    QMap<QString, double> parameters;
    QMap<QString, double> ratings; // Explicit user-selected limits in SI units; absent means unspecified.
    QByteArray data = {};
    QString dataType = {};
};
struct Pin
{
    QString part;
    int index = 0;
    QString key() const
    {
        return part + ":" + QString::number(index);
    }
    bool operator==(const Pin &p) const
    {
        return part == p.part && index == p.index;
    }
};
struct Wire
{
    Pin a, b;
};
struct NetLabel
{
    QString name;
    Pin pin;
};
struct Circuit
{
    QVector<Part> parts;
    QVector<Wire> wires;
    QMap<QString, Pin> probes;
    QVector<NetLabel> labels;
    double duration = 6, step = .001;
    bool operatingPoint = false;
    QJsonObject json() const;
    static bool parse(const QJsonObject &, Circuit &, QString &);
    static Circuit blinker();
    static Circuit rc();
    static Circuit transistor();
    static Circuit comparator();
    bool hasExternalBias(const Pin &) const;
    Part *part(const QString &id);
    const Part *part(const QString &id) const;
    QStringList validate() const;
    QMap<QString, QString> nodes() const;
    QString netlist() const;
};
class Document : public QObject
{
    Q_OBJECT
  public:
    Circuit circuit = Circuit::blinker();
    QUndoStack undo;
    QString path;
    bool editsLocked = false;
    explicit Document(QObject *p = nullptr) : QObject(p) {}
    void edit(const QString &label, const std::function<void(Circuit &)> &fn);
    void replace(const Circuit &c, const QString &file = {});
    bool save(const QString &, QString &);
    bool load(const QString &, QString &);
  signals:
    void changed();
};
QString quantity(double value, const QString &kind);
bool parseQuantity(QString, double &);
bool parseQuantityUnit(QString, const QString &, double &);
} // namespace cmp
