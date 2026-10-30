#pragma once
#include "cmp/Circuit.h"
#include "cmp/DeviceVm.h"
#include "cmp/LogicVm.h"
#include <memory>
namespace cmp
{
struct DigitalLayout
{
    QVector<int> inputs, outputs;
    int supply = -1, ground = -1;
};
DigitalLayout digitalLayout(const QString &);
bool digitalDevice(const QString &);
void appendDigitalCatalog(QVector<PartSpec> &);
QStringList digitalNetlist(const Part &, const QStringList &);
// Electrical/catalog adapter retains the public component API; execution lives in DeviceVm.
class DigitalDevice : public DeviceVm
{
  public:
    explicit DigitalDevice(const Part &part) : DeviceVm(part) {}
};
} // namespace cmp
