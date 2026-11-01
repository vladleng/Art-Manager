#pragma once
#include "WorkspaceModel.h"
#include <functional>

namespace arranger
{
struct PageRow { juce::String id; int depth = 0; };
struct TaskReference { juce::String songId, ownerPageId, taskId; };

struct WorkspaceQueries
{
    static bool isArchived(const WorkspaceModel& model, juce::String pageId)
    {
        std::set<juce::String> visited;
        while (pageId.isNotEmpty())
        {
            if (!visited.insert(pageId).second) return true;
            const auto* page = model.findPage(pageId);
            if (page == nullptr) return true;
            if (page->archivedAt.isNotEmpty()) return true;
            pageId = page->parentPageId;
        }
        return false;
    }

    static bool isDescendant(const WorkspaceModel& model, const juce::String& pageId,
        const juce::String& ancestorId)
    {
        auto id = pageId;
        std::set<juce::String> visited;
        while (id.isNotEmpty() && visited.insert(id).second)
        {
            if (id == ancestorId) return true;
            const auto* page = model.findPage(id);
            if (page == nullptr) break;
            id = page->parentPageId;
        }
        return false;
    }

    static juce::String breadcrumb(const WorkspaceModel& model, juce::String pageId)
    {
        juce::StringArray titles;
        std::set<juce::String> visited;
        while (pageId.isNotEmpty() && visited.insert(pageId).second)
        {
            const auto* page = model.findPage(pageId);
            if (page == nullptr) break;
            titles.insert(0, page->title);
            pageId = page->parentPageId;
        }
        return titles.joinIntoString(" / ");
    }

    static std::vector<PageRow> activePages(const WorkspaceModel& model)
    {
        std::vector<PageRow> rows;
        std::set<juce::String> visited;
        std::function<void(const juce::String&, int)> append = [&](const auto& id, int depth)
        {
            if (!visited.insert(id).second || isArchived(model, id)) return;
            rows.push_back({id, depth});
            for (const auto& child : model.pages)
                if (child.kind == "page" && child.parentPageId == id) append(child.id, depth + 1);
        };
        for (const auto& page : model.pages)
            if (page.kind == "page")
            {
                const auto* parent = model.findPage(page.parentPageId);
                if (parent == nullptr || parent->kind != "page") append(page.id, 0);
            }
        return rows;
    }

    static std::vector<juce::String> archivedRoots(const WorkspaceModel& model)
    {
        std::vector<juce::String> result;
        for (const auto& page : model.pages)
            if (page.kind == "page" && page.archivedAt.isNotEmpty()) result.push_back(page.id);
        return result;
    }

    static const CatalogSong* song(const WorkspaceModel& model, const juce::String& songId)
    {
        for (const auto& item : model.catalog.songs) if (item.id == songId) return &item;
        return nullptr;
    }

    static const CatalogTask* task(const WorkspaceModel& model, const TaskReference& reference)
    {
        const auto* owner = song(model, reference.songId);
        if (owner != nullptr && owner->pageId == reference.ownerPageId)
            for (const auto& item : owner->tasks) if (item.id == reference.taskId) return &item;
        return nullptr;
    }


    static const WorkspaceTask* generalTask(const WorkspaceModel& model, const TaskReference& ref)
    {
        const auto* task = model.findTask(ref.taskId);
        return ref.songId.isEmpty() && task && task->ownerPageId == ref.ownerPageId ? task : nullptr;
    }
    static bool taskArchived(const WorkspaceModel& model, const juce::String& id)
    {
        const auto* task = model.findTask(id);
        return !task || task->archivedAt.isNotEmpty() || isArchived(model, task->ownerPageId);
    }
    static juce::String taskName(const WorkspaceModel& model, const TaskReference& ref)
    {
        if (const auto* t = generalTask(model, ref)) return t->properties.name;
        if (const auto* t = task(model, ref)) return t->name;
        return {};
    }
    static Status taskStatus(const WorkspaceModel& model, const TaskReference& ref)
    {
        if (const auto* t = generalTask(model, ref)) return t->properties.status;
        if (const auto* t = task(model, ref)) return t->status;
        return Status::unmarked;
    }
    static std::pair<int, int> taskProgress(const WorkspaceTask& task)
    {
        int done = 0, total = 0;
        for (const auto& cp : task.checkpoints) if (cp.archivedAt.isEmpty()) { ++total; if (cp.status == Status::done) ++done; }
        return {done, total};
    }

    static std::vector<TaskReference> allTasks(const WorkspaceModel& model)
    {
        std::vector<TaskReference> result;
        for (const auto& owner : model.catalog.songs)
            if (!isArchived(model, owner.pageId))
                for (const auto& item : owner.tasks)
                    result.push_back({owner.id, owner.pageId, item.id});
        for (const auto& item : model.tasks)
            if (!taskArchived(model, item.id)) result.push_back({{}, item.ownerPageId, item.id});
        return result;
    }
};
}
