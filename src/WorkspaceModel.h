#pragma once
#include "SongCatalog.h"
#include <set>

namespace arranger
{
struct WorkspaceBlock
{
    juce::String id, type = "text", text, url;
    bool checked = false;
    juce::String name; // Optional in schema 1; older documents have no block name.
    bool operator==(const WorkspaceBlock&) const = default;
};
struct WorkspacePage
{
    juce::String id, parentPageId, title, kind = "page", archivedAt;
    std::vector<WorkspaceBlock> blocks;
};


struct TaskProperties
{
    juce::String name, notes;
    Status status = Status::todo;
    int priority = 3;
    bool operator==(const TaskProperties&) const = default;
};
struct TaskCheckpoint
{
    juce::String id, name, notes, archivedAt;
    Status status = Status::todo;
    bool operator==(const TaskCheckpoint&) const = default;
};
struct WorkspaceTask
{
    juce::String id, ownerPageId, archivedAt;
    TaskProperties properties;
    std::vector<WorkspaceBlock> blocks;
    std::vector<TaskCheckpoint> checkpoints;
};
inline juce::Array<juce::var> writeBlocks(const std::vector<WorkspaceBlock>& source)
{
    juce::Array<juce::var> result;
    for (const auto& block : source)
    {
        auto b = std::make_unique<juce::DynamicObject>();
        b->setProperty("id", block.id); b->setProperty("name", block.name);
        b->setProperty("type", block.type); b->setProperty("text", block.text);
        b->setProperty("url", block.url); b->setProperty("checked", block.checked);
        result.add(juce::var(b.release()));
    }
    return result;
}
inline juce::Result readBlocks(const juce::var& raw, std::vector<WorkspaceBlock>& result)
{
    if (!raw.isArray()) return juce::Result::fail("Document blocks are missing.");
    for (const auto& b : *raw.getArray())
    {
        if (!b.isObject() || !b.getProperty("checked", {}).isBool())
            return juce::Result::fail("Invalid block record.");
        result.push_back({b.getProperty("id", {}).toString(), b.getProperty("type", {}).toString(),
            b.getProperty("text", {}).toString(), b.getProperty("url", {}).toString(),
            static_cast<bool>(b.getProperty("checked", false)), b.getProperty("name", {}).toString()});
    }
    return juce::Result::ok();
}

// Song tasks are still stored once in catalog.songs[].tasks. Their owner is
// CatalogSong::pageId. Later views resolve these same IDs, never copies.
struct WorkspaceModel
{
    juce::String id = juce::Uuid().toString(), name = "My workspace", timezone = "Asia/Krasnoyarsk";
    juce::int64 revision = 0;
    SongCatalog catalog;
    std::vector<WorkspacePage> pages;
    std::vector<WorkspaceTask> tasks;

    const WorkspacePage* findPage(const juce::String& pageId) const
    {
        for (const auto& page : pages) if (page.id == pageId) return &page;
        return nullptr;
    }
    WorkspacePage* findPage(const juce::String& pageId)
    {
        return const_cast<WorkspacePage*>(std::as_const(*this).findPage(pageId));
    }

    const WorkspaceTask* findTask(const juce::String& taskId) const
    {
        for (const auto& task : tasks) if (task.id == taskId) return &task;
        return nullptr;
    }
    WorkspaceTask* findTask(const juce::String& taskId)
    {
        return const_cast<WorkspaceTask*>(std::as_const(*this).findTask(taskId));
    }

