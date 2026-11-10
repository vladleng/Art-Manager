#pragma once
#include "WorkspaceQueries.h"

// Presentation-only labels. Resolve current page and ancestry by stable IDs
// on every calendar rebuild; never store hierarchy strings in task data.
namespace arranger
{
struct CalendarCardLabels
{
    juce::String context;  // Full page / parent task chain, excluding the card's own title.
    juce::String compact;  // Immediate parent + title for short time slots.
    juce::String full;     // Unabbreviated page / parent chain + task title.
};

inline CalendarCardLabels calendarCardLabels(const WorkspaceModel& model, const WorkspaceTask& task)
{
    juce::StringArray parents;
    std::set<juce::String> seen {task.id};
    auto parentId = task.parentTaskId;
    // Normal valid workspaces allow 8 task levels. Guard corrupt/cyclic data
    // as well, so painting never loops forever.
    for (int depth = 0; parentId.isNotEmpty() && depth < 9 && seen.insert(parentId).second; ++depth)
    {
        const auto* parent = model.findTask(parentId);
        if (!parent || parent->ownerPageId != task.ownerPageId) break;
        parents.insert(0, parent->properties.name);
        parentId = parent->parentTaskId;
    }

    const auto page = WorkspaceQueries::breadcrumb(model, task.ownerPageId);
    const auto parentPath = parents.joinIntoString(" › ");
    const auto context = parentPath.isEmpty() ? page
        : (page.isEmpty() ? parentPath : page + " · " + parentPath);
    const auto compact = parents.isEmpty() ? task.properties.name
        : parents[parents.size() - 1] + " › " + task.properties.name;
    const auto full = context.isEmpty() ? task.properties.name : context + " › " + task.properties.name;
    return {context, compact, full};
}
}
