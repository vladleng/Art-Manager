#include "WorkspaceController.h"
#include "WorkspacePortable.h"
#include <iostream>

#define CHECK(expr) do { if (!(expr)) { std::cerr << "FAIL line " << __LINE__ << ": " << #expr << "\n"; return 1; } } while (false)
struct Sandbox
{
    juce::File root = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("ArtManagerPortable-" + juce::Uuid().toString());
    Sandbox() { root.createDirectory(); }
    ~Sandbox() { root.deleteRecursively(); }
};
int main()
{
    using namespace arranger;
    Sandbox sandbox;
    WorkspaceModel source;
    WorkspaceCommands commands(source);
    juce::String root, nested, note, first, child, other, checkpoint;
    CHECK(commands.createPage({}, "Project A", root).wasOk());
    CHECK(commands.createPage(root, "Child project", nested).wasOk());
    CHECK(commands.createNotePage(root, "Ideas", note).wasOk());
    CHECK(commands.createTask(root, "Arrange", first).wasOk());
    CHECK(commands.createSubtask(first, "Harmony", child).wasOk());
    CHECK(commands.createTask(nested, "Recording", other).wasOk());
    CHECK(commands.createCheckpoint(first, "Verse", checkpoint).wasOk());
    auto* rootPage = source.findPage(root);
    rootPage->blocks.push_back({"legacy-block", "text", juce::String::fromUTF8("Старый текст"), {}, false});
    source.findPage(note)->blocks.push_back({"note-block", "heading", "Ideas", {}, false});
    auto* task = source.findTask(first);
    task->properties.notes = juce::String::fromUTF8("Оставить комментарий");
    task->properties.priority = 1;
    task->blocks.push_back({"task-block", "checklist", "Done", {}, true});
    task->checkpoints.front().notes = "Checkpoint note";
    task->checkpoints.front().archivedAt = "2026-10-01";
    source.findTask(other)->archivedAt = "2026-10-30";
    CHECK(source.validate().wasOk());

    juce::String text;
    PortablePreview overview;
    CHECK(WorkspacePortable::makeExport(source, PortableScope::allLocal, {}, text, overview).wasOk());
    CHECK(overview.pages == 3 && overview.tasks == 3
        && overview.blocks == 3 && overview.checkpoints == 1 && overview.archived == 2);
    PortablePackage imported;
    CHECK(WorkspacePortable::parse(text, imported).wasOk());
    CHECK(imported.sourceWorkspaceId == source.id && imported.selection == "all-local");
    CHECK(imported.contents.toJson() == source.toJson());

    WorkspaceModel target;
    const auto targetBefore = target.toJson();
    CHECK(WorkspacePortable::inspect(imported.contents, &target).conflictingIds == 0);
    CHECK(WorkspacePortable::merge(imported, target, PortableImportPolicy::preserveIds).wasOk());
    CHECK(target.findTask(first)->properties.notes == source.findTask(first)->properties.notes
        && target.findTask(child)->parentTaskId == first
        && target.findTask(first)->checkpoints.front().id == checkpoint
        && target.findTask(first)->checkpoints.front().archivedAt == "2026-10-01"
        && target.findPage(root)->blocks.front().id == "legacy-block");
    const auto keptOnce = target.toJson();
    CHECK(WorkspacePortable::inspect(imported.contents, &target).conflictingIds > 0);
    CHECK(WorkspacePortable::merge(imported, target, PortableImportPolicy::preserveIds).failed());
    CHECK(target.toJson() == keptOnce);
    CHECK(WorkspacePortable::merge(imported, target, PortableImportPolicy::createCopies).wasOk());
    CHECK(target.pages.size() == 6 && target.tasks.size() == 6);
    const auto& copiedRoot = target.pages[3];
    const auto& copiedTask = target.tasks[3];
    const auto& copiedChild = target.tasks[4];
    CHECK(copiedRoot.id != root
        && copiedTask.id != first && copiedTask.ownerPageId == copiedRoot.id
        && copiedChild.parentTaskId == copiedTask.id
        && copiedTask.properties.notes == source.findTask(first)->properties.notes
        && copiedTask.blocks.front().id != "task-block"
        && copiedTask.checkpoints.front().id != checkpoint
        && target.validate().wasOk());
    CHECK(targetBefore != target.toJson());

    juce::String subsetText;
    CHECK(WorkspacePortable::makeExport(source, PortableScope::pageSubtree, nested, subsetText, overview).wasOk());
    CHECK(overview.pages == 1 && overview.tasks == 1 && overview.blocks == 0);
    PortablePackage subtree;
    CHECK(WorkspacePortable::parse(subsetText, subtree).wasOk());
    CHECK(subtree.contents.pages.front().parentPageId.isEmpty()
        && subtree.contents.tasks.front().id == other);
    CHECK(WorkspacePortable::makeExport(source, PortableScope::taskSubtree, first, subsetText, overview).wasOk());
    CHECK(overview.pages == 1 && overview.tasks == 2 && overview.checkpoints == 1);
    CHECK(WorkspacePortable::parse(subsetText, subtree).wasOk());
    CHECK(subtree.contents.pages.front().blocks.empty()
        && subtree.contents.tasks.front().parentTaskId.isEmpty());
    CHECK(WorkspacePortable::makeExport(source, PortableScope::pageSubtree, "missing", subsetText, overview).failed());

    auto unknown = juce::JSON::parse(text);
    unknown.getDynamicObject()->setProperty("schemaVersion", 999);
    CHECK(WorkspacePortable::parse(juce::JSON::toString(unknown), subtree).failed());
    CHECK(WorkspacePortable::parse("{bad json", subtree).failed());
    unknown = juce::JSON::parse(text);
    unknown.getDynamicObject()->setProperty("sourceWorkspaceId", "wrong");
    CHECK(WorkspacePortable::parse(juce::JSON::toString(unknown), subtree).failed());

    const auto exportedFile = sandbox.root.getChildFile("export.artmanager.json");
    CHECK(WorkspacePortable::writeExport(exportedFile, text).wasOk());
    CHECK(exportedFile.loadFileAsString() == text);
    CHECK(WorkspacePortable::writeExport(sandbox.root, text).failed());
    auto* saved = target.findTask(first);
    saved->parentTaskId = "missing-parent";
    CHECK(target.validate().failed());
    CHECK(WorkspacePortable::merge(imported, target, PortableImportPolicy::preserveIds).failed());
    CHECK(target.findTask(first)->parentTaskId == "missing-parent"); // No partial mutation.

    // Legacy blocks are moved, not copied. Controller + Store guarantee a
    // verified atomic write, previous-state backup and undo/redo.
    WorkspaceModel persistent;
    const auto file = sandbox.root.getChildFile("live.workspace.json");
    WorkspaceStore store(file);
    CHECK(store.load({}, {}, persistent).wasOk());
    WorkspaceController controller(persistent, store);
    juce::String legacyPage;
    CHECK(controller.execute([&](WorkspaceCommands& c)
        { return c.createPage({}, "Original page", legacyPage); }).wasOk());
    persistent.findPage(legacyPage)->blocks = {
        {"block-original-1", "text", juce::String::fromUTF8("Текст из старой страницы"), {}, false},
        {"block-original-2", "checklist", "Check me", {}, true}};
    CHECK(store.save(persistent).wasOk());
    const auto beforeMove = file.loadFileAsString();
    juce::String createdNote;
    CHECK(controller.execute([&](WorkspaceCommands& c)
        { return c.moveLegacyBlocksToNote(legacyPage, createdNote); }).wasOk());
    CHECK(persistent.findPage(legacyPage)->blocks.empty());
    CHECK(persistent.findPage(createdNote)->kind == "note"
        && persistent.findPage(createdNote)->parentPageId == legacyPage
        && persistent.findPage(createdNote)->blocks[0].id == "block-original-1"
        && persistent.findPage(createdNote)->blocks[1].checked);
    CHECK(store.backupFile().loadFileAsString() == beforeMove);
    WorkspaceModel restart;
    WorkspaceStore reopened(file);
    CHECK(reopened.load({}, {}, restart).wasOk()
        && restart.findPage(createdNote)->blocks[0].id == "block-original-1");
    CHECK(controller.undoLast().wasOk()
        && persistent.findPage(legacyPage)->blocks.size() == 2
        && persistent.findPage(createdNote) == nullptr);
    CHECK(controller.undoLast().wasOk()
        && persistent.findPage(createdNote) != nullptr);
    CHECK(controller.undoLast().wasOk()
        && persistent.findPage(createdNote) == nullptr
        && persistent.findPage(legacyPage)->blocks.size() == 2);
    // Block a backup write; the domain command succeeds on a private
    // candidate, but the failed storage transaction changes neither state.
    store.backupFile().deleteFile();
    store.backupFile().createDirectory();
    const auto liveFileBeforeFailedMove = file.loadFileAsString();
    CHECK(controller.execute([&](WorkspaceCommands& c)
        { juce::String id; return c.moveLegacyBlocksToNote(legacyPage, id); }).failed());
    CHECK(file.loadFileAsString() == liveFileBeforeFailedMove);
    CHECK(persistent.findPage(createdNote) == nullptr
        && persistent.findPage(legacyPage)->blocks.size() == 2);
    std::cout << "Portable export/import, conflict handling, IDs, archive, legacy blocks, rollback PASS\n";
    return 0;
}