    void reconcileCatalogPages()
    {
        // Preserve existing song titles/blocks and IDs. Current catalog deletion
        // keeps 0.1d behavior; archive UX is implemented in 0.1f.
        std::set<juce::String> live;
        for (const auto& folder : catalog.folders)
        {
            live.insert(folder.id);
            if (findPage(folder.id) == nullptr)
                pages.push_back({folder.id, {}, folder.name, "dawFolder", {}, {}});
            else
                findPage(folder.id)->title = folder.name;
        }
        for (const auto& song : catalog.songs)
        {
            live.insert(song.pageId);
            if (findPage(song.pageId) == nullptr)
                pages.push_back({song.pageId, song.folderId,
                    juce::File(song.path).getFileNameWithoutExtension(), "song", {}, {}});
            else
                findPage(song.pageId)->parentPageId = song.folderId;
        }
        pages.erase(std::remove_if(pages.begin(), pages.end(), [&](const auto& page)
        {
            return (page.kind == "song" || page.kind == "dawFolder") && live.count(page.id) == 0;
        }), pages.end());
    }

    juce::Result validate() const
    {
        auto fail = [](const char* text) { return juce::Result::fail(text); };
        if (id.isEmpty() || name.trim().isEmpty() || timezone.isEmpty() || revision < 0)
            return fail("Invalid workspace identity or revision.");
        std::set<juce::String> ids;
        auto addId = [&](const juce::String& value) { return value.isNotEmpty() && ids.insert(value).second; };
        if (!addId(id)) return fail("Duplicate workspace ID.");
        std::set<juce::String> folderIds, pageIds, songPages;
        for (const auto& page : pages)
        {
            if (!addId(page.id) || page.title.trim().isEmpty()
                || (page.kind != "page" && page.kind != "song" && page.kind != "dawFolder"))
                return fail("Invalid or duplicate page.");
            pageIds.insert(page.id);
            for (const auto& block : page.blocks)
                if (!addId(block.id) || (block.type != "text" && block.type != "heading"
                    && block.type != "list" && block.type != "checklist" && block.type != "link"))
                    return fail("Invalid or duplicate block.");
        }
        for (const auto& page : pages)
        {
            std::set<juce::String> visited { page.id };
            auto parent = page.parentPageId;
            while (parent.isNotEmpty())
            {
                if (!visited.insert(parent).second) return fail("Page hierarchy contains a cycle.");
                const WorkspacePage* found = nullptr;
                for (const auto& candidate : pages) if (candidate.id == parent) found = &candidate;
                if (found == nullptr) return fail("Page parent is missing.");
                parent = found->parentPageId;
            }
        }
        for (const auto& folder : catalog.folders)
        {
            const WorkspacePage* linked = nullptr;
            for (const auto& p : pages) if (p.id == folder.id) linked = &p;
            if (!folderIds.insert(folder.id).second || linked == nullptr || linked->kind != "dawFolder")
                return fail("Invalid DAW folder page.");
        }
        std::set<juce::String> paths;
        for (const auto& song : catalog.songs)
        {
            const WorkspacePage* linked = nullptr;
            for (const auto& p : pages) if (p.id == song.pageId) linked = &p;
            if (!addId(song.id) || linked == nullptr || linked->kind != "song"
                || !songPages.insert(song.pageId).second || song.path.isEmpty()
                || !juce::File(song.path).hasFileExtension("song")
                || !paths.insert(song.path.toLowerCase()).second
                || (song.folderId.isNotEmpty() && folderIds.count(song.folderId) == 0)
                || linked->parentPageId != song.folderId)
                return fail("Invalid song identity, path or owner page.");
            std::set<juce::String> noteKeys;
            for (const auto& note : song.localNotes)
                if (note.key.isEmpty() || !noteKeys.insert(note.key).second)
                    return fail("Invalid or duplicate local Notes key.");
            for (const auto& task : song.tasks)
            {
                if (!addId(task.id) || task.name.trim().isEmpty()) return fail("Invalid or duplicate task ID.");
                for (const auto& cp : task.checkpoints)
                    if (!addId(cp.id) || cp.name.trim().isEmpty()) return fail("Invalid or duplicate checkpoint ID.");
            }
        }
        for (const auto& page : pages)
            if ((page.kind == "song" && songPages.count(page.id) == 0)
                || (page.kind == "dawFolder" && folderIds.count(page.id) == 0))
                return fail("Orphan DAW page.");

        auto validStatus = [](Status status) { return parseStatus(label(status)) != Status::unmarked; };
        for (const auto& task : tasks)
        {
            const auto* owner = findPage(task.ownerPageId);
            if (!addId(task.id) || owner == nullptr || owner->kind != "page"
                || task.properties.name.trim().isEmpty() || !validStatus(task.properties.status)
                || task.properties.priority < 1 || task.properties.priority > 4)
                return fail("Invalid general task identity, owner or properties.");
            for (const auto& cp : task.checkpoints)
                if (!addId(cp.id) || cp.name.trim().isEmpty() || !validStatus(cp.status))
                    return fail("Invalid general checkpoint.");
            for (const auto& block : task.blocks)
                if (!addId(block.id) || (block.type != "text" && block.type != "heading"
                    && block.type != "list" && block.type != "checklist" && block.type != "link"))
                    return fail("Invalid task description block.");
        }
        return juce::Result::ok();
    }

