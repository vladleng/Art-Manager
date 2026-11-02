#include "WorkspaceController.h"
#include <iostream>
#define REQUIRE(v) do { if (!(v)) { std::cerr << "Failed line " << __LINE__ << ": " << #v << '\n'; return 1; } } while (false)

int main()
{
    using namespace arranger;
    for (bool project : {false, true})
        for (const auto& definition : builtinTemplates(project)) REQUIRE(definition.validate().wasOk());
    WorkspaceModel model; WorkspaceCommands commands(model);
    juce::String owner, original, child, archived, blockId = juce::Uuid().toString(), checkpoint;
    REQUIRE(commands.createPage({}, "Existing", owner).wasOk());
    REQUIRE(commands.createTask(owner, "Original", original).wasOk());
    REQUIRE(commands.createSubtask(original, "Child", child).wasOk());
    REQUIRE(commands.createSubtask(original, "Archived", archived).wasOk());
    REQUIRE(commands.archiveTask(archived).wasOk());
    REQUIRE(commands.createCheckpoint(original, "Checkpoint", checkpoint).wasOk());
    model.findTask(original)->properties = {"Original", "Retain notes", Status::wip, 1};
    model.findTask(original)->blocks.push_back({blockId, "text", "Description", {}, false, "Block name"});
    model.findTask(original)->checkpoints.front().notes = "Checkpoint notes";
    const auto unchanged = model.toJson();
    auto taskTemplate = captureTemplate(model, owner, original);
    REQUIRE(taskTemplate.validate().wasOk());
    REQUIRE(taskTemplate.content.tasks.size() == 2);
    REQUIRE(taskTemplate.content.tasks[0].id != original);
    REQUIRE(taskTemplate.content.tasks[0].blocks[0].id != blockId);
    REQUIRE(taskTemplate.content.tasks[0].checkpoints[0].id != checkpoint);
    REQUIRE(model.toJson() == unchanged);
    juce::String created, again;
    REQUIRE(commands.createFromTemplate(taskTemplate, owner, "New take", created).wasOk());
    REQUIRE(commands.createFromTemplate(taskTemplate, owner, "Another take", again).wasOk());
    REQUIRE(created != again && created != original);
    REQUIRE(model.findTask(original)->properties.name == "Original");
    REQUIRE(model.findTask(created)->properties.name == "New take");
    REQUIRE(model.findTask(created)->properties.notes == "Retain notes");
    REQUIRE(model.findTask(created)->properties.status == Status::wip);
    REQUIRE(model.findTask(created)->properties.priority == 1);
    REQUIRE(model.findTask(created)->blocks[0].text == "Description");
    REQUIRE(model.findTask(created)->checkpoints[0].notes == "Checkpoint notes");
    REQUIRE(model.findTask(created)->checkpoints[0].id != model.findTask(again)->checkpoints[0].id);
    int children = 0;
    for (const auto& task : model.tasks) if (task.parentTaskId == created) { ++children; REQUIRE(task.ownerPageId == owner); }
    REQUIRE(children == 1);
    REQUIRE(model.validate().wasOk());
    auto projectTemplate = captureTemplate(model, owner);
    REQUIRE(projectTemplate.validate().wasOk());
    juce::String project;
    REQUIRE(commands.createFromTemplate(projectTemplate, owner, "Nested project", project).wasOk());
    REQUIRE(model.findPage(project)->parentPageId == owner);
    REQUIRE(model.findTask(original)->ownerPageId == owner);
    WorkspaceModel decoded;
    REQUIRE(WorkspaceModel::fromJson(model.toJson(), decoded).wasOk());
    REQUIRE(decoded.toJson() == model.toJson());
    const auto beforeBad = model.toJson();
    juce::String sentinel = "unchanged";
    REQUIRE(commands.createFromTemplate(taskTemplate, "missing", "Name", sentinel).failed());
    REQUIRE(commands.createFromTemplate(projectTemplate, {}, "  ", sentinel).failed());
    auto broken = taskTemplate; broken.content.tasks[1].parentTaskId = "missing";
    REQUIRE(commands.createFromTemplate(broken, owner, "Broken", sentinel).failed());
    REQUIRE(sentinel == "unchanged" && beforeBad == model.toJson());
    // Archived destinations and note pages cannot acquire tasks.
    juce::String note;
    REQUIRE(commands.createNotePage({}, "Notes", note).wasOk());
    REQUIRE(commands.createFromTemplate(taskTemplate, note, "No", sentinel).failed());
    REQUIRE(commands.archivePage(project).wasOk());
    REQUIRE(commands.createFromTemplate(taskTemplate, project, "No", sentinel).failed());
    REQUIRE(commands.createFromTemplate(projectTemplate, project, "No", sentinel).failed());

    const auto directory = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("ArtManagerTemplateTests-" + juce::Uuid().toString());
    struct Cleanup { juce::File directory; ~Cleanup() { directory.deleteRecursively(); } } cleanup {directory};
    WorkspaceTemplateStore catalog(directory.getChildFile("templates.json"));
    REQUIRE(catalog.load().wasOk()); REQUIRE(catalog.put(taskTemplate).wasOk());
    const auto savedCatalog = directory.getChildFile("templates.json").loadFileAsString();
    WorkspaceTemplateStore restarted(directory.getChildFile("templates.json"));
    REQUIRE(restarted.load().wasOk()); REQUIRE(restarted.definitions.size() == 1);
    REQUIRE(restarted.definitions[0].content.toJson() == taskTemplate.content.toJson());
    auto edited = taskTemplate; edited.content.pages.front().title = "Edited";
    REQUIRE(restarted.put(edited).wasOk()); REQUIRE(restarted.definitions.size() == 1);
    REQUIRE(directory.getChildFile("templates.json.backup").loadFileAsString() == savedCatalog);
    REQUIRE(catalog.put(edited).failed()); // Concurrent catalog revision retained.
    REQUIRE(catalog.definitions[0].title() == taskTemplate.title());
    REQUIRE(restarted.remove(edited.id()).wasOk()); REQUIRE(restarted.definitions.empty());
    std::vector<WorkspaceTemplate> output {taskTemplate};
    REQUIRE(WorkspaceTemplateStore::decode("{}", output).failed()); REQUIRE(output.size() == 1);
    REQUIRE(WorkspaceTemplateStore::decode(WorkspaceTemplateStore::encode({taskTemplate, taskTemplate}), output).failed());
    auto future = savedCatalog.replace("\"version\": 1", "\"version\": 99");
    REQUIRE(WorkspaceTemplateStore::decode(future, output).failed());
    REQUIRE(directory.getChildFile("templates.json").replaceWithText("corrupt"));
    WorkspaceTemplateStore damaged(directory.getChildFile("templates.json"));
    REQUIRE(damaged.load().failed()); REQUIRE(damaged.put(taskTemplate).failed());
    REQUIRE(directory.getChildFile("templates.json").loadFileAsString() == "corrupt");
    WorkspaceTemplateStore unwritable(directory.getChildFile("folder"));
    REQUIRE(unwritable.load().wasOk()); REQUIRE(directory.getChildFile("folder").createDirectory().wasOk());
    REQUIRE(unwritable.put(taskTemplate).failed()); REQUIRE(unwritable.definitions.empty());
    // Real controller transaction: restart, persistent Undo and write conflict.
    WorkspaceStore storage(directory.getChildFile("workspace.json")); WorkspaceModel live;
    REQUIRE(storage.load({}, {}, live).wasOk()); WorkspaceController controller(live, storage);
    auto definition = builtinTemplates(true)[1];
    const auto initial = live.toJson();
    REQUIRE(controller.execute([&](WorkspaceCommands& cmd) { return cmd.createFromTemplate(definition, {}, "Session", created); }).wasOk());
    REQUIRE(live.pages.size() == 1 && !live.tasks.empty());
    const auto createdPage = live.pages.front().id;
    REQUIRE(controller.undoLast().wasOk()); REQUIRE(live.pages.empty() && live.tasks.empty());
    REQUIRE(controller.undoLast().wasOk()); REQUIRE(live.pages.front().id == createdPage);
    WorkspaceStore reloadedStorage(storage.getFile()); WorkspaceModel reloaded;
    REQUIRE(reloadedStorage.load({}, {}, reloaded).wasOk()); REQUIRE(reloaded.toJson() == live.toJson());
    const auto lastGood = live.toJson();
    REQUIRE(storage.getFile().replaceWithText(lastGood + " "));
    REQUIRE(controller.execute([&](WorkspaceCommands& cmd) { return cmd.createFromTemplate(definition, {}, "Conflict", created); }).failed());
    REQUIRE(lastGood == live.toJson());
    std::cout << "Workspace template tests passed\n";
    return 0;
}
