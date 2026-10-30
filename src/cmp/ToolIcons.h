#pragma once
#include "cmp/Theme.h"
#include <QHash>
#include <QIcon>
#include <QPainter>
namespace cmp
{
inline QIcon toolIcon(const QString &name)
{
    const auto &c = theme();
    const QString key = name + c.text.name() + c.disabled.name();
    static QHash<QString, QIcon> cache;
    if (cache.contains(key))
        return cache.value(key);
    const QIcon source(":/tools/" + name + ".svg");
    QIcon result;
    for (int size : {20, 28, 40, 56, 80, 112})
        for (auto mode : {QIcon::Normal, QIcon::Disabled})
        {
            auto pixmap = source.pixmap(size, size);
            QPainter painter(&pixmap);
            painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
            painter.fillRect(pixmap.rect(), mode == QIcon::Normal ? c.text : c.disabled);
            painter.end();
            result.addPixmap(pixmap, mode);
        }
    cache.insert(key, result);
    return result;
}
} // namespace cmp