    juce::String toJson() const
    {
        auto root = std::make_unique<juce::DynamicObject>();
        root->setProperty("format", "ArrangerManagerWorkspace");
        root->setProperty("schemaVersion", 2);
        root->setProperty("id", id);
        root->setProperty("name", name);
        root->setProperty("timezone", timezone);
        root->setProperty("revision", revision);
        root->setProperty("catalog", juce::JSON::parse(catalog.toJson()));
        juce::Array<juce::var> pageArray;
        for (const auto& page : pages)
        {
            auto p = std::make_unique<juce::DynamicObject>();
            p->setProperty("id", page.id);
            p->setProperty("parentPageId", page.parentPageId);
            p->setProperty("title", page.title);
            p->setProperty("kind", page.kind);
            p->setProperty("archivedAt", page.archivedAt);
            juce::Array<juce::var> blocks;
            for (const auto& block : page.blocks)
            {
                auto b = std::make_unique<juce::DynamicObject>();
                b->setProperty("id", block.id);
                b->setProperty("type", block.type);
                b->setProperty("text", block.text);
                b->setProperty("url", block.url);
                b->setProperty("checked", block.checked);
                b->setProperty("name", block.name);
                blocks.add(juce::var(b.release()));
            }
            p->setProperty("blocks", blocks);
            pageArray.add(juce::var(p.release()));
        }

        root->setProperty("pages", pageArray);
        juce::Array<juce::var> taskArray;
        for (const auto& task : tasks)
        {
            auto t = std::make_unique<juce::DynamicObject>();
            t->setProperty("id", task.id); t->setProperty("ownerPageId", task.ownerPageId);
            t->setProperty("archivedAt", task.archivedAt);
            t->setProperty("name", task.properties.name); t->setProperty("notes", task.properties.notes);
            t->setProperty("status", label(task.properties.status)); t->setProperty("priority", task.properties.priority);
            t->setProperty("blocks", writeBlocks(task.blocks));
            juce::Array<juce::var> checkpoints;
            for (const auto& cp : task.checkpoints)
            {
                auto c = std::make_unique<juce::DynamicObject>();
                c->setProperty("id", cp.id); c->setProperty("name", cp.name); c->setProperty("notes", cp.notes);
                c->setProperty("status", label(cp.status)); c->setProperty("archivedAt", cp.archivedAt);
                checkpoints.add(juce::var(c.release()));
            }
            t->setProperty("checkpoints", checkpoints);
            taskArray.add(juce::var(t.release()));
        }
        root->setProperty("tasks", taskArray);
        return juce::JSON::toString(juce::var(root.release()));
    }

