#pragma once
#include "WorkspaceModel.h"
#include <map>
#include <set>

namespace arranger
{
// #68: portable data includes only local pages/notes and general tasks.
// No DAW catalog, file paths, media, credentials or external integrations.
enum class PortableScope { allLocal, pageSubtree, taskSubtree };
enum class PortableImportPolicy { preserveIds, createCopies };

struct PortablePackage
{
    WorkspaceModel contents;
    juce::String sourceWorkspaceId, selection;
};
struct PortablePreview
{
    int pages = 0, tasks = 0, events = 0, blocks = 0, checkpoints = 0, archived = 0;
    int conflictingIds = 0, ownerConflicts = 0, notesConflicts = 0;
    juce::String description() const
    {
        return "Pages: " + juce::String(pages) + "\nTasks: " + juce::String(tasks)
            + "\nEvents: " + juce::String(events) + "\nBlocks: " + juce::String(blocks) + "\nCheckpoints: " + juce::String(checkpoints)
            + "\nArchived records: " + juce::String(archived)
            + "\nConflicting IDs: " + juce::String(conflictingIds)
            + "\nOwner/parent conflicts: " + juce::String(ownerConflicts)
            + "\nNotes conflicts: " + juce::String(notesConflicts);
    }
};
class WorkspacePortable
{
public:
    static juce::Result makeExport(const WorkspaceModel& source, PortableScope scope,
        const juce::String& selectedId, juce::String& output, PortablePreview& preview)
    {
        auto result = source.validate();
        if (result.failed()) return result;
        WorkspaceModel subset;
        subset.id = source.id; subset.name = source.name;
        subset.timezone = source.timezone; subset.revision = source.revision;
        std::set<juce::String> included;
        if (scope == PortableScope::taskSubtree)
        {
            const auto* selected = source.findTask(selectedId);
            const auto* owner = selected ? source.findPage(selected->ownerPageId) : nullptr;
            if (!owner || owner->kind != "page")
                return juce::Result::fail("Selected local task or owner is missing.");
            auto anchor = *owner;
            anchor.parentPageId.clear(); anchor.blocks.clear();
            subset.pages.push_back(std::move(anchor));
            included.insert(owner->id);
            for (auto task : source.tasks)
            {
                if (task.id == selectedId || taskDescendant(source, task, selectedId))
                {
                    if (task.id == selectedId) task.parentTaskId.clear();
                    subset.tasks.push_back(std::move(task));
                }
            }
        }
        else
        {
            if (scope == PortableScope::pageSubtree)
            {
                const auto* root = source.findPage(selectedId);
                if (!root || (root->kind != "page" && root->kind != "note"))
                    return juce::Result::fail("Selected local page is missing.");
            }
            for (auto page : source.pages)
            {
                if (page.kind != "page" && page.kind != "note") continue;
                if (scope == PortableScope::pageSubtree
                    && page.id != selectedId && !pageDescendant(source, page, selectedId))
                    continue;
                included.insert(page.id);
                subset.pages.push_back(std::move(page));
            }
            for (auto& page : subset.pages)
                if (!included.contains(page.parentPageId)) page.parentPageId.clear();
            for (const auto& task : source.tasks)
                if (included.contains(task.ownerPageId)) subset.tasks.push_back(task);
        }
        // Global events belong to the workspace; page-linked events are
        // included only if their owner page is part of the export.
        if (scope != PortableScope::taskSubtree)
            for (const auto& event : source.events)
                if (scope == PortableScope::allLocal || included.contains(event.ownerPageId))
                    subset.events.push_back(event);
        result = subset.validate();
        if (result.failed()) return result;
        auto root = std::make_unique<juce::DynamicObject>();
        root->setProperty("format", "ArtManagerPortable");
        root->setProperty("schemaVersion", 1);
        root->setProperty("sourceWorkspaceId", source.id);
        const auto selection = scope == PortableScope::allLocal ? juce::String("all-local")
            : scope == PortableScope::pageSubtree ? juce::String("page:" + selectedId)
            : juce::String("task:" + selectedId);
        root->setProperty("selection", selection);
        root->setProperty("contents", juce::JSON::parse(subset.toJson()));
        output = juce::JSON::toString(juce::var(root.release()));
        preview = inspect(subset);
        return juce::Result::ok();
    }

