#pragma once
#include "cmp/Circuit.h"
namespace cmp
{
void appendDisplayCatalog(QVector<PartSpec> &);
QStringList displayNetlist(const Part &, const QStringList &);
} // namespace cmp