    // The legacy reader is deliberately permissive for older plug-ins. The
    // workspace boundary rejects any catalog row it would silently discard.
    static juce::Result readCatalog(const juce::var& raw, SongCatalog& output, bool legacy)
    {
        const int version = static_cast<int>(raw.getProperty("version", 0));
        if (!raw.isObject() || version != (legacy ? 1 : 2))
            return juce::Result::fail("Unsupported catalog version.");
        const auto folders = raw.getProperty("folders", {});
        const auto songs = raw.getProperty("songs", {});
        if (!folders.isArray() || !songs.isArray())
            return juce::Result::fail("Catalog arrays are missing.");
        auto candidate = SongCatalog::fromJson(juce::JSON::toString(raw));
        if (candidate.folders.size() != static_cast<size_t>(folders.getArray()->size())
            || candidate.songs.size() != static_cast<size_t>(songs.getArray()->size()))
            return juce::Result::fail("Catalog contains invalid or duplicate rows; migration cancelled.");
        for (int i = 0; i < songs.getArray()->size(); ++i)
        {
            const auto& source = (*songs.getArray())[i];
            const auto& target = candidate.songs[static_cast<size_t>(i)];
            if (source.getProperty("folderId", {}).toString() != target.folderId
                || source.getProperty("songType", {}).toString() != songTypeKey(target.type))
                return juce::Result::fail("Catalog folder or type cannot be preserved.");
            const auto status = source.getProperty("status", {}).toString();
            if (status.isNotEmpty() && status != label(target.status)
                && !(status == "REVIEW" && target.status == Status::wait))
                return juce::Result::fail("Unknown song status.");
            const auto tasks = source.getProperty("tasks", juce::Array<juce::var>());
            const auto notes = source.getProperty("localNotes", juce::Array<juce::var>());
            if (!tasks.isArray() || !notes.isArray()
                || target.tasks.size() != static_cast<size_t>(tasks.getArray()->size())
                || target.localNotes.size() != static_cast<size_t>(notes.getArray()->size()))
                return juce::Result::fail("Catalog tasks or Notes cannot be preserved.");
            for (int j = 0; j < notes.getArray()->size(); ++j)
            {
                const auto& note = (*notes.getArray())[j];
                if (note.getProperty("key", {}).toString() != target.localNotes[static_cast<size_t>(j)].key
                    || note.getProperty("text", {}).toString() != target.localNotes[static_cast<size_t>(j)].text)
                    return juce::Result::fail("Catalog Notes changed during migration.");
            }
            for (int j = 0; j < tasks.getArray()->size(); ++j)
            {
                const auto& task = (*tasks.getArray())[j];
                const auto& added = target.tasks[static_cast<size_t>(j)];
                auto validStatus = [](const juce::var& item, Status value)
                {
                    const auto text = item.getProperty("status", {}).toString();
                    return text == label(value) || (text == "REVIEW" && value == Status::wait);
                };
                if (!validStatus(task, added.status)) return juce::Result::fail("Unknown task status.");
                const auto cps = task.getProperty("checkpoints", juce::Array<juce::var>());
                if (!cps.isArray() || added.checkpoints.size() != static_cast<size_t>(cps.getArray()->size()))
                    return juce::Result::fail("Catalog checkpoints cannot be preserved.");
                for (int k = 0; k < cps.getArray()->size(); ++k)
                    if (!validStatus((*cps.getArray())[k], added.checkpoints[static_cast<size_t>(k)].status))
                        return juce::Result::fail("Unknown checkpoint status.");
            }
        }
        output = std::move(candidate);
        return juce::Result::ok();
    }

    static juce::Result migrateLegacy(const juce::String& text, WorkspaceModel& output)
    {
        WorkspaceModel candidate;
        if (text.isNotEmpty())
        {
            const auto result = readCatalog(juce::JSON::parse(text), candidate.catalog, true);
            if (result.failed()) return result;
        }
        candidate.reconcileCatalogPages();
        const auto result = candidate.validate();
        if (result.wasOk()) output = std::move(candidate);
        return result;
    }

