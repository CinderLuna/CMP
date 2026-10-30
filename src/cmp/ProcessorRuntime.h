#pragma once
#include <QtCore>
#include <atomic>
namespace cmp
{
enum class RunReason
{
    Deadline,
    Budget,
    Halt,
    Fault,
    Cancelled
};
struct ProcessorRun
{
    quint64 reachedCycle = 0, instructions = 0;
    RunReason reason = RunReason::Fault;
};
// Instructions commit atomically and may overshoot the requested cycle by one instruction.
// Budget yields do not claim the requested cycle was reached. Time never runs backward.
} // namespace cmp
