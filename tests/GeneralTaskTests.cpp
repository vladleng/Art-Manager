#include "WorkspaceController.h"
#include "BlockEditorSession.h"
#include "SongProgress.h"
#include "TaskTreeRows.h"
#include <iostream>
static bool check(bool value, const char* message) { if (!value) std::cerr << message << '\n'; return value; }
#define REQUIRE(value, message) do { if (!check((value), message)) return 1; } while (false)
struct TestDirectory
{
    juce::File path = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("ArrangerTaskTest-" + juce::Uuid().toString());
    TestDirectory() { path.createDirectory(); }
    ~TestDirectory() { path.deleteRecursively(); }
};
int main()
{
    using namespace arranger;
    TestDirectory dir;
    const auto file = dir.path.getChildFile("workspace.json");
    WorkspaceModel old;
    old.catalog.addSong("C:\\Songs\\First.song");
    const auto songTask = old.catalog.addTask(old.catalog.songs[0].path, "DAW task");
    old.catalog.addCheckpoint(old.catalog.songs[0].path, songTask, "DAW checkpoint");
    old.catalog.setLocalNote(old.catalog.songs[0].path, "task:" + songTask, "DAW note");
    old.reconcileCatalogPages();
    WorkspaceCommands original(old); juce::String root, child;
    REQUIRE(original.createPage({}, "Root", root).wasOk() && original.createPage(root, "Child", child).wasOk(), "Pages");
    old.findPage(root)->blocks.push_back({juce::Uuid().toString(), "text", juce::String::fromUTF8("Старый текст"), {}, false, "Existing"});
    old.revision = 41;
    auto v1 = juce::JSON::parse(old.toJson());
    v1.getDynamicObject()->setProperty("schemaVersion", 1); v1.getDynamicObject()->removeProperty("tasks");
    const auto source = juce::JSON::toString(v1) + "\n";
    REQUIRE(file.replaceWithData(source.toRawUTF8(), static_cast<size_t>(source.getNumBytesAsUTF8())), "Write schema 1 fixture");
    WorkspaceStore store(file); WorkspaceModel model;
    REQUIRE(store.load("invalid stale settings", {}, model).wasOk(), "Schema 1 migration");
    REQUIRE(store.schemaOneFile().loadFileAsString() == source && store.backupFile().loadFileAsString() == source, "Exact schema 1 backup");
    REQUIRE(static_cast<int>(juce::JSON::parse(file.loadFileAsString()).getProperty("schemaVersion", 0)) == 2, "Schema 2 not written");
    REQUIRE(model.id == old.id && model.revision == 42 && model.catalog.toJson() == old.catalog.toJson()
        && model.findPage(root)->blocks == old.findPage(root)->blocks && model.tasks.empty(), "Migration changed data");
    WorkspaceStore restart(file); WorkspaceModel restarted;
    REQUIRE(restart.load({}, {}, restarted).wasOk() && restarted.toJson() == model.toJson(), "Repeated load remigrated");
    WorkspaceController controller(model, store);
    juce::String first, second, cp;
    REQUIRE(controller.execute([&](WorkspaceCommands& c) { return c.createTask(root, juce::String::fromUTF8("Публикация"), first); }).wasOk(), "Create task");
    REQUIRE(controller.execute([&](WorkspaceCommands& c) { return c.createTask(child, "Other", second); }).wasOk(), "Second task");
    const auto catalog = model.catalog.toJson();
    auto refs = WorkspaceQueries::allTasks(model);
    REQUIRE(refs.size() == 3, "All tasks copies or omits records");
    auto ref = TaskReference{{}, root, first};
    REQUIRE(WorkspaceQueries::generalTask(model, ref) == model.findTask(first), "Task source identity");
    auto wrong = ref; wrong.ownerPageId = child;
    REQUIRE(WorkspaceQueries::generalTask(model, wrong) == nullptr, "Wrong owner accepted");
    const auto properties = model.findTask(first)->properties;
    auto changed = properties; changed.status = Status::wip; changed.priority = 1; changed.notes = juce::String::fromUTF8("Проверить монтаж");
    REQUIRE(controller.execute([&](WorkspaceCommands& c) { return c.updateTask(first, properties, changed); }).wasOk(), "Properties save");
    REQUIRE(WorkspaceQueries::taskStatus(model, ref) == Status::wip && model.findTask(first)->properties.priority == 1, "View stale");
    const auto beforeInvalid = model.toJson();
    REQUIRE(controller.execute([&](WorkspaceCommands& c) { return c.updateTask(first, properties, changed); }).failed(), "Stale properties accepted");
    auto invalid = changed; invalid.name = " "; invalid.priority = 9;
    REQUIRE(controller.execute([&](WorkspaceCommands& c) { return c.updateTask(first, changed, invalid); }).failed()
        && model.toJson() == beforeInvalid, "Invalid properties mutated model");
    REQUIRE(controller.execute([&](WorkspaceCommands& c) { juce::String id; return c.createTask("missing", "Invalid", id); }).failed(), "Missing owner accepted");
    REQUIRE(controller.execute([&](WorkspaceCommands& c) { juce::String id; return c.createTask(model.catalog.songs[0].pageId, "Invalid", id); }).failed(), "DAW owner accepted");
    REQUIRE(controller.execute([&](WorkspaceCommands& c) { return c.createCheckpoint(first, "Verse", cp); }).wasOk(), "Create checkpoint");
    const auto checkpoint = model.findTask(first)->checkpoints[0];
    auto cpChanged = checkpoint; cpChanged.status = Status::done; cpChanged.notes = "Ready";
    REQUIRE(controller.execute([&](WorkspaceCommands& c) { return c.updateCheckpoint(first, checkpoint, cpChanged); }).wasOk(), "Update checkpoint");
    REQUIRE(WorkspaceQueries::taskProgress(*model.findTask(first)) == std::make_pair(1, 1)
        && model.findTask(first)->properties.status == Status::wip, "Checkpoint incorrectly changes task status");
    std::set<juce::String> expanded {"task:" + first, "checkpoint:" + cp};
    const auto treeSource = model.toJson();
    auto tree = taskTreeRows(model, WorkspaceQueries::allTasks(model), expanded);
    REQUIRE(tree.size() == 6 && model.toJson() == treeSource, "Expansion mutated model or omitted notes/checkpoints");
    REQUIRE(tree[3].checkpointId == cp && !tree[3].notes && tree[3].depth == 1
        && tree[4].notes && tree[4].depth == 2, "Checkpoint nesting/identity");
    auto collapsed = taskTreeRows(model, WorkspaceQueries::allTasks(model), {});
    REQUIRE(collapsed.size() == 3, "Collapsed rows leak descendants");
    auto detailRows = taskTreeRows(model, {ref}, expanded, true);
    REQUIRE(detailRows.size() == 2 && detailRows[0].depth == 0 && detailRows[1].depth == 1, "Detail checkpoint expansion");
    auto wrongRows = taskTreeRows(model, {wrong}, expanded);
    REQUIRE(wrongRows.empty(), "Tree accepts wrong owner");
    BlockEditorSession editor;
    REQUIRE(editor.bind("task:" + first, [&]() { return std::optional(model.findTask(first)->blocks); },
        [&](const auto& expected, const auto& blocks) { return controller.execute([&](WorkspaceCommands& c) { return c.replaceTaskBlocks(first, expected, blocks); }); }).wasOk(), "Description binding");
    BlockEditorSession::Blocks blocks {{juce::Uuid().toString(), "checklist", "Description check", {}, true, "Description"}};
    editor.stage(blocks); REQUIRE(editor.flush().wasOk(), "Description save");
    REQUIRE(model.catalog.toJson() == catalog && model.findTask(first)->properties.status == Status::wip, "Description modified statuses/DAW");
    REQUIRE(editor.undo().wasOk() && model.findTask(first)->blocks.empty() && model.findTask(first)->checkpoints[0] == cpChanged, "Undo modified checkpoint");
    REQUIRE(editor.redo().wasOk(), "Description redo");
    REQUIRE(controller.execute([&](WorkspaceCommands& c) { return c.archiveCheckpoint(first, cp); }).wasOk(), "Archive checkpoint");
    REQUIRE(WorkspaceQueries::taskProgress(*model.findTask(first)) == std::make_pair(0, 0), "Archived checkpoint counted");
    REQUIRE(taskTreeRows(model, {ref}, expanded).size() == 2, "Archived checkpoint leaks into expanded tree");
    REQUIRE(controller.execute([&](WorkspaceCommands& c) { return c.restoreCheckpoint(first, cp); }).wasOk()
        && model.findTask(first)->checkpoints[0].id == cp, "Restore checkpoint ID");
    const auto taskBeforeArchive = *model.findTask(first);
    REQUIRE(controller.execute([&](WorkspaceCommands& c) { return c.archiveTask(first); }).wasOk(), "Archive task");
    REQUIRE(WorkspaceQueries::allTasks(model).size() == 2 && WorkspaceQueries::taskArchived(model, first), "Task archive visibility");
    REQUIRE(controller.execute([&](WorkspaceCommands& c) { return c.updateTask(first, changed, changed); }).failed(), "Archived task edited");
    REQUIRE(controller.execute([&](WorkspaceCommands& c) { return c.archivePage(root); }).wasOk(), "Archive owner");
    REQUIRE(WorkspaceQueries::allTasks(model).size() == 1, "Owner archive did not hide descendant tasks");
    REQUIRE(taskTreeRows(model, {ref}, expanded).empty(), "Archived owner leaks expanded rows");
    REQUIRE(controller.execute([&](WorkspaceCommands& c) { return c.restoreTask(first); }).failed(), "Task restored under archived owner");
    REQUIRE(controller.execute([&](WorkspaceCommands& c) { return c.restorePage(root); }).wasOk(), "Restore page");
    REQUIRE(WorkspaceQueries::taskArchived(model, first) && !WorkspaceQueries::taskArchived(model, second), "Own task archive lost");
    REQUIRE(controller.execute([&](WorkspaceCommands& c) { return c.restoreTask(first); }).wasOk(), "Restore task");
    REQUIRE(model.findTask(first)->id == first && model.findTask(first)->blocks == taskBeforeArchive.blocks
        && model.findTask(first)->checkpoints == taskBeforeArchive.checkpoints && model.findTask(first)->properties == taskBeforeArchive.properties, "Restore lost task fields/IDs");
    REQUIRE(model.catalog.toJson() == catalog && model.validate().wasOk(), "Dangling references or changed DAW");
    WorkspaceStore reload(file); WorkspaceModel loaded;
    REQUIRE(reload.load({}, {}, loaded).wasOk() && loaded.toJson() == model.toJson(), "Task restart/round trip");
    REQUIRE(store.schemaOneFile().loadFileAsString() == source, "Permanent migration backup changed");
    auto bad = model; bad.tasks[0].ownerPageId = "missing";
    REQUIRE(bad.validate().failed(), "Dangling task owner accepted");
    bad = model; bad.tasks[0].blocks[0].id = bad.tasks[1].id;
    REQUIRE(bad.validate().failed(), "Duplicate global block/task ID accepted");
    // Disk conflict must not commit new properties to RAM.
    const auto beforeConflict = model.toJson();
    REQUIRE(file.replaceWithText(beforeConflict + "\n"), "External disk change");
    changed = model.findTask(first)->properties; changed.notes = "Unsaved";
    REQUIRE(controller.execute([&](WorkspaceCommands& c) { return c.updateTask(first, model.findTask(first)->properties, changed); }).failed()
        && model.toJson() == beforeConflict, "Failed task save mutated live model");
    auto missingTasks = juce::JSON::parse(model.toJson()); missingTasks.getDynamicObject()->removeProperty("tasks");
    WorkspaceModel sentinel; sentinel.id = "sentinel";
    REQUIRE(WorkspaceModel::fromJson(juce::JSON::toString(missingTasks), sentinel).failed() && sentinel.id == "sentinel", "Schema 2 missing tasks accepted");
    // Preserve a schema 1 source byte-for-byte even with a UTF-8 BOM.
    const auto bomFile = dir.path.getChildFile("bom-workspace.json");
    const unsigned char bom[] = {0xef, 0xbb, 0xbf};
    REQUIRE(bomFile.replaceWithData(bom, sizeof(bom))
        && bomFile.appendData(source.toRawUTF8(), static_cast<size_t>(source.getNumBytesAsUTF8())), "BOM fixture");
    juce::MemoryBlock originalBytes, backupBytes;
    REQUIRE(bomFile.loadFileAsData(originalBytes), "Read BOM bytes");
    WorkspaceStore bomStore(bomFile); WorkspaceModel bomModel;
    REQUIRE(bomStore.load({}, {}, bomModel).wasOk()
        && bomStore.schemaOneFile().loadFileAsData(backupBytes) && originalBytes == backupBytes, "Migration backup changed source bytes");

    // An unwritable migration backup must preserve the source exactly.
    const auto blockedFile = dir.path.getChildFile("blocked-migration.json");
    REQUIRE(blockedFile.replaceWithData(source.toRawUTF8(), static_cast<size_t>(source.getNumBytesAsUTF8())), "Blocked fixture");
    WorkspaceStore blocked(blockedFile); blocked.schemaOneFile().createDirectory();
    REQUIRE(blocked.load({}, {}, sentinel).failed() && blockedFile.loadFileAsString() == source, "Migration backup failure overwrote schema 1");
    std::cout << "General tasks, description, archives, migration and persistence passed\n";
    return 0;
}