    static juce::Result fromJson(const juce::String& text, WorkspaceModel& output)
    {
        const auto root = juce::JSON::parse(text);
        const int version = static_cast<int>(root.getProperty("schemaVersion", 0));
        if (!root.isObject() || root.getProperty("format", {}).toString() != "ArrangerManagerWorkspace"
            || (version != 1 && version != 2))
            return juce::Result::fail("Invalid or unsupported workspace schema; file was not modified.");
        if (version == 1 && root.hasProperty("tasks"))
            return juce::Result::fail("Schema 1 contains unexpected task data; migration cancelled.");
        WorkspaceModel candidate;
        candidate.id = root.getProperty("id", {}).toString();
        candidate.name = root.getProperty("name", {}).toString();
        candidate.timezone = root.getProperty("timezone", {}).toString();
        candidate.revision = static_cast<juce::int64>(root.getProperty("revision", -1));
        const auto read = readCatalog(root.getProperty("catalog", {}), candidate.catalog, false);
        if (read.failed()) return read;
        const auto pages = root.getProperty("pages", {});
        if (!pages.isArray()) return juce::Result::fail("Workspace pages are missing.");
        for (const auto& p : *pages.getArray())
        {
            if (!p.isObject()) return juce::Result::fail("Invalid page record.");
            WorkspacePage page { p.getProperty("id", {}).toString(), p.getProperty("parentPageId", {}).toString(),
                p.getProperty("title", {}).toString(), p.getProperty("kind", {}).toString(),
                p.getProperty("archivedAt", {}).toString(), {} };
            const auto blocks = p.getProperty("blocks", {});
            if (!blocks.isArray()) return juce::Result::fail("Page blocks are missing.");
            for (const auto& b : *blocks.getArray())
            {
                if (!b.isObject() || !b.getProperty("checked", {}).isBool())
                    return juce::Result::fail("Invalid block record.");
                page.blocks.push_back({ b.getProperty("id", {}).toString(), b.getProperty("type", {}).toString(),
                    b.getProperty("text", {}).toString(), b.getProperty("url", {}).toString(),
                    static_cast<bool>(b.getProperty("checked", false)), b.getProperty("name", {}).toString() });
            }
            candidate.pages.push_back(std::move(page));
        }

        if (version == 2)
        {
            const auto taskArray = root.getProperty("tasks", {});
            if (!taskArray.isArray()) return juce::Result::fail("Workspace tasks are missing.");
            for (const auto& t : *taskArray.getArray())
            {
                if (!t.isObject() || !t.getProperty("priority", {}).isInt())
                    return juce::Result::fail("Invalid task record.");
                WorkspaceTask task;
                task.id = t.getProperty("id", {}).toString(); task.ownerPageId = t.getProperty("ownerPageId", {}).toString();
                task.archivedAt = t.getProperty("archivedAt", {}).toString();
                task.properties.name = t.getProperty("name", {}).toString(); task.properties.notes = t.getProperty("notes", {}).toString();
                const auto status = t.getProperty("status", {}).toString();
                task.properties.status = parseStatus(status.toStdString());
                task.properties.priority = static_cast<int>(t.getProperty("priority", 0));
                if (status != label(task.properties.status)) return juce::Result::fail("Unknown task status.");
                const auto read = readBlocks(t.getProperty("blocks", {}), task.blocks);
                if (read.failed()) return read;
                const auto checkpoints = t.getProperty("checkpoints", {});
                if (!checkpoints.isArray()) return juce::Result::fail("Task checkpoints are missing.");
                for (const auto& c : *checkpoints.getArray())
                {
                    if (!c.isObject()) return juce::Result::fail("Invalid checkpoint record.");
                    const auto cpStatus = c.getProperty("status", {}).toString();
                    TaskCheckpoint cp {c.getProperty("id", {}).toString(), c.getProperty("name", {}).toString(),
                        c.getProperty("notes", {}).toString(), c.getProperty("archivedAt", {}).toString(),
                        parseStatus(cpStatus.toStdString())};
                    if (cpStatus != label(cp.status)) return juce::Result::fail("Unknown checkpoint status.");
                    task.checkpoints.push_back(std::move(cp));
                }
                candidate.tasks.push_back(std::move(task));
            }
        }
        const auto result = candidate.validate();
        if (result.wasOk()) output = std::move(candidate);
        return result;
    }
};
}
