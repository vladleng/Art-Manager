#pragma once
#include <juce_core/juce_core.h>
#include <chrono>

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

// Pure civil-calendar arithmetic: avoid the machine's local timezone and DST.
// A planned date never means a JUCE/system-local midnight.
inline std::chrono::sys_days civilDay(const juce::String& value)
{
    return std::chrono::sys_days {std::chrono::year_month_day {
        std::chrono::year {value.substring(0, 4).getIntValue()},
        std::chrono::month {static_cast<unsigned>(value.substring(5, 7).getIntValue())},
        std::chrono::day {static_cast<unsigned>(value.substring(8, 10).getIntValue())}
    }};
}

inline juce::String addCivilDays(const juce::String& day, int offset)
{
    if (!isCivilDate(day)) return {};
    const std::chrono::year_month_day next {civilDay(day) + std::chrono::days {offset}};
    const int year = static_cast<int>(next.year());
    if (year < 1 || year > 9999) return {};
    return juce::String(year).paddedLeft('0', 4) + "-"
        + juce::String(static_cast<unsigned>(next.month())).paddedLeft('0', 2) + "-"
        + juce::String(static_cast<unsigned>(next.day())).paddedLeft('0', 2);
}

// Schedule an existing task on a chosen day. For an existing multi-day plan,
// move the whole range (not only its start); preserve local times, deadline
// and completion metadata. A task without a plan acquires a single-day start.
inline juce::Result taskPlanForDay(const TaskPlan& source, const juce::String& targetDay, TaskPlan& target)
{
    if (!isCivilDate(targetDay))
        return juce::Result::fail("Choose a valid YYYY-MM-DD planning day.");
    auto draft = source;
    if (source.start.isEmpty())
    {
        draft.start = targetDay;
        draft.end.clear();
    }
    else
    {
        if (!isCivilDateTime(source.start) ||
            (source.end.isNotEmpty() && !isCivilDateTime(source.end)))
            return juce::Result::fail("The existing task plan is invalid.");
        draft.start = targetDay + source.start.substring(10); // Preserve time, if present.
        if (source.end.isNotEmpty())
        {
            const auto calendarSpan = (civilDay(source.end.substring(0, 10))
                - civilDay(source.start.substring(0, 10))).count();
            const auto shiftedEnd = addCivilDays(targetDay, static_cast<int>(calendarSpan));
            if (shiftedEnd.isEmpty())
                return juce::Result::fail("The shifted planning interval is out of range.");
            draft.end = shiftedEnd + source.end.substring(10);
        }
    }
    const auto valid = validateTaskPlan(draft);
    if (valid.wasOk()) target = std::move(draft);
    return valid;
}

// Apply a drop on the calendar: minute=-1 means all-day; 0..1439 is a
// quarter-hour-aligned slot. Existing timed intervals preserve their elapsed
// duration; date-only schedules use a 60-minute default when given a time.
inline juce::Result taskPlanForSlot(const TaskPlan& source, const juce::String& day,
    int minute, TaskPlan& result)
{
    if (!isCivilDate(day) || minute < -1 || minute >= 1440
        || (minute >= 0 && minute % 15 != 0))
        return juce::Result::fail("Invalid calendar day or quarter-hour slot.");
    TaskPlan next;
    auto valid = taskPlanForDay(source, day, next);
    if (valid.failed()) return valid;
    if (minute == -1)
    {
        next.start = day;
        if (next.end.isNotEmpty()) next.end = next.end.substring(0, 10);
        if (next.end == day) next.end.clear();
    }
    else
    {
        auto time = [](int m)
        {
            return juce::String(m / 60).paddedLeft('0', 2)
                + ":" + juce::String(m % 60).paddedLeft('0', 2);
        };
        int duration = 60;
        if (source.start.length() == 16 && source.end.length() == 16
            && isCivilDateTime(source.start) && isCivilDateTime(source.end))
        {
            const int days = static_cast<int>((civilDay(source.end.substring(0, 10))
                - civilDay(source.start.substring(0, 10))).count());
            const int first = source.start.substring(11, 13).getIntValue() * 60
                + source.start.substring(14, 16).getIntValue();
            const int last = source.end.substring(11, 13).getIntValue() * 60
                + source.end.substring(14, 16).getIntValue();
            duration = juce::jmax(15, days * 1440 + last - first);
        }
        const int endMinute = minute + duration;
        const auto endDay = addCivilDays(day, endMinute / 1440);
        if (endDay.isEmpty())
            return juce::Result::fail("Timed calendar interval exceeds the supported date range.");
        next.start = day + "T" + time(minute);
        next.end = endDay + "T" + time(endMinute % 1440);
    }
    valid = validateTaskPlan(next);
    if (valid.wasOk()) result = std::move(next);
    return valid;
}
}
