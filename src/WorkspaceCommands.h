#pragma once
#include "WorkspaceQueries.h"

namespace arranger
{
// Page ownership and mutation rules live here; UI has no storage/file knowledge.
class WorkspaceCommands
{
public:
    explicit WorkspaceCommands(WorkspaceModel& value) : model(value) {}

    juce::Result createPage(const juce::String& parentId, juce::String title, juce::String& createdId)
    {
        title = title.trim();
        if (title.isEmpty()) return juce::Result::fail("Page title cannot be empty.");
        const auto parent = validParent(parentId);
        if (parent.failed()) return parent;
        const auto id = juce::Uuid().toString();
        model.pages.push_back({id, parentId, title, "page", {}, {}});
        createdId = id;
        return juce::Result::ok();
    }

    juce::Result renamePage(const juce::String& id, juce::String title)
    {
        auto* page = editablePage(id);
        if (page == nullptr) return juce::Result::fail("Only active ordinary pages can be renamed.");
        title = title.trim();
        if (title.isEmpty()) return juce::Result::fail("Page title cannot be empty.");
        page->title = title;
        return juce::Result::ok();
    }

    juce::Result movePage(const juce::String& id, const juce::String& parentId)
    {
        auto* page = editablePage(id);
        if (page == nullptr) return juce::Result::fail("Only active ordinary pages can be moved.");
        const auto parent = validParent(parentId);
        if (parent.failed()) return parent;
        if (parentId.isNotEmpty() && WorkspaceQueries::isDescendant(model, parentId, id))
            return juce::Result::fail("A page cannot be nested inside itself or its descendants.");
        page->parentPageId = parentId;
        return juce::Result::ok();
    }

    juce::Result archivePage(const juce::String& id)
    {
        auto* page = editablePage(id);
        if (page == nullptr) return juce::Result::fail("Only active ordinary pages can be archived.");
        // Descendants inherit archive state. Existing child archives are retained
        // when a parent is later restored, and no blocks/IDs are removed.
        page->archivedAt = juce::Time::getCurrentTime().toISO8601(true);
        return juce::Result::ok();
    }

    juce::Result restorePage(const juce::String& id)
    {
        auto* page = model.findPage(id);
        if (page == nullptr || page->kind != "page" || page->archivedAt.isEmpty())
            return juce::Result::fail("Archived page was not found.");
        if (WorkspaceQueries::isArchived(model, page->parentPageId))
            return juce::Result::fail("Restore the archived parent first.");
        page->archivedAt.clear();
        return juce::Result::ok();
    }

    juce::Result replacePageBlocks(const juce::String& id,
        const std::vector<WorkspaceBlock>& expected, const std::vector<WorkspaceBlock>& blocks)
    {
        auto* page = editablePage(id);
        if (page == nullptr) return juce::Result::fail("Only active ordinary pages can be edited.");
        if (page->blocks != expected)
            return juce::Result::fail("Page content changed elsewhere. Reload the editor before editing.");
        auto candidate = model;
        candidate.findPage(id)->blocks = blocks;
        const auto result = candidate.validate();
        if (result.wasOk()) page->blocks = blocks;
        return result;
    }

