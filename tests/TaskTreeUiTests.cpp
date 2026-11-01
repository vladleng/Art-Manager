#include "TaskTree.h"
#include <iostream>
static bool check(bool value, const char* message) { if (!value) std::cerr << message << '\n'; return value; }
#define REQUIRE(value, message) do { if (!check((value), message)) return 1; } while (false)
static juce::MouseEvent click(juce::Component& component, float x)
{
    const auto now = juce::Time::getCurrentTime();
    return {juce::Desktop::getInstance().getMainMouseSource(), {x, 22}, juce::ModifierKeys::leftButtonModifier,
        1, 0, 0, 0, 0, &component, &component, now, {x, 22}, now, 1, false};
}
int main()
{
    juce::ScopedJuceInitialiser_GUI gui;
    arranger::WorkspaceModel model;
    arranger::WorkspaceCommands commands(model); juce::String page, first, second, third, cp;
    REQUIRE(commands.createPage({}, juce::String::fromUTF8("Разное"), page).wasOk(), "Page");
    REQUIRE(commands.createTask(page, juce::String::fromUTF8("Идеи для MRS"), first).wasOk(), "Task");
    REQUIRE(commands.createTask(page, juce::String::fromUTF8("Публикация"), second).wasOk(), "Second task");
    REQUIRE(commands.createTask(page, juce::String::fromUTF8("Репетиция"), third).wasOk(), "Third task");
    auto expected = model.findTask(first)->properties, properties = expected;
    properties.notes = juce::String::fromUTF8("Подготовить новую композицию и записать демо.");
    properties.priority = 1; properties.status = arranger::Status::wip;
    REQUIRE(commands.updateTask(first, expected, properties).wasOk(), "Properties");
    for (const auto* name : {"Гармония", "Рыба", "Репетиция", "Доработка"})
    {
        juce::String id; REQUIRE(commands.createCheckpoint(first, juce::String::fromUTF8(name), id).wasOk(), "Checkpoint");
        if (cp.isEmpty()) cp = id;
    }
    const auto checkpoint = model.findTask(first)->checkpoints[0]; auto value = checkpoint;
    value.status = arranger::Status::done; value.notes = juce::String::fromUTF8("Проверить гармонию припева.\nСохранить финальный вариант.");
    REQUIRE(commands.updateCheckpoint(first, checkpoint, value).wasOk(), "Checkpoint notes");
    bool allowFlush = true; int opened = 0;
    TaskTree tree(model, [&](const arranger::WorkspaceAction& action) { return action(commands); },
        [&] { return allowFlush; }, [&](arranger::TaskReference ref) { if (ref.taskId == first) ++opened; }, [] {}, [](juce::String) {});
    tree.setSize(1180, 600); const auto refs = arranger::WorkspaceQueries::allTasks(model); tree.setTasks(refs);
    auto* list = dynamic_cast<juce::ListBox*>(tree.getChildComponent(0)); REQUIRE(list != nullptr, "List");
    auto* view = list->getListBoxModel(); REQUIRE(view->getNumRows() == 3, "Collapsed count");
    view->listBoxItemClicked(0, click(*list, 15)); REQUIRE(view->getNumRows() == 8, "Task expansion");
    view->listBoxItemClicked(2, click(*list, 30)); REQUIRE(view->getNumRows() == 9, "Checkpoint expansion");
    list->selectRow(2); tree.setTasks(refs);
    REQUIRE(view->getNumRows() == 9 && list->getSelectedRow() == 2, "Refresh lost expansion/selection");
    view->listBoxItemDoubleClicked(0, click(*list, 180)); REQUIRE(opened == 1, "Open source identity");
    const auto image = tree.createComponentSnapshot(tree.getLocalBounds(), true, 1.0f);
    REQUIRE(image.isValid() && image.getWidth() == 1180, "Snapshot");
    juce::MemoryOutputStream png; REQUIRE(juce::PNGImageFormat().writeImageToStream(image, png), "PNG");
    if (juce::SystemStats::getEnvironmentVariable("ARRANGER_UI_PREVIEW", {}).isNotEmpty())
        std::cout << "UI_PREVIEW_BASE64=" << juce::Base64::toBase64(png.getData(), png.getDataSize()) << '\n';
    auto file = juce::File::getCurrentWorkingDirectory().getChildFile("task-tree-preview.png");
    REQUIRE(file.replaceWithData(png.getData(), png.getDataSize()), "Save preview");
    REQUIRE(commands.archiveCheckpoint(first, cp).wasOk(), "Archive"); tree.setTasks(refs);
    REQUIRE(view->getNumRows() == 7, "Archived checkpoint still visible");
    REQUIRE(commands.restoreCheckpoint(first, cp).wasOk(), "Restore"); tree.setTasks(refs);
    REQUIRE(view->getNumRows() == 9, "Restored checkpoint lost expansion");
    allowFlush = false; tree.showTaskMenu(refs[0], {0, 0});
    REQUIRE(!juce::PopupMenu::dismissAllActiveMenus(), "Menu bypassed failed pending save");
    allowFlush = true; tree.setTasks({refs[0]}, true);
    REQUIRE(view->getNumRows() == 5, "Detail checkpoints expansion");
    tree.setTasks({}); REQUIRE(!list->isVisible(), "Empty list blocks creation menu");
    std::cout << "Task table expansion, source IDs, selection, archives and pending-save guard passed\n";
}
