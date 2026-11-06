#pragma once
#include <juce_core/juce_core.h>

// Local civil dates/times are interpreted in WorkspaceModel::timezone.
// They deliberately have no implicit system-local timezone conversion.
// The model stores date-only YYYY-MM-DD or local YYYY-MM-DDTHH:mm.
namespace arranger
{
struct TaskPlan
{
    juce::String start, end, deadline, completedAt;
    bool operator==(const TaskPlan&) const = default;
    bool isEmpty() const
    {
        return start.isEmpty() && end.isEmpty() && deadline.isEmpty() && completedAt.isEmpty();
    }
};

inline bool isCivilDate(const juce::String& value)
{
    if (value.length() != 10 || value[4] != '-' || value[7] != '-') return false;
    for (int i = 0; i < 10; ++i)
        if (i != 4 && i != 7 && (value[i] < '0' || value[i] > '9')) return false;
    const int year = value.substring(0, 4).getIntValue();
    const int month = value.substring(5, 7).getIntValue();
    const int day = value.substring(8, 10).getIntValue();
    if (year < 1 || month < 1 || month > 12 || day < 1) return false;
    const bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
    const int lengths[] = {31, leap ? 29 : 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    return day <= lengths[month - 1];
}

inline bool isCivilDateTime(const juce::String& value)
{
    if (value.length() == 10) return isCivilDate(value);
    if (value.length() != 16 || value[10] != 'T' || value[13] != ':'
        || !isCivilDate(value.substring(0, 10))) return false;
    for (const int i : {11, 12, 14, 15})
        if (value[i] < '0' || value[i] > '9') return false;
    return value.substring(11, 13).getIntValue() < 24
        && value.substring(14, 16).getIntValue() < 60;
}

inline juce::Result validateTaskPlan(const TaskPlan& plan)
{
    if ((plan.start.isNotEmpty() && !isCivilDateTime(plan.start))
        || (plan.end.isNotEmpty() && !isCivilDateTime(plan.end))
        || (plan.deadline.isNotEmpty() && !isCivilDateTime(plan.deadline))
        || (plan.completedAt.isNotEmpty() && !isCivilDateTime(plan.completedAt)))
        return juce::Result::fail("Invalid ISO local date or time in task plan.");
    if (plan.end.isNotEmpty() && plan.start.isEmpty())
        return juce::Result::fail("A planned end requires a start.");
    if (plan.end.isNotEmpty() && plan.end.compare(plan.start) < 0)
        return juce::Result::fail("Planned end cannot precede start.");
    return juce::Result::ok();
}

inline bool planIncludesDay(const TaskPlan& plan, const juce::String& day)
{
    if (!isCivilDate(day) || !isCivilDateTime(plan.start)) return false;
    const auto startDay = plan.start.substring(0, 10);
    const auto endDay = plan.end.isNotEmpty() ? plan.end.substring(0, 10) : startDay;
    return startDay.compare(day) <= 0 && endDay.compare(day) >= 0;
}

inline bool planDeadlineOverdue(const TaskPlan& plan, const juce::String& day)
{
    return isCivilDate(day) && isCivilDateTime(plan.deadline)
        && plan.deadline.substring(0, 10).compare(day) < 0;
}
}