    juce::Result createTask(const juce::String& ownerId, juce::String name, juce::String& createdId)
    {
        if (editablePage(ownerId) == nullptr) return juce::Result::fail("Choose an active ordinary owner page.");
        name = name.trim();
        if (name.isEmpty()) return juce::Result::fail("Task name cannot be empty.");
        WorkspaceTask task;
        task.id = juce::Uuid().toString(); task.ownerPageId = ownerId; task.properties.name = name;
        createdId = task.id; model.tasks.push_back(std::move(task));
        return juce::Result::ok();
    }
    juce::Result updateTask(const juce::String& id, const TaskProperties& expected, TaskProperties value)
    {
        auto* task = editableTask(id);
        if (task == nullptr) return juce::Result::fail("Only active general tasks can be edited.");
        if (task->properties != expected) return juce::Result::fail("Task properties changed elsewhere.");
        value.name = value.name.trim();
        if (value.name.isEmpty() || value.priority < 1 || value.priority > 4 || parseStatus(label(value.status)) == Status::unmarked)
            return juce::Result::fail("Invalid task name, priority or status.");
        task->properties = std::move(value);
        return juce::Result::ok();
    }
    juce::Result replaceTaskBlocks(const juce::String& id,
        const std::vector<WorkspaceBlock>& expected, const std::vector<WorkspaceBlock>& blocks)
    {
        auto* task = editableTask(id);
        if (!task) return juce::Result::fail("Only active task descriptions can be edited.");
        if (task->blocks != expected) return juce::Result::fail("Task description changed elsewhere.");
        auto candidate = model; candidate.findTask(id)->blocks = blocks;
        const auto result = candidate.validate();
        if (result.wasOk()) task->blocks = blocks;
        return result;
    }
    juce::Result archiveTask(const juce::String& id)
    {
        auto* task = editableTask(id);
        if (!task) return juce::Result::fail("Active general task was not found.");
        task->archivedAt = juce::Time::getCurrentTime().toISO8601(true);
        return juce::Result::ok();
    }
    juce::Result restoreTask(const juce::String& id)
    {
        auto* task = model.findTask(id);
        if (!task || task->archivedAt.isEmpty()) return juce::Result::fail("Archived task was not found.");
        if (WorkspaceQueries::isArchived(model, task->ownerPageId)) return juce::Result::fail("Restore the owner page first.");
        task->archivedAt.clear(); return juce::Result::ok();
    }
    juce::Result createCheckpoint(const juce::String& taskId, juce::String name, juce::String& createdId)
    {
        auto* task = editableTask(taskId); name = name.trim();
        if (!task || name.isEmpty()) return juce::Result::fail("Choose an active task and a non-empty checkpoint name.");
        TaskCheckpoint cp; cp.id = juce::Uuid().toString(); cp.name = name;
        createdId = cp.id; task->checkpoints.push_back(std::move(cp)); return juce::Result::ok();
    }
    juce::Result updateCheckpoint(const juce::String& taskId, const TaskCheckpoint& expected, TaskCheckpoint value)
    {
        auto* cp = checkpoint(taskId, expected.id);
        if (!cp || cp->archivedAt.isNotEmpty()) return juce::Result::fail("Active checkpoint was not found.");
        if (*cp != expected) return juce::Result::fail("Checkpoint changed elsewhere.");
        value.name = value.name.trim();
        if (value.id != expected.id || value.archivedAt != expected.archivedAt || value.name.isEmpty()
            || parseStatus(label(value.status)) == Status::unmarked) return juce::Result::fail("Invalid checkpoint.");
        *cp = std::move(value); return juce::Result::ok();
    }
    juce::Result archiveCheckpoint(const juce::String& taskId, const juce::String& cpId)
    {
        auto* cp = checkpoint(taskId, cpId);
        if (!cp || cp->archivedAt.isNotEmpty()) return juce::Result::fail("Active checkpoint was not found.");
        cp->archivedAt = juce::Time::getCurrentTime().toISO8601(true); return juce::Result::ok();
    }
    juce::Result restoreCheckpoint(const juce::String& taskId, const juce::String& cpId)
    {
        auto* cp = checkpoint(taskId, cpId);
        if (!cp || cp->archivedAt.isEmpty()) return juce::Result::fail("Archived checkpoint was not found.");
        cp->archivedAt.clear(); return juce::Result::ok();
    }

