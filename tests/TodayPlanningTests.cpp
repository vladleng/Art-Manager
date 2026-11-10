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
    CHECK(addCivilDays("2024-02-28", 1) == "2024-02-29");
    CHECK(addCivilDays("2026-12-31", 1) == "2027-01-01");
    CHECK(addCivilDays("2026-01-01", -1) == "2025-12-31");
    CHECK(addCivilDays("2026-02-29", 1).isEmpty());
    TaskPlan moved;
    const TaskPlan timedRange {"2026-12-31T10:30", "2027-01-02T12:15",
        "2026-12-30T09:00", "2026-12-31T17:45"};
    CHECK(taskPlanForDay(timedRange, "2027-01-04", moved).wasOk());
    CHECK(moved.start == "2027-01-04T10:30" && moved.end == "2027-01-06T12:15"
        && moved.deadline == timedRange.deadline && moved.completedAt == timedRange.completedAt);
    CHECK(taskPlanForDay({}, "2026-11-07", moved).wasOk()
        && moved.start == "2026-11-07" && moved.end.isEmpty());
    CHECK(taskPlanForDay(timedRange, "2026-11-31", moved).failed());

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
    // Both deadlines (Nov 5 and Nov 6) are already overdue on Nov 7.
    // Keep the query's stable task order and verify each identity explicitly.
    CHECK(rows.planned.size() == 2 && rows.overdue.size() == 2
        && rows.planned[0].taskId == arrange && rows.planned[1].taskId == publish
        && rows.overdue[0].taskId == arrange && rows.overdue[1].taskId == publish);
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

    // #38 stage 2: move a real task, preserve source and metadata, undo it,
    // then unschedule without deleting deadline or task. No daily task copies.
    const auto initialCount = model.tasks.size();
    const auto originalOwner = model.findTask(arrange)->ownerPageId;
    const auto originalNotes = model.findTask(arrange)->properties.notes;
    CHECK(controller.execute([&](WorkspaceCommands& cmd)
        { return cmd.scheduleTaskForDay(arrange, newPlan, "2026-11-10"); }).wasOk());
    CHECK(model.findTask(arrange)->plan.start == "2026-11-10"
        && model.findTask(arrange)->plan.end == "2026-11-12"
        && model.findTask(arrange)->plan.deadline == "2026-11-05");
    CHECK(model.tasks.size() == initialCount && model.findTask(arrange)->ownerPageId == originalOwner
        && model.findTask(arrange)->properties.notes == originalNotes);
    CHECK(controller.execute([&](WorkspaceCommands& cmd)
        { return cmd.removeTaskFromPlan(arrange, newPlan); }).failed()); // stale CAS
    CHECK(controller.undoLast().wasOk() && model.findTask(arrange)->plan == newPlan);
    CHECK(controller.execute([&](WorkspaceCommands& cmd)
        { return cmd.removeTaskFromPlan(arrange, newPlan); }).wasOk());
    CHECK(model.findTask(arrange)->plan.start.isEmpty() && model.findTask(arrange)->plan.end.isEmpty()
        && model.findTask(arrange)->plan.deadline == newPlan.deadline);
    CHECK(tasksForDay(model, "2026-11-07").planned.size() == 1);
    CHECK(controller.undoLast().wasOk() && model.findTask(arrange)->plan == newPlan);
    CHECK(controller.execute([&](WorkspaceCommands& cmd)
        { return cmd.scheduleTaskForDay(arrange, newPlan, "invalid"); }).failed());
    CHECK(model.findTask(arrange)->plan == newPlan);

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
