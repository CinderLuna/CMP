#pragma once
#include <QtCore>
namespace cmp
{
bool parseFirmware(const QByteArray &source, bool intelHex, QByteArray &image, QString &error,
                   int capacity = 32768);
}
