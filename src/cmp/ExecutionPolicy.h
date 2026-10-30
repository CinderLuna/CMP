#pragma once
#include <QtCore>
namespace cmp
{
struct Circuit;
struct PartSpec;
enum class ExecutionBackend
{
    AnalogSolver,
    Topology,
    DeviceVm,
    AvrRuntime
};
enum class NativeLowering
{
    None,
    LogicGates,
    Rom8085
};
struct ComponentExecution
{
    QString kind, behavior;
    ExecutionBackend backend;
    NativeLowering lowering;
};
const QVector<ComponentExecution> &executionPolicies();
const ComponentExecution *executionPolicy(const QString &kind);
QString executionBackendName(ExecutionBackend);
bool validateExecutionSpec(const PartSpec &, QString &error);
bool validateExecutionCatalog(QString &error);
bool validateExecutionCircuit(const Circuit &, QString &error);
} // namespace cmp
