#pragma once
#include "WorkspaceQueries.h"

namespace arranger
{
struct TaskTreeRow
{
    TaskReference reference;
    juce::String checkpointId;
    bool notes = false;
    int depth = 0;
    bool addChild = false;
    juce::String key() const
    {
        if (addChild) return "task:" + reference.taskId + ":add";
        return (checkpointId.isEmpty() ? "task:" + reference.taskId : "checkpoint:" + checkpointId)
            + (notes ? ":notes" : "");
    }
};
inline std::vector<TaskTreeRow> taskTreeRows(const WorkspaceModel& model,
    const std::vector<TaskReference>& references, const std::set<juce::String>& expanded, bool checkpointsOnly = false)
{
    std::vector<TaskTreeRow> rows;
    std::set<juce::String> visited;
    std::function<void(const TaskReference&, int)> appendTask = [&](const TaskReference& ref, int depth)
    {
        const auto* general = WorkspaceQueries::generalTask(model, ref);
        const auto* legacy = WorkspaceQueries::task(model, ref);
        if ((!general && !legacy) || WorkspaceQueries::isArchived(model, ref.ownerPageId)
            || (general && WorkspaceQueries::taskArchived(model, general->id))) return;
        if (!visited.insert(ref.taskId).second) return;
        TaskTreeRow root {ref, {}, false, depth};
        if (!checkpointsOnly) rows.push_back(root);
        if (!checkpointsOnly && !expanded.contains(root.key())) return;
        if (!checkpointsOnly) rows.push_back({ref, {}, true, depth + 1});
        const auto appendCheckpoint = [&](const juce::String& id)
        {
            TaskTreeRow child {ref, id, false, checkpointsOnly ? 0 : depth + 1};
            rows.push_back(child);
            if (expanded.contains(child.key())) rows.push_back({ref, id, true, child.depth + 1});
        };
        if (general)
        {
            // New full tasks form a recursive tree. Pre-existing checkpoint
            // records remain as independent leaves with their original IDs.
            if (!checkpointsOnly)
                for (const auto& child : model.tasks)
                    if (child.parentTaskId == general->id && child.ownerPageId == ref.ownerPageId
                        && !WorkspaceQueries::taskArchived(model, child.id))
                        appendTask({{}, ref.ownerPageId, child.id}, depth + 1);
            for (const auto& cp : general->checkpoints)
                if (cp.archivedAt.isEmpty()) appendCheckpoint(cp.id);
        }
        else for (const auto& cp : legacy->checkpoints) appendCheckpoint(cp.id);
        rows.push_back({ref, {}, false, checkpointsOnly ? 0 : depth + 1, true});
    };
    for (const auto& ref : references) appendTask(ref, 0);
    return rows;
}
}
