#include "WorkspaceController.h"
#include "SongNoteKeys.h"
#include "TaskTreeRows.h"
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

    // Move a second ordinary root under the transferred task, keeping its ID.
    REQUIRE(controller.execute([&](WorkspaceCommands& cmd) {
        return cmd.moveTask(two, pageA, {}, pageB, one);
    }).wasOk(), "Cross-page reparent");
    REQUIRE(model.findTask(two)->ownerPageId == pageB &&
        model.findTask(two)->parentTaskId == one &&
        model.findTask(two)->properties.name == "Master",
        "Reparent copied or corrupted the original task");
    REQUIRE(WorkspaceQueries::allTasks(model).size() == 1,
        "Child of a moved root appeared as a duplicate top-level task");
    // Persisted one-step Undo/Redo after a cross-page move, retaining revision order.
    const auto revisionBeforeUndo = model.revision;
    REQUIRE(controller.undoLast().wasOk(), "Undo of cross-page move");
    REQUIRE(model.findTask(two)->ownerPageId == pageA && model.findTask(two)->parentTaskId.isEmpty()
        && model.revision > revisionBeforeUndo, "Undo did not restore previous parent and revision");
    REQUIRE(controller.undoLast().wasOk(), "Redo of cross-page move");
    REQUIRE(model.findTask(two)->ownerPageId == pageB && model.findTask(two)->parentTaskId == one,
        "Redo did not restore task hierarchy");


    std::set<juce::String> expanded {"task:" + one, "task:" + child};
    const auto tree = taskTreeRows(model, WorkspaceQueries::allTasks(model), expanded);
    bool foundChild = false, foundGrandchild = false;
    for (const auto& row : tree)
    {
        if (!row.notes && !row.addChild && row.reference.taskId == child && row.depth == 1)
            foundChild = true;
        if (!row.notes && !row.addChild && row.reference.taskId == grandchild && row.depth == 2)
            foundGrandchild = true;
    }
    REQUIRE(foundChild && foundGrandchild, "Nested task tree projection lost the parent chain");

    // Regression #38: before the fix the progress meter only counted old
    // checkpoints and stayed at 0% when a nested task became DONE.
    const auto progress = [&]() {
        return WorkspaceQueries::taskProgress(model, *model.findTask(one));
    };
    REQUIRE(progress() == std::make_pair(0, 5),
        "Progress must count 3 nested tasks and 2 legacy checkpoints");
    REQUIRE(controller.execute([&](WorkspaceCommands& cmd) {
        return cmd.setReferenceStatus({{}, pageB, grandchild}, {}, Status::done);
    }).wasOk(), "Complete grandchild task");
    REQUIRE(progress() == std::make_pair(1, 5)
        && WorkspaceQueries::taskProgress(model, *model.findTask(child)) == std::make_pair(1, 1),
        "Nested DONE failed to advance parent and grandparent progress");
    REQUIRE(controller.execute([&](WorkspaceCommands& cmd) {
        return cmd.setReferenceStatus({{}, pageB, one}, firstCp, Status::done);
    }).wasOk(), "Complete legacy checkpoint");
    REQUIRE(progress() == std::make_pair(2, 5),
        "Legacy checkpoint progress must remain available alongside nested tasks");
    REQUIRE(controller.execute([&](WorkspaceCommands& cmd) {
        return cmd.setReferenceStatus({{}, pageB, one}, {}, Status::done);
    }).wasOk() && progress() == std::make_pair(2, 5),
        "Parent manual DONE must not silently replace derived progress");
    REQUIRE(controller.undoLast().wasOk() && progress() == std::make_pair(2, 5),
        "Undo task status must not corrupt progress");

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
    REQUIRE(progress() == std::make_pair(1, 3),
        "Archived nested subtree must be excluded without hiding root checkpoints");
    REQUIRE(controller.execute([&](WorkspaceCommands& cmd) {
        return cmd.createSubtask(grandchild, "Nope", deep);
    }).failed(), "Archived ancestor still accepted new descendants");
    REQUIRE(controller.execute([&](WorkspaceCommands& cmd) {
        return cmd.restoreTask(child);
    }).wasOk(), "Restore parent task");
    REQUIRE(!WorkspaceQueries::taskArchived(model, grandchild), "Restore lost descendant");
    REQUIRE(progress() == std::make_pair(2, 5),
        "Restore must return nested tasks to progress calculation");

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
    REQUIRE(old.findTask(two)->parentTaskId == one, "Moved task lost its parent on JSON round trip");

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
