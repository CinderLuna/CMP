#pragma once
#include "cmp/Circuit.h"
#include "cmp/Simulator.h"
namespace cmp
{
struct RatingDefinition
{
    QString key, label, unit;
    double preset = 0; // Zero means a user rating is required.
};
QVector<RatingDefinition> ratingDefinitions(const QString &kind);
struct ElectricalIssue
{
    QString component, rule, unit;
    double firstTime = 0, peak = 0, limit = 0;
    bool operatingRange = false, lowerBound = false;
};
struct ElectricalReport
{
    QVector<ElectricalIssue> issues;
    QStringList incomplete;
    QStringList lines(bool solverCompleted) const;
};
ElectricalReport diagnoseElectrical(const Circuit &, const Samples &);
} // namespace cmp
