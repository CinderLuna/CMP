#pragma once
#include <QtCore>
#include <functional>
namespace cmp::ipc
{
constexpr quint32 MaximumFrame = 8 * 1024 * 1024;
constexpr int Version = 1;
QByteArray frame(QJsonObject);
bool writeSampleRows(const QJsonArray &, const std::function<bool(QJsonObject)> &, QString &error);
class Decoder
{
  public:
    bool feed(const QByteArray &, QVector<QJsonObject> &, QString &);
    bool atBoundary() const
    {
        return buffer.isEmpty();
    }

  private:
    QByteArray buffer;
    bool failed = false;
};
bool safeNetlist(const QString &, QString &);
} // namespace cmp::ipc
