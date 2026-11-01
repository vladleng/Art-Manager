#pragma once
#include "WorkspaceQueries.h"
#include <algorithm>

namespace arranger
{
// Task-view preferences are UI-only; never serialize them into WorkspaceModel.
enum class TaskViewSort { manual, nameAsc, nameDesc, priority };
struct TaskViewOptions
{
    juce::String search, status; // "" status means any status.
    int priority = 0;            // 0 means any P1–P4.
    TaskViewSort sort = TaskViewSort::manual;
    bool operator==(const TaskViewOptions&) const = default;
};
inline bool matchesQuery(const juce::String& value, const juce::String& query)
{
    return query.isEmpty() || value.containsIgnoreCase(query.trim());
}
inline bool pageMatchesSearch(const WorkspaceModel& model, const WorkspacePage& page, juce::String search)
{
    search = search.trim();
    if (search.isEmpty()) return true;
    if (matchesQuery(page.title, search) || matchesQuery(WorkspaceQueries::breadcrumb(model, page.id), search))
        return true;
    for (const auto& block : page.blocks)
        if (matchesQuery(block.text, search) || matchesQuery(block.name, search) || matchesQuery(block.url, search))
            return true;
    return false;
}
inline bool taskOwnMatchesSearch(const WorkspaceModel& model, const TaskReference& ref, juce::String search)
{
    search = search.trim();
    if (search.isEmpty()) return true;
    if (matchesQuery(WorkspaceQueries::taskName(model, ref), search)) return true;
    if (const auto* page = model.findPage(ref.ownerPageId))
        if (pageMatchesSearch(model, *page, search)) return true;
    if (const auto* task = WorkspaceQueries::generalTask(model, ref))
    {
        if (matchesQuery(task->properties.notes, search)) return true;
        for (const auto& block : task->blocks)
            if (matchesQuery(block.text, search) || matchesQuery(block.name, search)) return true;
        for (const auto& cp : task->checkpoints)
            if (cp.archivedAt.isEmpty()
                && (matchesQuery(cp.name, search) || matchesQuery(cp.notes, search))) return true;
    }
    else if (const auto* catalogTask = WorkspaceQueries::task(model, ref))
    {
        for (const auto& cp : catalogTask->checkpoints)
            if (matchesQuery(cp.name, search)) return true;
        for (const auto& song : model.catalog.songs)
            if (song.id == ref.songId)
                for (const auto& local : song.localNotes)
                    if (matchesQuery(local.text, search)) return true;
    }
    return false;
}
inline bool taskMatchesView(const WorkspaceModel& model, const TaskReference& ref,
    const TaskViewOptions& options)
{
    if (ref.songId.isNotEmpty())
    {
        if (!options.status.isEmpty() && juce::String(label(WorkspaceQueries::taskStatus(model, ref))) != options.status) return false;
        if (options.priority != 0) return false; // Legacy tasks have no priority.
    }
    else if (const auto* task = WorkspaceQueries::generalTask(model, ref))
    {
        if (WorkspaceQueries::taskArchived(model, task->id)) return false;
        if (!options.status.isEmpty() && juce::String(label(task->properties.status)) != options.status) return false;
        if (options.priority != 0 && task->properties.priority != options.priority) return false;
    }
    else return false;
    return taskOwnMatchesSearch(model, ref, options.search);
}
inline bool taskTreeMatchesView(const WorkspaceModel& model, const TaskReference& ref,
    const TaskViewOptions& options)
{
    if (taskMatchesView(model, ref, options)) return true;
    if (ref.songId.isNotEmpty()) return false;
    const auto* ancestor = model.findTask(ref.taskId);
    if (!ancestor) return false;
    std::set<juce::String> visited {ancestor->id};
    std::function<bool(const juce::String&)> hasMatch = [&](const juce::String& parentId)
    {
        for (const auto& child : model.tasks)
        {
            if (child.parentTaskId != parentId || child.ownerPageId != ancestor->ownerPageId
                || WorkspaceQueries::taskArchived(model, child.id) || !visited.insert(child.id).second) continue;
            if (taskMatchesView(model, {{}, child.ownerPageId, child.id}, options) || hasMatch(child.id))
                return true;
        }
        return false;
    };
    return hasMatch(ancestor->id);
}
inline std::vector<TaskReference> projectTaskView(const WorkspaceModel& model,
    std::vector<TaskReference> source, const TaskViewOptions& options)
{
    std::erase_if(source, [&](const TaskReference& ref) { return !taskTreeMatchesView(model, ref, options); });
    if (options.sort != TaskViewSort::manual)
        std::stable_sort(source.begin(), source.end(), [&](const TaskReference& a, const TaskReference& b)
        {
            if (options.sort == TaskViewSort::priority)
            {
                const auto* ta = WorkspaceQueries::generalTask(model, a);
                const auto* tb = WorkspaceQueries::generalTask(model, b);
                const auto pa = ta ? ta->properties.priority : 5, pb = tb ? tb->properties.priority : 5;
                if (pa != pb) return pa < pb;
            }
            else
            {
                const auto compare = WorkspaceQueries::taskName(model, a).compareIgnoreCase(
                    WorkspaceQueries::taskName(model, b));
                if (compare != 0) return options.sort == TaskViewSort::nameAsc ? compare < 0 : compare > 0;
            }
            return false; // Stable source order for ties.
        });
    return source;
}
inline juce::String encodeTaskView(const TaskViewOptions& options)
{
    auto result = std::make_unique<juce::DynamicObject>();
    result->setProperty("search", options.search);
    result->setProperty("status", options.status);
    result->setProperty("priority", options.priority);
    result->setProperty("sort", static_cast<int>(options.sort));
    return juce::JSON::toString(juce::var(result.release()));
}
inline TaskViewOptions decodeTaskView(const juce::String& json)
{
    const auto parsed = juce::JSON::parse(json);
    TaskViewOptions result;
    if (!parsed.isObject()) return result;
    result.search = parsed.getProperty("search", {}).toString().substring(0, 256);
    result.status = parsed.getProperty("status", {}).toString();
    result.priority = static_cast<int>(parsed.getProperty("priority", 0));
    const auto sort = static_cast<int>(parsed.getProperty("sort", 0));
    if (sort < 0 || sort > 3) result.sort = TaskViewSort::manual;
    else result.sort = static_cast<TaskViewSort>(sort);
    if (result.priority < 0 || result.priority > 4) result.priority = 0;
    if (result.status.isNotEmpty() && parseStatus(result.status.toStdString()) == Status::unmarked)
        result.status.clear();
    return result;
}
}