    // Local task editing shared by the general workspace and retained legacy records.
    // This does not read or write any external musical project.
    juce::Result updateTaskText(const TaskReference& ref, const juce::String& cpId, TaskTextField field,
        const juce::String& expected, juce::String value)
    {
        if (WorkspaceQueries::isArchived(model, ref.ownerPageId)) return juce::Result::fail("Owner is archived or missing.");
        const bool notes = field == TaskTextField::notes;
        if (!notes) { value = value.trim(); if (value.isEmpty()) return juce::Result::fail("Name cannot be empty."); }
        if (auto* task = editableTask(ref.taskId); ref.songId.isEmpty() && task && task->ownerPageId == ref.ownerPageId)
        {
            if (cpId.isEmpty())
            {
                auto props = task->properties;
                if ((notes ? props.notes : props.name) != expected) return juce::Result::fail("Field changed elsewhere.");
                auto next = props; (notes ? next.notes : next.name) = value;
                return updateTask(ref.taskId, props, next);
            }
            for (const auto& cp : task->checkpoints) if (cp.id == cpId)
            {
                if ((notes ? cp.notes : cp.name) != expected) return juce::Result::fail("Field changed elsewhere.");
                const auto before = cp; auto next = before; (notes ? next.notes : next.name) = value;
                return updateCheckpoint(ref.taskId, before, next);
            }
            return juce::Result::fail("Subtask not found.");
        }
        auto* song = model.catalog.findSongById(ref.songId);
        if (!song || song->pageId != ref.ownerPageId) return juce::Result::fail("Task owner changed.");
        auto* task = model.catalog.findTask(song->path, ref.taskId);
        if (!task) return juce::Result::fail("Task not found.");
        juce::String* name = &task->name;
        if (cpId.isNotEmpty())
        {
            name = nullptr; for (auto& cp : task->checkpoints) if (cp.id == cpId) name = &cp.name;
            if (!name) return juce::Result::fail("Subtask not found.");
        }
        if (notes)
        {
            const auto key = cpId.isEmpty() ? taskNoteKey(ref.taskId) : checkpointNoteKey(cpId);
            if (model.catalog.localNote(song->path, key) != expected) return juce::Result::fail("Notes changed elsewhere.");
            return model.catalog.setLocalNote(song->path, key, value) ? juce::Result::ok() : juce::Result::fail("Cannot save notes.");
        }
        if (*name != expected) return juce::Result::fail("Name changed elsewhere.");
        *name = value; return juce::Result::ok();
    }
    juce::Result createTaskCheckpoint(const TaskReference& ref, const juce::String& name, juce::String& created)
    {
        if (WorkspaceQueries::isArchived(model, ref.ownerPageId)) return juce::Result::fail("Owner is archived or missing.");
        if (ref.songId.isEmpty())
        {
            const auto* task = editableTask(ref.taskId);
            if (!task || task->ownerPageId != ref.ownerPageId) return juce::Result::fail("Task owner changed.");
            return createCheckpoint(ref.taskId, name, created);
        }
        auto* song = model.catalog.findSongById(ref.songId);
        if (!song || song->pageId != ref.ownerPageId) return juce::Result::fail("Task owner changed.");
        created = model.catalog.addCheckpoint(song->path, ref.taskId, name);
        return created.isNotEmpty() ? juce::Result::ok() : juce::Result::fail("Choose a task and a non-empty name.");
    }
    juce::Result setReferenceStatus(const TaskReference& ref, const juce::String& cpId, Status status)
    {
        if (parseStatus(label(status)) == Status::unmarked || WorkspaceQueries::isArchived(model, ref.ownerPageId)) return juce::Result::fail("Invalid status or owner.");
        if (ref.songId.isEmpty())
        {
            const auto* task = editableTask(ref.taskId);
            if (!task || task->ownerPageId != ref.ownerPageId) return juce::Result::fail("Task owner changed.");
            if (cpId.isEmpty()) { const auto before = task->properties; auto next = before; next.status = status; return updateTask(ref.taskId, before, next); }
            for (const auto& cp : task->checkpoints) if (cp.id == cpId) { const auto before = cp; auto next = before; next.status = status; return updateCheckpoint(ref.taskId, before, next); }
            return juce::Result::fail("Subtask not found.");
        }
        const auto* song = model.catalog.findSongById(ref.songId);
        if (!song || song->pageId != ref.ownerPageId) return juce::Result::fail("Task owner changed.");
        const bool ok = cpId.isEmpty() ? model.catalog.setTaskStatus(song->path, ref.taskId, status) : model.catalog.setCheckpointStatus(song->path, ref.taskId, cpId, status);
        return ok ? juce::Result::ok() : juce::Result::fail("Task not found.");
    }

private:
    WorkspaceTask* editableTask(const juce::String& id)
    {
        auto* task = model.findTask(id);
        return task && task->archivedAt.isEmpty() && !WorkspaceQueries::isArchived(model, task->ownerPageId) ? task : nullptr;
    }
    TaskCheckpoint* checkpoint(const juce::String& taskId, const juce::String& id)
    {
        if (auto* task = editableTask(taskId))
            for (auto& cp : task->checkpoints) if (cp.id == id) return &cp;
        return nullptr;
    }
    WorkspacePage* editablePage(const juce::String& id)
    {
        auto* page = model.findPage(id);
        return page != nullptr && page->kind == "page" && !WorkspaceQueries::isArchived(model, id) ? page : nullptr;
    }
    juce::Result validParent(const juce::String& id) const
    {
        if (id.isEmpty()) return juce::Result::ok();
        const auto* page = model.findPage(id);
        if (page == nullptr || page->kind != "page" || WorkspaceQueries::isArchived(model, id))
            return juce::Result::fail("Choose an active ordinary page as parent.");
        return juce::Result::ok();
    }
    WorkspaceModel& model;
};
using WorkspaceAction = std::function<juce::Result(WorkspaceCommands&)>;
}