    static juce::Result parse(const juce::String& text, PortablePackage& output)
    {
        const auto root = juce::JSON::parse(text);
        if (!root.isObject() || root.getProperty("format", {}).toString() != "ArtManagerPortable"
            || !root.getProperty("schemaVersion", {}).isInt()
            || static_cast<int>(root.getProperty("schemaVersion", 0)) != 1
            || !root.getProperty("contents", {}).isObject())
            return juce::Result::fail("Unknown or damaged portable format; no data changed.");
        PortablePackage candidate;
        candidate.sourceWorkspaceId = root.getProperty("sourceWorkspaceId", {}).toString();
        candidate.selection = root.getProperty("selection", {}).toString();
        if (candidate.sourceWorkspaceId.isEmpty() || candidate.selection.isEmpty())
            return juce::Result::fail("Portable source or selection is missing.");
        const auto contents = root.getProperty("contents", {});
        if (contents.getProperty("format", {}).toString() != "ArrangerManagerWorkspace"
            || !contents.getProperty("schemaVersion", {}).isInt()
            || (static_cast<int>(contents.getProperty("schemaVersion", 0)) != 2
                && static_cast<int>(contents.getProperty("schemaVersion", 0)) != 3))
            return juce::Result::fail("Portable contents must use workspace schema 2 or 3.");
        auto result = WorkspaceModel::fromJson(
            juce::JSON::toString(root.getProperty("contents", {})), candidate.contents);
        if (result.failed()) return result;
        if (candidate.contents.id != candidate.sourceWorkspaceId
            || !candidate.contents.catalog.songs.empty() || !candidate.contents.catalog.folders.empty())
            return juce::Result::fail("Portable file contains foreign catalog or inconsistent identity.");
        for (const auto& page : candidate.contents.pages)
            if (page.kind != "page" && page.kind != "note")
                return juce::Result::fail("Portable format contains a nonlocal page.");
        output = std::move(candidate);
        return juce::Result::ok();
    }

    static PortablePreview inspect(const WorkspaceModel& value, const WorkspaceModel* target = nullptr)
    {
        PortablePreview preview;
        for (const auto& page : value.pages)
        {
            ++preview.pages; preview.blocks += static_cast<int>(page.blocks.size());
            if (page.archivedAt.isNotEmpty()) ++preview.archived;
        }
        for (const auto& event : value.events)
        {
            ++preview.events;
            if (event.archivedAt.isNotEmpty()) ++preview.archived;
        }
        for (const auto& task : value.tasks)
        {
            ++preview.tasks; preview.blocks += static_cast<int>(task.blocks.size());
            preview.checkpoints += static_cast<int>(task.checkpoints.size());
            if (task.archivedAt.isNotEmpty()) ++preview.archived;
            for (const auto& cp : task.checkpoints) if (cp.archivedAt.isNotEmpty()) ++preview.archived;
        }
        if (target)
        {
            const auto occupied = ids(*target);
            for (const auto& id : ids(value)) if (id != value.id && occupied.contains(id))
                ++preview.conflictingIds;
            for (const auto& page : value.pages)
                if (const auto* current = target->findPage(page.id))
                    if (page.parentPageId != current->parentPageId) ++preview.ownerConflicts;
            for (const auto& task : value.tasks)
                if (const auto* current = target->findTask(task.id))
                {
                    if (task.ownerPageId != current->ownerPageId
                        || task.parentTaskId != current->parentTaskId) ++preview.ownerConflicts;
                    if (task.properties.notes != current->properties.notes) ++preview.notesConflicts;
                    for (const auto& cp : task.checkpoints)
                        for (const auto& existing : current->checkpoints)
                            if (cp.id == existing.id && cp.notes != existing.notes)
                                ++preview.notesConflicts;
                }
        }
        return preview;
    }

