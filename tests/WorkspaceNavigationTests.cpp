#include "WorkspaceController.h"
#include "WorkspaceViewRouting.h"
#include <iostream>

static bool check(bool condition, const char* message)
{
    if (!condition) std::cerr << message << '\n';
    return condition;
}
struct TestDirectory
{
    juce::File path = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("ArrangerNavigationTest-" + juce::Uuid().toString());
    TestDirectory() { path.createDirectory(); }
    ~TestDirectory() { path.deleteRecursively(); }
};

int main()
{
    using namespace arranger;
    WorkspaceModel model;
    model.catalog.addSong("C:\\Songs\\One.song");
    model.catalog.addSong("C:\\Songs\\Two.song");
    const auto taskId = model.catalog.addTask(model.catalog.songs[0].path, "Harmony");
    model.catalog.addCheckpoint(model.catalog.songs[0].path, taskId, "Verse");
    model.catalog.addTask(model.catalog.songs[1].path, "Mix");
    model.reconcileCatalogPages();
    WorkspaceCommands commands(model);
    juce::String root, child, grandchild, other;
    if (!check(commands.createPage({}, juce::String::fromUTF8("Контент"), root).wasOk(), "Root creation failed")
        || !check(commands.createPage(root, "YouTube", child).wasOk(), "Child creation failed")
        || !check(commands.createPage(child, "Shorts", grandchild).wasOk(), "Grandchild creation failed")
        || !check(commands.createPage({}, "Other", other).wasOk(), "Other root failed")) return 1;
    // Task creation should not automatically navigate to the obsolete
    // full-page TaskDetails screen; the new row must stay visible.
    if (!check(viewAfterTaskCreation("view:tasks", root) == "view:tasks",
            "Creating from All Tasks must stay in the task list")
        || !check(viewAfterTaskCreation("pageTasks:" + root, root) == "pageTasks:" + root,
            "Creating from the owner page must stay in its task list")
        || !check(viewAfterTaskCreation("pageTasks:" + other, root) == "pageTasks:" + root,
            "Creating for a different owner must show the right task list")
        || !check(viewAfterTaskCreation("page:" + root, root) == "pageTasks:" + root,
            "Creating while blocks are visible must show the tasks")
        || !check(viewAfterTaskCreation("task:previous", root) == "pageTasks:" + root,
            "Task creation must never open a full-page task detail")) return 1;
    model.findPage(child)->blocks.push_back({"kept-block", "text", "Text", {}, false});
    const auto original = model.toJson();
    const auto childId = child;
    if (!check(commands.movePage(root, grandchild).failed(), "Descendant cycle accepted")
        || !check(commands.movePage(root, root).failed(), "Self cycle accepted")
        || !check(commands.movePage(child, "missing").failed(), "Dangling parent accepted")
        || !check(commands.movePage(child, model.catalog.songs[0].pageId).failed(), "DAW page parent accepted")
        || !check(commands.renamePage(root, "  ").failed(), "Empty rename accepted")
        || !check(model.toJson() == original, "Failed command changed model")
        || !check(commands.renamePage(child, juce::String::fromUTF8("Видео")).wasOk(), "UTF-8 rename failed")
        || !check(child == childId && model.findPage(child)->blocks[0].id == "kept-block", "Rename lost identity/content")) return 1;

    auto rows = WorkspaceQueries::activePages(model);
    if (!check(rows.size() == 4 && rows[0].id == root && rows[1].id == child && rows[2].depth == 2,
            "Tree ordering or depth failed")
        || !check(WorkspaceQueries::breadcrumb(model, grandchild)
            == juce::String::fromUTF8("Контент / Видео / Shorts"), "Breadcrumb failed")
        || !check(commands.movePage(child, other).wasOk(), "Subtree move failed")
        || !check(model.findPage(grandchild)->parentPageId == child, "Subtree ownership changed")
        || !check(WorkspaceQueries::breadcrumb(model, grandchild)
            == juce::String::fromUTF8("Other / Видео / Shorts"), "Moved breadcrumb failed")) return 1;

    if (!check(commands.archivePage(child).wasOk(), "Archive failed")
        || !check(WorkspaceQueries::isArchived(model, grandchild), "Archive did not hide descendants")
        || !check(WorkspaceQueries::archivedRoots(model).size() == 1, "Archive roots duplicated")
        || !check(WorkspaceQueries::activePages(model).size() == 2, "Archived pages remained visible")
        || !check(commands.renamePage(grandchild, "bad").failed(), "Archived descendant renamed")
        || !check(commands.createPage(child, "bad", root).failed(), "Created page under archive")
        || !check(commands.movePage(other, child).failed(), "Moved page into archive")
        || !check(commands.restorePage(child).wasOk(), "Restore failed")
        || !check(!WorkspaceQueries::isArchived(model, grandchild), "Restore did not reveal descendants")
        || !check(model.findPage(child)->blocks[0].text == "Text", "Archive lost blocks")) return 1;

    // Independently archived child stays archived after ancestor restore.
    if (!check(commands.archivePage(grandchild).wasOk(), "Independent child archive failed")
        || !check(commands.archivePage(other).wasOk(), "Ancestor archive failed")
        || !check(commands.restorePage(grandchild).failed(), "Child restored inside archived ancestor")
        || !check(commands.restorePage(other).wasOk(), "Ancestor restore failed")
        || !check(WorkspaceQueries::isArchived(model, grandchild), "Ancestor restore cleared child archive")
        || !check(commands.restorePage(grandchild).wasOk(), "Independent child restore failed")
        || !check(model.validate().wasOk(), "Page operations broke workspace")) return 1;
    if (!check(commands.renamePage(model.catalog.songs[0].pageId, "bad").failed(), "DAW page renamed by ordinary command")
        || !check(commands.archivePage(model.catalog.songs[0].pageId).failed(), "DAW page archived by ordinary command")) return 1;

    const auto refs = WorkspaceQueries::allTasks(model);
    if (!check(refs.size() == 2 && refs[0].taskId == taskId, "All-tasks query duplicated/missed records")
        || !check(WorkspaceQueries::task(model, refs[0]) == &model.catalog.songs[0].tasks[0],
            "All-tasks view copied task instead of resolving source")) return 1;
    model.catalog.setTaskStatus(model.catalog.songs[0].path, taskId, Status::done);
    if (!check(WorkspaceQueries::task(model, refs[0])->status == Status::done, "View did not reflect source mutation")) return 1;
    auto wrongOwner = refs[0]; wrongOwner.ownerPageId = "wrong";
    if (!check(WorkspaceQueries::task(model, wrongOwner) == nullptr, "Wrong task owner was accepted")) return 1;

    TestDirectory directory;
    const auto file = directory.path.getChildFile("workspace.json");
    WorkspaceStore store(file);
    WorkspaceModel persisted;
    if (!check(store.load({}, {}, persisted).wasOk(), "Store setup failed")) return 1;
    WorkspaceController controller(persisted, store);
    juce::String pageId;
    if (!check(controller.execute([&](WorkspaceCommands& c)
        { return c.createPage({}, juce::String::fromUTF8("Проект"), pageId); }).wasOk(), "Controller create failed")) return 1;
    const auto beforeInvalid = persisted.toJson();
    if (!check(controller.execute([&](WorkspaceCommands& c) { return c.movePage(pageId, pageId); }).failed(),
            "Controller accepted cycle")
        || !check(persisted.toJson() == beforeInvalid, "Rejected command changed live model")) return 1;
    WorkspaceStore reopened(file);
    WorkspaceModel loaded;
    if (!check(reopened.load({}, {}, loaded).wasOk(), "Restart failed")
        || !check(loaded.findPage(pageId) != nullptr
            && loaded.findPage(pageId)->title == juce::String::fromUTF8("Проект"), "Page ID/title lost on restart")) return 1;
    file.replaceWithText("{external-change");
    if (!check(controller.execute([&](WorkspaceCommands& c) { return c.renamePage(pageId, "Unsaved"); }).failed(),
            "Controller ignored save failure")
        || !check(persisted.toJson() == beforeInvalid, "Failed save committed UI/model mutation")
        || !check(file.loadFileAsString() == "{external-change", "Failed command overwrote external data")) return 1;

    WorkspaceModel restored;
    if (!check(WorkspaceModel::fromJson(model.toJson(), restored).wasOk(), "Round-trip failed")
        || !check(restored.findPage(child)->id == childId && restored.findPage(grandchild)->parentPageId == child,
            "Hierarchy/IDs lost on restart")
        || !check(restored.catalog.songs[0].tasks[0].checkpoints[0].name == "Verse", "Page operations changed DAW checkpoints")) return 1;
    // #63 / 0.2.4: two distinct page kinds, shared stable tree and safe
    // preservation of old page blocks without an implicit migration.
    WorkspaceModel notesModel;
    WorkspaceCommands notesCommand(notesModel);
    juce::String projectPage, notebook, nestedNote, nestedProject, createdTask;
    if (!check(notesCommand.createPage({}, "Production", projectPage).wasOk(), "Project page create")
        || !check(notesCommand.createNotePage({}, "Ideas", notebook).wasOk(), "Note page create")
        || !check(notesCommand.createNotePage(projectPage, "Side notes", nestedNote).wasOk(), "Nested note create")
        || !check(notesCommand.createPage(notebook, "Tasks under notes", nestedProject).wasOk(),
            "Project under note page create")
        || !check(notesModel.findPage(notebook)->kind == "note"
            && notesModel.findPage(projectPage)->kind == "page"
            && WorkspaceQueries::activePages(notesModel).size() == 4, "Navigation lost page kinds")
        || !check(notesCommand.createTask(notebook, "Invalid owner", createdTask).failed(),
            "Note pages must not own general tasks")
        || !check(notesCommand.createTask(projectPage, "Arrangement", createdTask).wasOk(),
            "Normal task page cannot create tasks")) return 1;
    const std::vector<WorkspaceBlock> oldBlocks {{"old-page-block", "text", "Historical content", {}, false}};
    const std::vector<WorkspaceBlock> noteBlocks {{"note-block", "heading", "Idea", {}, false}};
    if (!check(notesCommand.replacePageBlocks(projectPage, {}, oldBlocks).wasOk(),
            "Cannot retain historical materials in ordinary page")
        || !check(notesCommand.replacePageBlocks(notebook, {}, noteBlocks).wasOk(),
            "Cannot edit note page blocks")
        || !check(notesCommand.moveTask(createdTask, projectPage, {}, notebook, {}).failed(),
            "Moved task into note-only page")
        || !check(notesCommand.archivePage(notebook).wasOk(), "Archive note")
        || !check(WorkspaceQueries::isArchived(notesModel, nestedProject), "Note archive missed descendants")
        || !check(WorkspaceQueries::activePages(notesModel).size() == 2, "Archived notes remained in navigation")
        || !check(notesCommand.restorePage(notebook).wasOk(), "Restore note")
        || !check(WorkspaceQueries::activePages(notesModel).size() == 4, "Restored notes missing")
        || !check(notesModel.validate().wasOk(), "Note kind invalid in workspace schema")) return 1;
    WorkspaceModel reloadedNotes;
    if (!check(WorkspaceModel::fromJson(notesModel.toJson(), reloadedNotes).wasOk(),
            "Schema-2 note page cannot round-trip")
        || !check(reloadedNotes.findPage(projectPage)->blocks[0].id == "old-page-block"
            && reloadedNotes.findPage(notebook)->blocks[0].id == "note-block"
            && reloadedNotes.findPage(nestedNote)->parentPageId == projectPage,
            "Round-trip lost note or historical page block IDs")) return 1;
    auto badKind = notesModel;
    badKind.findPage(notebook)->kind = "unsupported";
    if (!check(badKind.validate().failed(), "Unsupported page kind was accepted")) return 1;
    return 0;
}
