#pragma once
#include "WorkspaceQueries.h"
#include "WorkspaceTemplates.h"
#include "WorkspacePortable.h"
#include <algorithm>
#include <iterator>

namespace arranger
{
// Page ownership and mutation rules live here; UI has no storage/file knowledge.
class WorkspaceCommands
{
public:
    explicit WorkspaceCommands(WorkspaceModel& value) : model(value) {}

    // #68: Controller wraps the entire operation in its one-save transaction.
    // Imported data is never written directly into the live model or source file.
    juce::Result importPortable(const PortablePackage& package, PortableImportPolicy policy)
    {
        return WorkspacePortable::merge(package, model, policy);
    }

    // Explicit legacy material transfer. Old page.blocks IDs and content move
    // intact into a new child note; there is no automatic migration on load.
    juce::Result moveLegacyBlocksToNote(const juce::String& pageId, juce::String& createdId)
    {
        const auto* original = editablePage(pageId);
        if (!original || original->kind != "page" || original->blocks.empty())
            return juce::Result::fail("No editable legacy page blocks to move.");
        auto candidate = model;
        auto* page = candidate.findPage(pageId);
        const auto id = juce::Uuid().toString();
        WorkspacePage note;
        note.id = id;
        note.parentPageId = pageId;
        note.title = juce::String::fromUTF8("Материалы — ") + page->title;
        note.kind = "note";
        note.blocks = std::move(page->blocks);
        page->blocks.clear();
        candidate.pages.push_back(std::move(note));
        const auto result = candidate.validate();
        if (result.failed()) return result;
        model = std::move(candidate);
        createdId = id;
        return juce::Result::ok();
    }

    juce::Result createFromTemplate(const WorkspaceTemplate& definition, const juce::String& ownerOrParent,
        juce::String name, juce::String& createdId)
    {
        auto result = definition.validate(); if (result.failed()) return result;
        name = name.trim();
        if (name.isEmpty()) return juce::Result::fail("Name cannot be empty.");
        auto candidate = model;
        WorkspaceCommands commands(candidate);
        auto fresh = definition; renewTemplateIds(fresh);
        juce::String owner = ownerOrParent, created;
        if (definition.project)
        {
            result = commands.createPage(ownerOrParent, name, owner);
            if (result.failed()) return result;
            candidate.findPage(owner)->blocks = fresh.content.pages.front().blocks;
            created = owner;
        }
        else
        {
            const auto* page = candidate.findPage(owner);
            if (!page || page->kind != "page" || WorkspaceQueries::isArchived(candidate, owner))
                return juce::Result::fail("Choose an active task page.");
        }
        for (auto task : fresh.content.tasks)
        {
            task.ownerPageId = owner;
            if (!definition.project && task.parentTaskId.isEmpty())
            { task.properties.name = name; created = task.id; }
            candidate.tasks.push_back(std::move(task));
        }
        result = candidate.validate();
        if (result.wasOk()) { model = std::move(candidate); createdId = created; }
        return result;
    }

    juce::Result createPage(const juce::String& parentId, juce::String title, juce::String& createdId, bool notePage = false)
    {
        title = title.trim();
        if (title.isEmpty()) return juce::Result::fail("Page title cannot be empty.");
        const auto parent = validParent(parentId);
        if (parent.failed()) return parent;
        const auto id = juce::Uuid().toString();
        model.pages.push_back({id, parentId, title, notePage ? "note" : "page", {}, {}});
        createdId = id;
        return juce::Result::ok();
    }

    juce::Result createNotePage(const juce::String& parentId, juce::String title, juce::String& createdId)
    {
        return createPage(parentId, std::move(title), createdId, true);
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
        if (page == nullptr || (page->kind != "page" && page->kind != "note") || page->archivedAt.isEmpty())
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
        const auto* page = editablePage(ownerId);
        if (page == nullptr || page->kind != "page") return juce::Result::fail("Choose an active task page, not a note.");
        name = name.trim();
        if (name.isEmpty()) return juce::Result::fail("Task name cannot be empty.");
        WorkspaceTask task;
        task.id = juce::Uuid().toString(); task.ownerPageId = ownerId; task.properties.name = name;
        createdId = task.id; model.tasks.push_back(std::move(task));
        return juce::Result::ok();
    }
    // New child tasks are normal WorkspaceTask records (not destructive conversions
    // of the pre-existing checkpoint records). Old documents remain readable.
    juce::Result createSubtask(const juce::String& parentId, juce::String name, juce::String& createdId)
    {
        const auto* parent = editableTask(parentId);
        name = name.trim();
        if (!parent || name.isEmpty()) return juce::Result::fail("Choose an active parent task and a name.");
        auto candidate = model;
        WorkspaceTask child;
        child.id = juce::Uuid().toString();
        child.ownerPageId = parent->ownerPageId;
        child.parentTaskId = parentId;
        child.properties.name = name;
        candidate.tasks.push_back(child);
        const auto result = candidate.validate();
        if (result.failed()) return result;
        model.tasks.push_back(std::move(child));
        createdId = model.tasks.back().id;
        return juce::Result::ok();
    }

