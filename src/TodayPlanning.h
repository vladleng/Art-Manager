#pragma once
#include "WorkspaceQueries.h"

namespace arranger
{
struct TodayResults
{
    std::vector<TaskReference> planned, overdue, completed;
};

// Explicit civil day in workspace timezone. This read-only projection never
// creates task copies and intentionally ignores retained offline DAW catalog.
inline TodayResults tasksForDay(const WorkspaceModel& model, const juce::String& day)
{
    TodayResults result;
    if (!isCivilDate(day)) return result;
    for (const auto& task : model.tasks)
    {
        if (WorkspaceQueries::taskArchived(model, task.id)) continue;
        const TaskReference ref {{}, task.ownerPageId, task.id};
        const bool scheduled = planIncludesDay(task.plan, day);
        const bool completedToday = task.plan.completedAt.startsWith(day);
        const bool done = task.properties.status == Status::done;
        if (done)
        {
            if (scheduled || completedToday) result.completed.push_back(ref);
            continue;
        }
        if (scheduled) result.planned.push_back(ref);
        if (planDeadlineOverdue(task.plan, day)) result.overdue.push_back(ref);
    }
    return result;
}
}
