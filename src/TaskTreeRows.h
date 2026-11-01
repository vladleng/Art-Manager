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
    juce::String key() const
    {
        return (checkpointId.isEmpty() ? "task:" + reference.taskId : "checkpoint:" + checkpointId)
            + (notes ? ":notes" : "");
    }
};
inline std::vector<TaskTreeRow> taskTreeRows(const WorkspaceModel& model,
    const std::vector<TaskReference>& references, const std::set<juce::String>& expanded, bool checkpointsOnly = false)
{
    std::vector<TaskTreeRow> rows;
    for (const auto& ref : references)
    {
        const auto* general = WorkspaceQueries::generalTask(model, ref);
        const auto* daw = WorkspaceQueries::task(model, ref);
        if ((!general && !daw) || WorkspaceQueries::isArchived(model, ref.ownerPageId)
            || (general && general->archivedAt.isNotEmpty())) continue;
        TaskTreeRow root {ref, {}, false, 0};
        if (!checkpointsOnly) rows.push_back(root);
        if (!checkpointsOnly && !expanded.contains(root.key())) continue;
        if (!checkpointsOnly) rows.push_back({ref, {}, true, 1});
        const auto append = [&](const juce::String& id)
        {
            TaskTreeRow child {ref, id, false, checkpointsOnly ? 0 : 1};
            rows.push_back(child);
            if (expanded.contains(child.key())) rows.push_back({ref, id, true, child.depth + 1});
        };
        if (general)
        {
            for (const auto& cp : general->checkpoints) if (cp.archivedAt.isEmpty()) append(cp.id);
        }
        else for (const auto& cp : daw->checkpoints) append(cp.id);
    }
    return rows;
}
}