    // One transaction for within-page ordering and cross-page moves. The new
    // parent and optional "before" sibling are stable IDs, never row indices.
    juce::Result moveTask(const juce::String& taskId,
        const juce::String& expectedOwner, const juce::String& expectedParent,
        const juce::String& destinationOwner, const juce::String& destinationParent,
        const juce::String& beforeSibling = {})
    {
        const auto* source = editableTask(taskId);
        if (!source) return juce::Result::fail("Active task was not found.");
        if (source->ownerPageId != expectedOwner || source->parentTaskId != expectedParent)
            return juce::Result::fail("Task position changed elsewhere.");
        const auto* destinationPage = editablePage(destinationOwner);
        if (!destinationPage || destinationPage->kind != "page")
            return juce::Result::fail("Destination task page is missing, archived or a note.");

        std::set<juce::String> subtree {taskId};
        bool expanded = true;
        while (expanded)
        {
            expanded = false;
            for (const auto& item : model.tasks)
                if (subtree.contains(item.parentTaskId) && subtree.insert(item.id).second)
                    expanded = true;
        }
        if (destinationParent.isNotEmpty())
        {
            const auto* parent = model.findTask(destinationParent);
            if (!parent || WorkspaceQueries::taskArchived(model, destinationParent)
                || parent->ownerPageId != destinationOwner || subtree.contains(destinationParent))
                return juce::Result::fail("Invalid destination parent or task cycle.");
        }
        if (beforeSibling.isNotEmpty())
        {
            const auto* sibling = model.findTask(beforeSibling);
            if (!sibling || sibling->id == taskId || WorkspaceQueries::taskArchived(model, beforeSibling)
                || sibling->ownerPageId != destinationOwner || sibling->parentTaskId != destinationParent)
                return juce::Result::fail("Insertion target is not a sibling.");
        }

        auto candidate = model;
        for (auto& item : candidate.tasks)
        {
            if (subtree.contains(item.id)) item.ownerPageId = destinationOwner;
            if (item.id == taskId) item.parentTaskId = destinationParent;
        }

        // Reorder only the moved root in the storage sequence; descendants
        // remain linked by parentTaskId and retain their relative order.
        auto& items = candidate.tasks;
        const auto sourceIt = std::find_if(items.begin(), items.end(),
            [&](const auto& item) { return item.id == taskId; });
        auto moved = std::move(*sourceIt);
        items.erase(sourceIt);
        auto insertAt = items.end();
        if (beforeSibling.isNotEmpty())
            insertAt = std::find_if(items.begin(), items.end(),
                [&](const auto& item) { return item.id == beforeSibling; });
        else
            for (auto it = items.begin(); it != items.end(); ++it)
                if (it->ownerPageId == destinationOwner && it->parentTaskId == destinationParent)
                    insertAt = std::next(it);
        items.insert(insertAt, std::move(moved));
        const auto result = candidate.validate(); // Includes parent cycles, depth, ownership and ID guards.
        if (result.failed()) return result;
        model.tasks = std::move(candidate.tasks);
        return juce::Result::ok();
    }