    static juce::Result merge(const PortablePackage& package, WorkspaceModel& target,
        PortableImportPolicy policy)
    {
        auto result = package.contents.validate();
        if (result.failed()) return result;
        if (!package.contents.catalog.songs.empty() || !package.contents.catalog.folders.empty())
            return juce::Result::fail("Only local workspace content can be imported.");
        if (policy == PortableImportPolicy::preserveIds && inspect(package.contents, &target).conflictingIds)
            return juce::Result::fail("Duplicate ID detected. Import as copies or cancel; nothing was replaced.");
        auto candidate = target;
        std::set<juce::String> occupied = ids(candidate);
        std::map<juce::String, juce::String> mapping;
        auto remap = [&](const juce::String& original)
        {
            if (original.isEmpty()) return juce::String();
            if (policy == PortableImportPolicy::preserveIds) return original;
            if (const auto found = mapping.find(original); found != mapping.end()) return found->second;
            juce::String fresh;
            do { fresh = juce::Uuid().toString(); } while (!occupied.insert(fresh).second);
            mapping.emplace(original, fresh);
            return fresh;
        };
        // Allocate fresh IDs before remapping any references. Hierarchies and
        // owner links may refer to records later in the serialized arrays.
        if (policy == PortableImportPolicy::createCopies)
        {
            for (const auto& page : package.contents.pages)
            {
                remap(page.id);
                for (const auto& block : page.blocks) remap(block.id);
            }
            for (const auto& event : package.contents.events) remap(event.id);
            for (const auto& task : package.contents.tasks)
            {
                remap(task.id);
                for (const auto& block : task.blocks) remap(block.id);
                for (const auto& cp : task.checkpoints) remap(cp.id);
            }
        }
        for (auto page : package.contents.pages)
        {
            page.id = remap(page.id); page.parentPageId = remap(page.parentPageId);
            for (auto& block : page.blocks) block.id = remap(block.id);
            candidate.pages.push_back(std::move(page));
        }
        for (auto task : package.contents.tasks)
        {
            task.id = remap(task.id);
            task.ownerPageId = remap(task.ownerPageId);
            task.parentTaskId = remap(task.parentTaskId);
            for (auto& block : task.blocks) block.id = remap(block.id);
            for (auto& cp : task.checkpoints) cp.id = remap(cp.id);
            candidate.tasks.push_back(std::move(task));
        }
        for (auto event : package.contents.events)
        {
            event.id = remap(event.id);
            event.ownerPageId = remap(event.ownerPageId);
            candidate.events.push_back(std::move(event));
        }
        result = candidate.validate();
        if (result.wasOk()) target = std::move(candidate);
        return result;
    }

    static juce::Result writeExport(const juce::File& destination, const juce::String& text)
    {
        PortablePackage checked;
        auto result = parse(text, checked);
        if (result.failed()) return result;
        // Do not permit a portable export to overwrite the live WorkspaceStore,
        // its previous-state backups or the application settings file.
        const auto filename = destination.getFileName().toLowerCase();
        if (filename.endsWith(".workspace.json") || filename.contains(".workspace.json.")
            || filename.endsWith(".settings"))
            return juce::Result::fail("Choose an export filename, not a live workspace or backup file.");
        if (destination.isDirectory()) return juce::Result::fail("Export destination is a directory.");
        result = destination.getParentDirectory().createDirectory();
        if (result.failed()) return result;
        juce::TemporaryFile temporary(destination);
        {
            auto stream = temporary.getFile().createOutputStream();
            if (!stream) return juce::Result::fail("Cannot open export temporary file.");
            const auto utf8 = text.toUTF8();
            if (!stream->write(utf8.getAddress(), static_cast<size_t>(text.getNumBytesAsUTF8())))
                return juce::Result::fail("Export write failed.");
            stream->flush();
            if (stream->getStatus().failed()) return stream->getStatus();
        }
        if (temporary.getFile().loadFileAsString() != text)
            return juce::Result::fail("Export verification failed.");
        if (!temporary.overwriteTargetFileWithTemporary())
            return juce::Result::fail("Cannot replace export file.");
        return juce::Result::ok();
    }

private:
    static bool pageDescendant(const WorkspaceModel& model, const WorkspacePage& page,
        const juce::String& ancestor)
    {
        std::set<juce::String> seen;
        auto id = page.parentPageId;
        while (id.isNotEmpty() && seen.insert(id).second)
        {
            if (id == ancestor) return true;
            const auto* parent = model.findPage(id);
            id = parent ? parent->parentPageId : juce::String();
        }
        return false;
    }
    static bool taskDescendant(const WorkspaceModel& model, const WorkspaceTask& task,
        const juce::String& ancestor)
    {
        std::set<juce::String> seen;
        auto id = task.parentTaskId;
        while (id.isNotEmpty() && seen.insert(id).second)
        {
            if (id == ancestor) return true;
            const auto* parent = model.findTask(id);
            id = parent ? parent->parentTaskId : juce::String();
        }
        return false;
    }
    static std::set<juce::String> ids(const WorkspaceModel& value)
    {
        std::set<juce::String> used {value.id};
        for (const auto& page : value.pages)
        {
            used.insert(page.id);
            for (const auto& block : page.blocks) used.insert(block.id);
        }
        for (const auto& task : value.tasks)
        {
            used.insert(task.id);
            for (const auto& block : task.blocks) used.insert(block.id);
            for (const auto& cp : task.checkpoints) used.insert(cp.id);
        }
        for (const auto& event : value.events) used.insert(event.id);
        for (const auto& folder : value.catalog.folders) used.insert(folder.id);
        for (const auto& song : value.catalog.songs)
        {
            used.insert(song.id); used.insert(song.pageId);
            for (const auto& task : song.tasks)
            {
                used.insert(task.id);
                for (const auto& cp : task.checkpoints) used.insert(cp.id);
            }
        }
        return used;
    }
};
}
