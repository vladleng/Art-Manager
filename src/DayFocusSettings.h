#pragma once
#include "WorkspaceQueries.h"
#include <algorithm>
#include <map>
#include <set>
#include <vector>

// The daily focus and visual ordering are UI preferences, not task properties.
// In particular, reordering calendar cards must not reorder page task trees,
// change plan times, or require a workspace-schema migration.
namespace arranger
{
struct DayFocusSettings
{
    juce::String projectId;
    juce::String status; // Empty = all; otherwise the canonical Status label.
    bool hideDone = false;
    int allDayHeight = 0; // 0 = adapt to content (legacy default).
    std::map<juce::String, std::vector<juce::String>> dayOrder;
};

inline bool focusContainsPage(const WorkspaceModel& model,
                              const juce::String& ownerPageId,
                              const juce::String& projectId)
{
    return projectId.isEmpty() || WorkspaceQueries::isDescendant(model, ownerPageId, projectId);
}

inline bool focusMatchesTask(const WorkspaceModel& model, const WorkspaceTask& task,
                             const DayFocusSettings& settings)
{
    if (WorkspaceQueries::taskArchived(model, task.id)
        || !focusContainsPage(model, task.ownerPageId, settings.projectId))
        return false;
    if (settings.hideDone && task.properties.status == Status::done)
        return false;
    if (settings.status.isNotEmpty() && juce::String(label(task.properties.status)) != settings.status)
        return false;
    return true;
}

inline bool focusMatchesEvent(const WorkspaceModel& model, const CalendarEvent& event,
                              const DayFocusSettings& settings)
{
    // Events have no task status. Show them in the unfiltered view, but
    // never mislabel one as a TODO/WIP/DONE task when a status is selected.
    return event.archivedAt.isEmpty() && settings.status.isEmpty()
        && focusContainsPage(model, event.ownerPageId, settings.projectId);
}

inline std::vector<juce::String> effectiveDayOrder(
    const std::vector<juce::String>& activeIds,
    const std::vector<juce::String>& preferredIds)
{
    std::set<juce::String> allowed(activeIds.begin(), activeIds.end()), used;
    std::vector<juce::String> result;
    result.reserve(allowed.size());
    for (const auto& id : preferredIds)
        if (allowed.contains(id) && used.insert(id).second) result.push_back(id);
    for (const auto& id : activeIds)
        if (used.insert(id).second) result.push_back(id);
    return result;
}

inline std::vector<juce::String> moveDayCard(
    std::vector<juce::String> current, const juce::String& moved,
    const juce::String& target, bool after)
{
    if (moved.isEmpty() || moved == target
        || std::find(current.begin(), current.end(), moved) == current.end()
        || std::find(current.begin(), current.end(), target) == current.end())
        return current;
    current.erase(std::remove(current.begin(), current.end(), moved), current.end());
    auto where = std::find(current.begin(), current.end(), target);
    if (after) ++where;
    current.insert(where, moved);
    return current;
}

inline std::vector<juce::String> unplannedTaskIds(
    const WorkspaceModel& model, const juce::String& text,
    const DayFocusSettings& settings, size_t limit = 120)
{
    std::vector<juce::String> matches;
    const auto query = text.trim();
    for (const auto& task : model.tasks)
    {
        if (task.plan.start.isNotEmpty() || !focusMatchesTask(model, task, settings)) continue;
        if (query.isNotEmpty())
        {
            // Search both task names and ancestor/page context. A short
            // shared child name is not sufficient to identify a real task.
            juce::String searchable = task.properties.name + " "
                + task.properties.notes + " "
                + WorkspaceQueries::breadcrumb(model, task.ownerPageId);
            std::set<juce::String> seen {task.id};
            auto parentId = task.parentTaskId;
            while (parentId.isNotEmpty() && seen.insert(parentId).second)
            {
                const auto* parent = model.findTask(parentId);
                if (!parent) break;
                searchable += " " + parent->properties.name;
                parentId = parent->parentTaskId;
            }
            if (!searchable.containsIgnoreCase(query)) continue;
        }
        matches.push_back(task.id);
        if (matches.size() >= limit) break;
    }
    return matches;
}

inline juce::String encodeDayFocus(const DayFocusSettings& state)
{
    auto root = std::make_unique<juce::DynamicObject>();
    root->setProperty("version", 1);
    root->setProperty("projectId", state.projectId);
    root->setProperty("status", state.status);
    root->setProperty("hideDone", state.hideDone);
    root->setProperty("allDayHeight", state.allDayHeight);
    juce::Array<juce::var> days;
    for (const auto& [date, ids] : state.dayOrder)
    {
        if (!isCivilDate(date) || ids.empty()) continue;
        auto item = std::make_unique<juce::DynamicObject>();
        item->setProperty("day", date);
        juce::Array<juce::var> values;
        for (const auto& id : ids)
            if (id.isNotEmpty()) values.add(id);
        item->setProperty("ids", values);
        days.add(juce::var(item.release()));
    }
    root->setProperty("dayOrder", days);
    return juce::JSON::toString(juce::var(root.release()));
}

inline DayFocusSettings decodeDayFocus(const juce::String& json)
{
    DayFocusSettings result;
    const auto parsed = juce::JSON::parse(json);
    if (!parsed.isObject() || parsed.getProperty("version", 0) != 1) return result;
    result.projectId = parsed.getProperty("projectId", {}).toString();
    result.status = parsed.getProperty("status", {}).toString();
    if (result.status.isNotEmpty() && parseStatus(result.status.toStdString()) == Status::unmarked)
        result.status.clear();
    const auto hide = parsed.getProperty("hideDone", false);
    if (hide.isBool()) result.hideDone = static_cast<bool>(hide);
    const auto height = static_cast<int>(parsed.getProperty("allDayHeight", 0));
    result.allDayHeight = height > 0 ? juce::jlimit(110, 1200, height) : 0;
    const auto days = parsed.getProperty("dayOrder", {});
    if (!days.isArray()) return result;
    for (const auto& item : *days.getArray())
    {
        if (!item.isObject()) continue;
        const auto date = item.getProperty("day", {}).toString();
        const auto values = item.getProperty("ids", {});
        if (!isCivilDate(date) || !values.isArray() || result.dayOrder.size() >= 366) continue;
        std::set<juce::String> used;
        auto& ids = result.dayOrder[date];
        for (const auto& raw : *values.getArray())
        {
            if (!raw.isString()) continue;
            const auto id = raw.toString();
            if (id.isNotEmpty() && used.insert(id).second && ids.size() < 300)
                ids.push_back(id);
        }
    }
    return result;
}
}