    juce::Result reorderCheckpoint(const juce::String& taskId,
        const juce::String& checkpointId, const juce::String& beforeCheckpointId = {})
    {
        auto* task = editableTask(taskId);
        if (!task) return juce::Result::fail("Active task was not found.");
        auto& checkpoints = task->checkpoints;
        const auto source = std::find_if(checkpoints.begin(), checkpoints.end(),
            [&](const auto& cp) { return cp.id == checkpointId && cp.archivedAt.isEmpty(); });
        if (source == checkpoints.end()) return juce::Result::fail("Active checkpoint was not found.");
        if (checkpointId == beforeCheckpointId) return juce::Result::ok();
        if (beforeCheckpointId.isNotEmpty() &&
            std::none_of(checkpoints.begin(), checkpoints.end(),
                [&](const auto& cp) { return cp.id == beforeCheckpointId && cp.archivedAt.isEmpty(); }))
            return juce::Result::fail("Insertion checkpoint is missing or archived.");
        auto moved = std::move(*source);
        checkpoints.erase(source);
        auto destination = beforeCheckpointId.isNotEmpty()
            ? std::find_if(checkpoints.begin(), checkpoints.end(),
                [&](const auto& cp) { return cp.id == beforeCheckpointId; })
            : checkpoints.end();
        checkpoints.insert(destination, std::move(moved));
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
    // #38: day planning edits the existing local task by stable ID.
    // UI passes the expected plan so stale changes do not overwrite another edit.
    juce::Result updateTaskPlan(const juce::String& id, const TaskPlan& expected, TaskPlan next)
    {
        auto* task = editableTask(id);
        if (!task) return juce::Result::fail("Only active general tasks can be planned.");
        if (task->plan != expected) return juce::Result::fail("Task plan changed elsewhere.");
        const auto valid = validateTaskPlan(next);
        if (valid.failed()) return valid;
        task->plan = std::move(next);
        return juce::Result::ok();
    }

    // #38 stage 2: existing tasks are planned by ID, never cloned into Today.
    juce::Result scheduleTaskForDay(const juce::String& id, const TaskPlan& expected,
        const juce::String& day)
    {
        TaskPlan next;
        const auto valid = taskPlanForDay(expected, day, next);
        if (valid.failed()) return valid;
        return updateTaskPlan(id, expected, std::move(next));
    }

    // Clear only the planned interval; deadlines and completion facts are not
    // another copy of the schedule and must survive an unschedule action.
    juce::Result removeTaskFromPlan(const juce::String& id, const TaskPlan& expected)
    {
        auto next = expected;
        next.start.clear();
        next.end.clear();
        return updateTaskPlan(id, expected, std::move(next));
    }

    // Standalone events are local workspace entries, never task clones.
    juce::Result createCalendarEvent(CalendarEvent event, juce::String& created)
    {
        if (event.id.isEmpty()) event.id = juce::Uuid().toString();
        event.title = event.title.trim();
        const auto valid = validateCalendarEvent(event);
        if (valid.failed()) return valid;
        auto candidate = model;
        candidate.events.push_back(event);
        const auto result = candidate.validate();
        if (result.failed()) return result;
        model.events.push_back(std::move(event));
        created = model.events.back().id;
        return juce::Result::ok();
    }

    juce::Result updateCalendarEvent(const CalendarEvent& expected, CalendarEvent updated)
    {
        auto* found = model.findEvent(expected.id);
        if (!found || found->archivedAt.isNotEmpty())
            return juce::Result::fail("Active event not found.");
        if (*found != expected)
            return juce::Result::fail("Calendar event changed elsewhere.");
        updated.title = updated.title.trim();
        if (updated.id != expected.id || updated.archivedAt != expected.archivedAt)
            return juce::Result::fail("Event identity cannot be changed.");
        const auto valid = validateCalendarEvent(updated);
        if (valid.failed()) return valid;
        auto candidate = model;
        *candidate.findEvent(expected.id) = updated;
        const auto result = candidate.validate();
        if (result.failed()) return result;
        *found = std::move(updated);
        return juce::Result::ok();
    }

    juce::Result archiveCalendarEvent(const CalendarEvent& expected)
    {
        auto* found = model.findEvent(expected.id);
        if (!found || found->archivedAt.isNotEmpty() || *found != expected)
            return juce::Result::fail("Calendar event changed or was archived.");
        found->archivedAt = juce::Time::getCurrentTime().toISO8601(true);
        return juce::Result::ok();
    }

    juce::Result scheduleTaskAtSlot(const juce::String& id, const TaskPlan& expected,
        const juce::String& day, int minute)
    {
        TaskPlan next;
        const auto valid = taskPlanForSlot(expected, day, minute, next);
        if (valid.failed()) return valid;
        return updateTaskPlan(id, expected, std::move(next));
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
        if (WorkspaceQueries::isArchived(model, task->ownerPageId)
            || (task->parentTaskId.isNotEmpty() && WorkspaceQueries::taskArchived(model, task->parentTaskId)))
            return juce::Result::fail("Restore the owner page and parent task first.");
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
        return task && !WorkspaceQueries::taskArchived(model, id) ? task : nullptr;
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
        return page != nullptr && (page->kind == "page" || page->kind == "note")
            && !WorkspaceQueries::isArchived(model, id) ? page : nullptr;
    }
    juce::Result validParent(const juce::String& id) const
    {
        if (id.isEmpty()) return juce::Result::ok();
        const auto* page = model.findPage(id);
        if (page == nullptr || (page->kind != "page" && page->kind != "note") || WorkspaceQueries::isArchived(model, id))
            return juce::Result::fail("Choose an active ordinary page as parent.");
        return juce::Result::ok();
    }
    WorkspaceModel& model;
};
using WorkspaceAction = std::function<juce::Result(WorkspaceCommands&)>;
}
