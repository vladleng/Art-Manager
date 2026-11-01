#pragma once
#include "WorkspaceCommands.h"
#include "WorkspaceStore.h"
#include <functional>

namespace arranger
{
class WorkspaceController
{
public:
    using Action = WorkspaceAction;
    WorkspaceController(WorkspaceModel& value, WorkspaceStore& storage) : model(value), store(storage) {}

    juce::Result execute(const Action& action)
    {
        auto candidate = model;
        WorkspaceCommands commands(candidate);
        auto result = action(commands);
        if (result.failed()) return result;
        result = candidate.validate();
        if (result.failed()) return result;
        result = store.save(candidate);
        if (result.wasOk())
        {
            previous = model;
            model = std::move(candidate);
        }
        return result;
    }

    // One-step persistent Undo/Redo for accepted workspace commands, including
    // drag-reorder/reparent and cross-page transfers. Never bypass store CAS,
    // backups or monotonic revision. Failed saves keep both states unchanged.
    juce::Result undoLast()
    {
        if (!previous) return juce::Result::fail("No workspace change to undo.");
        auto candidate = *previous;
        candidate.revision = model.revision; // Preserve monotonic revision on restore.
        const auto result = store.save(candidate);
        if (result.wasOk())
        {
            previous = model; // A second invocation redoes the last change.
            model = std::move(candidate);
        }
        return result;
    }
private:
    WorkspaceModel& model;
    WorkspaceStore& store;
    std::optional<WorkspaceModel> previous;
};
}
