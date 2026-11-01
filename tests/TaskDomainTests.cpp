#include "WorkspaceController.h"
#include "SongNoteKeys.h"
#include <iostream>

static bool check(bool ok, const char* message)
{
    if (!ok) std::cerr << message << '\n';
    return ok;
}
#define REQUIRE(condition, message) do { if (!check((condition), (message))) return 1; } while (false)

struct TemporaryWorkspace
{
    juce::File folder = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("ManagerTaskDomain-" + juce::Uuid().toString());
    TemporaryWorkspace() { folder.createDirectory(); }
    ~TemporaryWorkspace() { folder.deleteRecursively(); }
};

int main()
{
    using namespace arranger;
    TemporaryWorkspace temporary;
    const auto path = temporary.folder.getChildFile("workspace.json");
    WorkspaceModel model;
    WorkspaceStore store(path);
    REQUIRE(store.load({}, {}, model).wasOk(), "Cannot create workspace fixture");
    WorkspaceController controller(model, store);

    juce::String pageA, pageB, one, two, child, grandchild, deep, firstCp, secondCp;
    REQUIRE(controller.execute([&](WorkspaceCommands& cmd) {
        return cmd.createPage({}, "Recording", pageA);
    }).wasOk(), "Page A");
    REQUIRE(controller.execute([&](WorkspaceCommands& cmd) {
        return cmd.createPage({}, "Publishing", pageB);
    }).wasOk(), "Page B");
    REQUIRE(controller.execute([&](WorkspaceCommands& cmd) {
        return cmd.createTask(pageA, "Mix", one);
    }).wasOk(), "Task one");
    REQUIRE(controller.execute([&](WorkspaceCommands& cmd) {
        return cmd.createTask(pageA, "Master", two);
    }).wasOk(), "Task two");
    REQUIRE(controller.execute([&](WorkspaceCommands& cmd) {
        return cmd.createSubtask(one, "Drums", child);
    }).wasOk(), "Child task");
    REQUIRE(controller.execute([&](WorkspaceCommands& cmd) {
        return cmd.createSubtask(child, "Kick", grandchild);
    }).wasOk(), "Grandchild task");

    REQUIRE(controller.execute([&](WorkspaceCommands& cmd) {
        return cmd.createCheckpoint(one, "Old checkpoint", firstCp);
    }).wasOk(), "Legacy checkpoint preserved");
    REQUIRE(controller.execute([&](WorkspaceCommands& cmd) {
        return cmd.createCheckpoint(one, "Second checkpoint", secondCp);
    }).wasOk(), "Checkpoint #2");
    REQUIRE(controller.execute([&](WorkspaceCommands& cmd) {
        return cmd.reorderCheckpoint(one, secondCp, firstCp);
    }).wasOk(), "Checkpoint reorder");
    REQUIRE(model.findTask(one)->checkpoints[0].id == secondCp &&
        model.findTask(one)->checkpoints[1].id == firstCp, "Checkpoint IDs/order lost");

    REQUIRE(controller.execute([&](WorkspaceCommands& cmd) {
        const TaskReference ref {{}, pageA, child};
        return cmd.updateTaskText(ref, {}, TaskTextField::notes, {}, "Needs new samples");
    }).wasOk(), "Child note edit");
    const auto originalId = child;
    const auto originalNote = model.findTask(child)->properties.notes;
    REQUIRE(WorkspaceQueries::allTasks(model).size() == 2, "Nested children duplicated in root list");

    REQUIRE(controller.execute([&](WorkspaceCommands& cmd) {
        return cmd.moveTask(two, pageA, {}, pageA, {}, one);
    }).wasOk(), "Reorder root tasks");
    REQUIRE(model.tasks.front().id == two, "Insert-before position lost");

    REQUIRE(controller.execute([&](WorkspaceCommands& cmd) {
        return cmd.moveTask(one, pageA, {}, pageB, {}, {});
    }).wasOk(), "Transfer task and descendants to another page");
    REQUIRE(model.findTask(one)->ownerPageId == pageB &&
        model.findTask(child)->ownerPageId == pageB &&
        model.findTask(grandchild)->ownerPageId == pageB &&
        model.findTask(child)->id == originalId &&
        model.findTask(child)->properties.notes == originalNote &&
        model.findTask(child)->parentTaskId == one &&
        model.findTask(grandchild)->parentTaskId == child,
        "Move lost subtree, IDs, parentage or Notes");
    REQUIRE(model.findTask(one)->checkpoints[0].id == secondCp, "Move lost legacy checkpoints");

    const auto beforeFailed = model.toJson();
    REQUIRE(controller.execute([&](WorkspaceCommands& cmd) {
        return cmd.moveTask(one, pageB, {}, pageB, grandchild);
    }).failed() && model.toJson() == beforeFailed, "Task cycle was accepted or changed persisted model");
    REQUIRE(controller.execute([&](WorkspaceCommands& cmd) {
        return cmd.moveTask(one, pageA, {}, pageB, {});
    }).failed() && model.toJson() == beforeFailed, "Stale owner was accepted");
    REQUIRE(controller.execute([&](WorkspaceCommands& cmd) {
        return cmd.moveTask(one, pageB, {}, pageB, {}, child);
    }).failed() && model.toJson() == beforeFailed, "Non-sibling before target was accepted");
    REQUIRE(controller.execute([&](WorkspaceCommands& cmd) {
        return cmd.moveTask(one, pageB, {}, "missing", {});
    }).failed() && model.toJson() == beforeFailed, "Unknown destination page was accepted");

    REQUIRE(controller.execute([&](WorkspaceCommands& cmd) {
        return cmd.archiveTask(child);
    }).wasOk(), "Archive parent task");
    REQUIRE(WorkspaceQueries::taskArchived(model, grandchild), "Archived ancestor did not hide descendants");
    REQUIRE(controller.execute([&](WorkspaceCommands& cmd) {
        return cmd.createSubtask(grandchild, "Nope", deep);
    }).failed(), "Archived ancestor still accepted new descendants");
    REQUIRE(controller.execute([&](WorkspaceCommands& cmd) {
        return cmd.restoreTask(child);
    }).wasOk(), "Restore parent task");
    REQUIRE(!WorkspaceQueries::taskArchived(model, grandchild), "Restore lost descendant");

    // At most eight parent edges; the ninth must fail without changing JSON.
    auto previous = grandchild;
    for (int i = 0; i < 6; ++i)
    {
        const auto name = "Level " + juce::String(i + 3);
        juce::String nested;
        REQUIRE(controller.execute([&](WorkspaceCommands& cmd) {
            return cmd.createSubtask(previous, name, nested);
        }).wasOk(), "Depth at or below eight should be supported");
        previous = nested;
    }
    const auto beforeDepth = model.toJson();
    REQUIRE(controller.execute([&](WorkspaceCommands& cmd) {
        return cmd.createSubtask(previous, "Depth nine", deep);
    }).failed() && model.toJson() == beforeDepth, "Depth nine corrupted model");

    // Existing schema 2 tasks (without parentTaskId) continue to parse as roots.
    WorkspaceModel old;
    REQUIRE(WorkspaceModel::fromJson(model.toJson(), old).wasOk(), "New schema-2 extension cannot round trip");
    REQUIRE(old.findTask(grandchild)->parentTaskId == child, "Parent relationship missing after parse");
    REQUIRE(old.findTask(two)->parentTaskId.isEmpty(), "Old root gained an unexpected parent");

    WorkspaceStore restart(path);
    WorkspaceModel loaded;
    REQUIRE(restart.load({}, {}, loaded).wasOk(), "Restart failed");
    REQUIRE(loaded.toJson() == model.toJson(), "Restart changed task tree or order");
    REQUIRE(loaded.findTask(child)->properties.notes == originalNote &&
        loaded.findTask(one)->checkpoints[0].id == secondCp, "Restart lost Notes or checkpoint order");
    REQUIRE(store.backupFile().existsAsFile(), "Previous-workspace backup missing");

    // Historical Studio Pro catalog remains wholly local and distinct.
    const auto missingSong = temporary.folder.getChildFile("unavailable.song");
    const auto source = missingSong.getFullPathName();
    REQUIRE(!missingSong.existsAsFile(), "Legacy song fixture unexpectedly exists");
    REQUIRE(controller.execute([&](WorkspaceCommands& cmd) {
        (void) cmd;
        return juce::Result::ok();
    }).wasOk(), "Controller cannot transact");
    auto original = model;
    original.catalog.addSong(source);
    original.catalog.addTask(source, "Legacy Song Task");
    original.catalog.setLocalNote(source,
        taskNoteKey(original.catalog.songs.back().tasks.front().id), "Legacy Note");
    original.reconcileCatalogPages();
    REQUIRE(original.validate().wasOk() && !missingSong.existsAsFile(), "Offline legacy data was not preserved");
    REQUIRE(original.catalog.localNote(source,
        taskNoteKey(original.catalog.songs.back().tasks.front().id)) == "Legacy Note", "Legacy Notes key changed");
    std::cout << "Task hierarchy, sibling reorder, cross-page move, archived owner, depth and restart passed\n";
    return 0;
}
