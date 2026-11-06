#include "TodayPlanning.h"
#include "WorkspaceController.h"
#include <iostream>

#define CHECK(condition) do { if (!(condition)) { std::cerr << "Failed line " << __LINE__ << ": " << #condition << "\n"; return 1; } } while (false)

struct TestLocation
{
    juce::File path = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("ManagerTodayTest-" + juce::Uuid().toString());
    TestLocation() { path.createDirectory(); }
    ~TestLocation() { path.deleteRecursively(); }
};

int main()
{
    using namespace arranger;
    CHECK(isCivilDate("2024-02-29"));
    CHECK(!isCivilDate("2026-02-29"));
    CHECK(!isCivilDate("2026-13-01"));
    CHECK(!isCivilDate("2026-11-31"));
    CHECK(isCivilDateTime("2026-11-06T19:45"));
    CHECK(!isCivilDateTime("2026-11-06T25:10"));
    CHECK(!isCivilDateTime("2026-11-06T12:65"));
    CHECK(validateTaskPlan({"2026-11-06", "2026-11-08", "2026-11-04", {}}).wasOk());
    CHECK(validateTaskPlan({"2026-11-06", "2026-11-05", {}, {}}).failed());
    CHECK(validateTaskPlan({{}, "2026-11-06", {}, {}}).failed());
    CHECK(planIncludesDay({"2026-11-06", "2026-11-08", {}, {}}, "2026-11-07"));
    CHECK(!planIncludesDay({"2026-11-06", "2026-11-08", {}, {}}, "2026-11-09"));
    CHECK(planDeadlineOverdue({{}, {}, "2026-11-05", {}}, "2026-11-06"));

    TestLocation location;
    const auto file = location.path.getChildFile("workspace.json");
    WorkspaceModel model;
    WorkspaceStore store(file);
    CHECK(store.load({}, {}, model).wasOk());
    WorkspaceController controller(model, store);
    juce::String music, media, arrange, publish, child;
    CHECK(controller.execute([&](WorkspaceCommands& commands)
        { return commands.createPage({}, "Music", music); }).wasOk());
    CHECK(controller.execute([&](WorkspaceCommands& commands)
        { return commands.createPage({}, "Media", media); }).wasOk());
    CHECK(controller.execute([&](WorkspaceCommands& commands)
        { return commands.createTask(music, "Arrange single", arrange); }).wasOk());
    CHECK(controller.execute([&](WorkspaceCommands& commands)
        { return commands.createTask(media, "Publish video", publish); }).wasOk());
    CHECK(controller.execute([&](WorkspaceCommands& commands)
        { return commands.createSubtask(arrange, "Harmony", child); }).wasOk());

    auto plan = model.findTask(arrange)->plan;
    const TaskPlan newPlan {"2026-11-06", "2026-11-08", "2026-11-05", {}};
    CHECK(controller.execute([&](WorkspaceCommands& cmd)
        { return cmd.updateTaskPlan(arrange, plan, newPlan); }).wasOk());
    CHECK(controller.execute([&](WorkspaceCommands& cmd)
        { return cmd.updateTaskPlan(arrange, plan, {"2026-11-11", {}, {}, {}}); }).failed());
    CHECK(model.findTask(arrange)->plan == newPlan);
    const TaskPlan otherPlan {"2026-11-07T10:30", {}, "2026-11-06", {}};
    CHECK(controller.execute([&](WorkspaceCommands& cmd)
        { return cmd.updateTaskPlan(publish, {}, otherPlan); }).wasOk());
    CHECK(model.tasks.size() == 3);
    auto rows = tasksForDay(model, "2026-11-07");
    CHECK(rows.planned.size() == 2 && rows.overdue.size() == 1
        && rows.planned[0].taskId == arrange && rows.planned[1].taskId == publish
        && rows.overdue[0].taskId == arrange);
    CHECK(tasksForDay(model, "2026-11-09").planned.empty());
    CHECK(model.findTask(child)->plan.isEmpty());

    auto original = model.findTask(publish)->properties;
    auto finished = original;
    finished.status = Status::done;
    CHECK(controller.execute([&](WorkspaceCommands& cmd)
        { return cmd.updateTask(publish, original, finished); }).wasOk());
    rows = tasksForDay(model, "2026-11-07");
    CHECK(rows.planned.size() == 1 && rows.completed.size() == 1
        && rows.completed[0].taskId == publish);
    CHECK(controller.undoLast().wasOk());
    CHECK(tasksForDay(model, "2026-11-07").planned.size() == 2);

    const auto saved = file.loadFileAsString();
    WorkspaceModel reload;
    WorkspaceStore reopened(file);
    CHECK(reopened.load({}, {}, reload).wasOk());
    CHECK(reload.findTask(arrange)->plan == newPlan
        && reload.findTask(publish)->plan == otherPlan
        && reload.findTask(child)->plan.isEmpty());
    CHECK(reload.findTask(arrange)->id == arrange && reload.findTask(publish)->ownerPageId == media);
    auto legacy = juce::JSON::parse(saved);
    auto tasks = legacy.getProperty("tasks", {});
    for (auto& item : *tasks.getArray()) item.getDynamicObject()->removeProperty("plan");
    WorkspaceModel oldSchema2;
    CHECK(WorkspaceModel::fromJson(juce::JSON::toString(legacy), oldSchema2).wasOk());
    CHECK(oldSchema2.findTask(arrange)->plan.isEmpty());
    auto invalid = juce::JSON::parse(saved);
    auto* first = (*invalid.getProperty("tasks", {}).getArray())[0].getDynamicObject();
    auto rawPlan = first->getProperty("plan");
    rawPlan.getDynamicObject()->setProperty("start", "2026-02-29");
    WorkspaceModel unchanged = reload;
    CHECK(WorkspaceModel::fromJson(juce::JSON::toString(invalid), unchanged).failed());
    CHECK(unchanged.findTask(arrange)->plan == newPlan);
    CHECK(file.loadFileAsString() == saved);
    std::cout << "Today planning: dates, CAS, selection, status, undo, restart and schema-2 compatibility PASS\n";
    return 0;
}
