#include "WorkspaceTaskViews.h"
#include "WorkspaceCommands.h"
#include <iostream>

static bool require(bool ok, const char* description)
{
    if (!ok) std::cerr << description << '\n';
    return ok;
}
#define CHECK(cond, explanation) do { if (!require((cond), (explanation))) return 1; } while(false)

int main()
{
    using namespace arranger;
    WorkspaceModel model;
    WorkspaceCommands commands(model);
    juce::String music, media, notes, a, b, child;
    CHECK(commands.createPage({}, "Moon River", music).wasOk(), "Music page");
    CHECK(commands.createPage({}, "Social Media", media).wasOk(), "Media page");
    CHECK(commands.createNotePage(music, "Chord ideas", notes).wasOk(), "Notes page");
    CHECK(commands.replacePageBlocks(notes, {}, {{"note-a", "text", "The subdominant in Corcovado", {}, false}}).wasOk(),
        "Note content");
    CHECK(commands.createTask(music, "Master vocals", a).wasOk(), "Task A");
    CHECK(commands.createTask(media, "Publish video", b).wasOk(), "Task B");
    CHECK(commands.createSubtask(a, "Jazz harmony", child).wasOk(), "Child");
    model.findTask(a)->properties.notes = "Edit Moon River take";
    model.findTask(a)->properties.priority = 4;
    model.findTask(b)->properties.priority = 1;
    model.findTask(b)->properties.status = Status::done;
    model.findTask(child)->properties.status = Status::wip;
    model.findTask(child)->properties.notes = "Analyze Fm6";
    CHECK(model.validate().wasOk(), "Fixture not valid");

    const auto original = WorkspaceQueries::allTasks(model);
    CHECK(original.size() == 2 && original[0].taskId == a && original[1].taskId == b, "Fixture order");
    TaskViewOptions view;
    view.search = "fm6";
    auto results = projectTaskView(model, original, view);
    CHECK(results.size() == 1 && results[0].taskId == a, "Nested note search must reveal its root");
    view.search = "jAZZ harmony";
    CHECK(projectTaskView(model, original, view).size() == 1, "Case-insensitive descendant name");
    view.search = "moon river";
    CHECK(projectTaskView(model, original, view).size() == 1, "Page title / breadcrumb search");
    view.search = "publish";
    CHECK(projectTaskView(model, original, view)[0].taskId == b, "Task name search");
    CHECK(pageMatchesSearch(model, *model.findPage(notes), "corcovado"), "Note block search");
    CHECK(!pageMatchesSearch(model, *model.findPage(notes), "Unrelated"), "Note false positive");

    view = {};
    view.status = "WIP";
    results = projectTaskView(model, original, view);
    CHECK(results.size() == 1 && results[0].taskId == a, "WIP nested child search");
    view.priority = 1;
    results = projectTaskView(model, original, view);
    CHECK(results.empty(), "Status and priority must combine, not OR");
    view.status.clear();
    results = projectTaskView(model, original, view);
    CHECK(results.size() == 1 && results[0].taskId == b, "P1 filter");
    view = {};
    view.sort = TaskViewSort::priority;
    results = projectTaskView(model, original, view);
    CHECK(results.size() == 2 && results[0].taskId == b && results[1].taskId == a,
        "Priority sort should not change stored order");
    CHECK(WorkspaceQueries::allTasks(model)[0].taskId == a, "Source order mutated by sorting");
    view.sort = TaskViewSort::nameAsc;
    results = projectTaskView(model, original, view);
    CHECK(results[0].taskId == a, "Name A-Z");
    view.sort = TaskViewSort::nameDesc;
    results = projectTaskView(model, original, view);
    CHECK(results[0].taskId == b, "Name Z-A");
    view.sort = TaskViewSort::manual;
    CHECK(projectTaskView(model, original, view)[0].taskId == a, "Manual order restore");

    view.search = "harmony";
    view.priority = 4;
    view.status = "WIP";
    view.sort = TaskViewSort::nameDesc;
    auto roundTripped = decodeTaskView(encodeTaskView(view));
    CHECK(roundTripped == view, "Saved UI-only settings did not round trip");
    CHECK(decodeTaskView("{bad json").sort == TaskViewSort::manual,
        "Corrupt preferences must fall back safely");
    CHECK(model.validate().wasOk() && model.findTask(child)->id == child,
        "Search/sort corrupted WorkspaceModel or IDs");
    std::cout << "Task/notes/page search, combined filters, sorting and preferences passed\n";
    return 0;
}
