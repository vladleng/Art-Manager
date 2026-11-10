#include "TaskTree.h"
#include "BlockEditor.h"
#include "WorkspaceTemplateEditor.h"
#include "WorkspaceTemplateCreation.h"
#include "TaskQuickProperties.h"
#include "TaskPlanEditor.h"
#include "DayCalendarPanel.h"
#include "CalendarCardLabels.h"
#include "TaskProgressMeter.h"
#include <iostream>
static bool check(bool value, const char* message) { if (!value) std::cerr << message << '\n'; return value; }
#define REQUIRE(value, message) do { if (!check((value), message)) return 1; } while (false)
static juce::MouseEvent click(juce::Component& component, float x)
{
    const auto now = juce::Time::getCurrentTime();
    return {juce::Desktop::getInstance().getMainMouseSource(), {x, 16}, juce::ModifierKeys::leftButtonModifier,
        1, 0, 0, 0, 0, &component, &component, now, {x, 16}, now, 1, false};
}
static juce::MouseEvent mouseAt(juce::Component& component, float x, float y)
{
    const auto now = juce::Time::getCurrentTime();
    return {juce::Desktop::getInstance().getMainMouseSource(), {x, y},
        juce::ModifierKeys::leftButtonModifier,
        1, 0, 0, 0, 0, &component, &component, now, {x, y}, now, 1, false};
}
static juce::TextEditor* firstText(juce::Component& component)
{
    if (auto* field = dynamic_cast<juce::TextEditor*>(&component)) return field;
    for (int i = 0; i < component.getNumChildComponents(); ++i)
        if (auto* field = firstText(*component.getChildComponent(i))) return field;
    return nullptr;
}
static bool snapshot(juce::Component& component, const char* filename)
{
    const auto image = component.createComponentSnapshot(component.getLocalBounds(), true, 1.0f);
    juce::MemoryOutputStream png;
    return image.isValid() && juce::PNGImageFormat().writeImageToStream(image,png)
        && juce::File::getCurrentWorkingDirectory().getChildFile(filename).replaceWithData(png.getData(),png.getDataSize());
}
int main()
{
    juce::ScopedJuceInitialiser_GUI gui;
    // #61: neutral Manager chrome has no blue tint, without flattening
    // meaningful status colours and priority badges.
    const auto graphite = [](std::uint32_t token)
    {
        const auto colour = mr::colour(token);
        const int r = colour.getRed(), g = colour.getGreen(), b = colour.getBlue();
        return std::abs(r - g) <= 6 && std::abs(g - b) <= 6 && std::abs(r - b) <= 6;
    };
    for (auto token : {mr::tokens::canvas, mr::tokens::panel, mr::tokens::item,
                       mr::tokens::selected, mr::tokens::outline, mr::tokens::bright,
                       mr::tokens::meta, mr::tokens::muted, mr::tokens::focus})
        REQUIRE(graphite(token), "Neutral Manager surface still carries a blue tint");
    REQUIRE(mr::colour(mr::tokens::wip) == juce::Colour(mr::tokens::wip)
         && mr::colour(mr::tokens::p1) == juce::Colour(mr::tokens::p1)
         && mr::colour(mr::tokens::progressFill) == juce::Colour(mr::tokens::progressFill),
        "Semantic status/priority/progress colours were modified");

    // Existing action SVGs use the old blue focus tone. Check that the
    // runtime cache neutralises these without rewriting the pinned SVGs.
    mr::Icons icons;
    juce::Image actionImage(juce::Image::ARGB, 28, 28, true);
    {
        juce::Graphics g(actionImage);
        icons.draw("manager/actions/organizer", g, {2.0f, 2.0f, 24.0f, 24.0f});
    }
    int visiblePixels = 0;
    for (int y = 0; y < actionImage.getHeight(); ++y)
        for (int x = 0; x < actionImage.getWidth(); ++x)
        {
            const auto pixel = actionImage.getPixelAt(x, y);
            if (pixel.getAlpha() < 64) continue;
            ++visiblePixels;
            REQUIRE(std::abs(static_cast<int>(pixel.getRed()) - static_cast<int>(pixel.getBlue())) <= 6,
                "Action icon retained a blue tint");
        }
    REQUIRE(visiblePixels > 0, "Graphite action icon did not render");
    arranger::WorkspaceModel model;
    model.catalog.addSong("C:/Fixture/legacy.song");
    const auto legacyId = model.catalog.addTask(model.catalog.songs[0].path, "Local legacy task");
    model.reconcileCatalogPages();
    arranger::WorkspaceCommands commands(model);
    juce::String page, first, second;
    REQUIRE(commands.createPage({}, juce::String::fromUTF8("Разное"), page).wasOk(), "Page");
    REQUIRE(commands.createTask(page, juce::String::fromUTF8("Идеи для MRS"), first).wasOk(), "Task");
    REQUIRE(commands.createTask(page, juce::String::fromUTF8("Репетиция"), second).wasOk(), "Second task");
    for (const auto* name : {"Гармония", "Рыба", "Репетиция", "Доработка"})
    {
        juce::String id;
        REQUIRE(commands.createCheckpoint(first, juce::String::fromUTF8(name), id).wasOk(), "Old checkpoint");
    }
    bool failWrites = false, allowFlush = true; int opened = 0;
    juce::String widthsSaved;
    mr::ManagerTheme theme;
    REQUIRE(theme.findColour(juce::PopupMenu::textColourId)
        == theme.findColour(juce::PopupMenu::highlightedTextColourId),
        "Hover changes menu text brightness");
    REQUIRE(theme.findColour(juce::PopupMenu::highlightedBackgroundColourId)
        == mr::colour(mr::tokens::selected), "Gray menu highlight was removed");
    // Reused TaskTree editing a definition is isolated from the real workspace.
    const auto liveBeforeTemplate = model.toJson();
    auto definition = arranger::builtinTemplates(true)[1];
    int templateSaves = 0;
    WorkspaceTemplateEditor templateEditor(definition, [&](const auto& saved)
    {
        ++templateSaves;
        return saved.validate();
    });
    templateEditor.setVisible(true);
    for (float scale : {1.0f, 1.25f, 1.5f, 2.0f})
    {
        const auto image = templateEditor.createComponentSnapshot(templateEditor.getLocalBounds(), true, scale);
        REQUIRE(image.isValid() && image.getWidth() >= 780, "Template editor DPI snapshot failed");
    }
    REQUIRE(snapshot(templateEditor, "template-editor.png"), "Template editor preview");
    REQUIRE(templateSaves == 0 && model.toJson() == liveBeforeTemplate,
        "Opening a definition changed live data or auto-saved it");
    juce::String creationName; int creates = 0;
    WorkspaceTemplateCreation creation("Track", {}, {}, 0, [&](juce::String name, juce::String)
    {
        ++creates; creationName = name;
        return juce::Result::fail("Write failure fixture");
    });
    auto* creationField = firstText(creation);
    REQUIRE(creationField != nullptr, "Template name field missing");
    creationField->setText("Unsaved project"); creationField->onReturnKey();
    REQUIRE(creates == 1 && creationName == "Unsaved project"
        && creationField->getText() == "Unsaved project", "Failed creation lost its draft");
    REQUIRE(snapshot(creation, "template-create.png"), "Template create preview");
    // Quick properties callout preserves the draft on errors; saving is explicit.
    int propertiesSaves = 0; bool propertiesWrite = false;
    juce::String editedName, editedNotes;
    int editedStatus = 0, editedPriority = 0;
    TaskQuickProperties properties(false, "Original", "Original Notes", 2, 3,
        [&](juce::String name, juce::String notes, int status, int priority)
        {
            ++propertiesSaves; editedName = name; editedNotes = notes;
            editedStatus = status; editedPriority = priority;
            return propertiesWrite ? juce::Result::ok() : juce::Result::fail("Simulated save conflict");
        });
    auto* quickName = firstText(properties);
    REQUIRE(quickName && quickName->getText() == "Original", "Quick task properties name");
    quickName->setText("New task name", false); quickName->onReturnKey();
    REQUIRE(propertiesSaves == 1 && quickName->getText() == "New task name",
        "A failed properties save discarded the draft");
    propertiesWrite = true; quickName->onReturnKey();
    REQUIRE(propertiesSaves == 2 && editedName == "New task name"
        && editedNotes == "Original Notes" && editedStatus == 2 && editedPriority == 3,
        "Quick properties did not forward name, notes, status and priority");
    REQUIRE(snapshot(properties, "task-properties.png"), "Quick properties preview");
    TaskQuickProperties checkpointProperties(true, "Subtask", "Subtask Notes", 3, 3,
        [](juce::String, juce::String, int, int) { return juce::Result::ok(); });
    REQUIRE(snapshot(checkpointProperties, "subtask-properties.png"), "Quick subtask properties preview");
    REQUIRE(PlanMonthGrid::daysInMonth(2024, 2) == 29
        && PlanMonthGrid::daysInMonth(2026, 2) == 28
        && PlanMonthGrid::mondayIndex(2026, 11, 1) == 6
        && PlanMonthGrid::mondayIndex(2026, 11, 2) == 0,
        "Gregorian calendar grid misaligned");
    TaskPlanEditor dateEditor({"2026-11-06", "2026-11-08", "2026-11-11", {}},
        "2026-11-06", [](arranger::TaskPlan) { return juce::Result::ok(); });
    REQUIRE(dateEditor.currentDraft()[0] == "2026-11-06"
        && dateEditor.currentDraft()[1] == "2026-11-08",
        "Calendar editor modified existing dates on open");
    dateEditor.selectField(2);
    REQUIRE(dateEditor.currentDraft()[2] == "2026-11-11",
        "Calendar editor lost deadline when switching tabs");
    REQUIRE(snapshot(dateEditor, "task-calendar.png"), "Task calendar preview");
    // MR Live-like near-black header: the title is drawn independently of
    // the generic V4 colour scheme, not with olive-tinted widget background.
    REQUIRE(mr::ManagerTheme::titleBarColour() == juce::Colour(0xff202123),
        "Title bar is no longer neutral near-black");
    juce::DocumentWindow windowFixture("Title test", mr::ManagerTheme::titleBarColour(),
        juce::DocumentWindow::allButtons);
    juce::Image titlePreview(juce::Image::ARGB, 280, 30, true);
    {
        juce::Graphics g(titlePreview);
        theme.drawDocumentWindowTitleBar(windowFixture, g, 280, 30, 5, 200, nullptr, true);
    }
    REQUIRE(titlePreview.getPixelAt(2, 2) == mr::ManagerTheme::titleBarColour(),
        "Window title bar painter did not apply black graphite");
    TaskTree tree(model,
        [&](const arranger::WorkspaceAction& action) {
            return failWrites ? juce::Result::fail("Disk failure fixture") : action(commands);
        },
        [&] { return allowFlush; },
        [&](arranger::TaskReference) { ++opened; },
        [] {}, [](juce::String) {}, {}, {},
        [&](juce::String widths) { widthsSaved = widths; });
    tree.setLookAndFeel(&theme);
    tree.setSize(1180, 520);
    const auto refs = arranger::WorkspaceQueries::allTasks(model);
    tree.setTasks(refs);
    auto* list = dynamic_cast<juce::ListBox*>(tree.getChildComponent(0));
    REQUIRE(list && list->getListBoxModel(), "ListBox");
    auto* view = list->getListBoxModel();
    REQUIRE(view->getNumRows() == 3 && list->getRowHeight() == 40, "Compact root task table");

    // #38/0.3.1-Upd2-fix2 regression: JUCE normally invokes
    // listBoxItemClicked on mouseDown for an unselected row, which opens the
    // task properties callout BEFORE it is possible to begin a drag.
    // The gesture must wait until mouseUp, when JUCE knows it wasn't a drag.
    REQUIRE(!list->getRowSelectedOnMouseDown(),
        "Task rows still dispatch click on mouseDown and block first-time drag");
    auto* rowCanvas = list->getViewport()->getViewedComponent();
    juce::Component* firstTaskRow = nullptr;
    for (int i = 0; rowCanvas && i < rowCanvas->getNumChildComponents(); ++i)
    {
        auto* candidate = rowCanvas->getChildComponent(i);
        if (list->getRowNumberOfComponent(candidate) == 1)
        {
            firstTaskRow = candidate;
            break;
        }
    }
    REQUIRE(firstTaskRow != nullptr, "Could not locate visible task row for click/drag repro");
    const auto press = click(*firstTaskRow, 10);
    firstTaskRow->mouseDown(press);
    REQUIRE(view->getNumRows() == 3,
        "Pressing/holding the first unselected task already dispatched the click");
    firstTaskRow->mouseUp(press);
    REQUIRE(view->getNumRows() == 8,
        "A completed ordinary click must still expand the task on mouseUp");

    // Notes now live in the right-hand column, not in an expandable detail row.
    REQUIRE(view->getNumRows() == 8, "Opening task must show four checkpoints and one add row, but no Notes row");
    // A checkpoint now has a neutral leaf glyph, never a second DONE checkbox.
    // Clicking its former square must not silently change TODO/DONE.
    const auto firstCheckpointId = model.findTask(first)->checkpoints[0].id;
    view->listBoxItemClicked(2, click(*list, 151));
    REQUIRE(model.findTask(first)->checkpoints[0].id == firstCheckpointId
        && model.findTask(first)->checkpoints[0].status == arranger::Status::todo,
        "Legacy checkpoint icon still toggles completion instead of using the status menu");

    auto* edit = dynamic_cast<juce::TextEditor*>(tree.getChildComponent(1));
    REQUIRE(edit, "Inline editor");
    view->listBoxItemDoubleClicked(1, click(*list, 930));
    REQUIRE(edit->isVisible() && edit->isMultiLine() && opened == 0, "Notes did not open in the column");
    edit->setText(juce::String::fromUTF8("Проверить демо перед записью."), false);
    REQUIRE(tree.flush().wasOk() && model.findTask(first)->properties.notes == edit->getText(),
        "Notes did not save from column");
    view->returnKeyPressed(1); // Keyboard Enter retains fast inline rename.
    edit->setText("Cancelled name", false); edit->onEscapeKey();
    REQUIRE(!edit->isVisible() && model.findTask(first)->properties.name != "Cancelled name",
        "Escape changed model");

    // The inline "+ subtask" action creates a normal nested task, not another checkpoint.
    view->listBoxItemClicked(6, click(*list, 200));
    REQUIRE(edit->isVisible(), "Inline add was not activated");
    edit->setText(juce::String::fromUTF8("Записать финальный дубль"), false); edit->onReturnKey();
    REQUIRE(model.findTask(first)->checkpoints.size() == 4 && model.tasks.size() == 3,
        "New nested task should not overwrite legacy checkpoints");
    juce::String child;
    for (const auto& task : model.tasks) if (task.parentTaskId == first) child = task.id;
    REQUIRE(child.isNotEmpty(), "New task has no parent task ID");

    view->listBoxItemDoubleClicked(2, click(*list, 930));
    failWrites = true; edit->setText("Unsaved child notes", false);
    REQUIRE(tree.flush().failed() && edit->isVisible() && edit->getText() == "Unsaved child notes",
        "Failed write lost draft");
    REQUIRE(model.findTask(child)->properties.notes != "Unsaved child notes", "Failed write mutated data");
    failWrites = false; edit->onEscapeKey();

    view->listBoxItemDoubleClicked(2, click(*list, 930));
    edit->setText("Child note after blur", false); edit->onFocusLost();
    REQUIRE(model.findTask(child)->properties.notes == "Child note after blur", "Blur did not save child notes");

    // Reproduction: the parent initially has four old checkpoints plus one
    // nested task. Completing that child must change its derived 0% to 20%.
    // Exercise the same explicit progress painter called by TaskTree.cpp,
    // without depending on ListBox row indices or a hidden window peer.
    const auto progressPercent = [&]() {
        const auto [completed, total] =
            arranger::WorkspaceQueries::taskProgress(model, *model.findTask(first));
        return total > 0 ? completed * 100 / total : 0;
    };
    const auto greenPixels = [&](int percent) {
        juce::Image image(juce::Image::ARGB, 92, 22, true);
        {
            // Complete the offscreen graphics context before reading pixels.
            // Other snapshot/image tests in this suite use the same scope.
            juce::Graphics graphics(image);
            mr::paintTaskProgressMeter(graphics, 8.0f, 9.0f, percent);
        }
        int filled = 0;
        for (int y = 10; y <= 11; ++y)
            for (int x = 11; x < 21; ++x)
            {
                const auto colour = image.getPixelAt(x, y);
                if (colour.getGreen() > colour.getRed() + 30
                    && colour.getAlpha() >= 128) ++filled;
            }
        return filled;
    };
    REQUIRE(progressPercent() == 0 && greenPixels(progressPercent()) == 0,
        "The parent progress meter should start empty");
    REQUIRE(commands.setReferenceStatus({{}, page, child}, {}, arranger::Status::done).wasOk(),
        "Failed to complete the nested task");
    tree.setTasks(refs);
    const auto completedProgress = arranger::WorkspaceQueries::taskProgress(model, *model.findTask(first));
    REQUIRE(completedProgress == std::make_pair(1, 5) && progressPercent() == 20,
        "Nested DONE did not advance the derived parent progress to 20%");
    const auto filledAfter = greenPixels(progressPercent());
    if (filledAfter <= 0)
        std::cerr << "Progress fill absent at 20%; token="
                  << mr::colour(mr::tokens::progressFill).toDisplayString(true)
                  << " green-pixels=" << filledAfter << "\n";
    REQUIRE(filledAfter > 0, "Progress meter did not paint its 20% fill");

    // Legacy song Notes preserve their original stable key in the same column.
    view->listBoxItemDoubleClicked(0, click(*list, 930));
    edit->setText("Local notes without DAW", false);
    REQUIRE(tree.flush().wasOk(), "Legacy Notes save");
    REQUIRE(model.catalog.localNote(model.catalog.songs[0].path,
        arranger::taskNoteKey(legacyId)) == "Local notes without DAW", "Legacy note identity");
    view->listBoxItemClicked(0, click(*list, 10));
    view->listBoxItemClicked(1, click(*list, 200));
    edit->setText("Legacy subtask", false); edit->onReturnKey();
    REQUIRE(model.catalog.songs[0].tasks[0].checkpoints.size() == 1,
        "Legacy inline checkpoint creation failed");

    const auto cp = model.findTask(first)->checkpoints[0].id;
    const int beforeArchive = view->getNumRows();
    REQUIRE(commands.archiveCheckpoint(first, cp).wasOk(), "Archive");
    tree.setTasks(refs);
    REQUIRE(view->getNumRows() == beforeArchive - 1, "Archived child leaked");
    REQUIRE(commands.restoreCheckpoint(first, cp).wasOk(), "Restore");
    tree.setTasks(refs);
    REQUIRE(view->getNumRows() == beforeArchive, "Restoring checkpoint changed row count");
    allowFlush = false; tree.showTaskMenu(refs[1], {0, 0});
    REQUIRE(!juce::PopupMenu::dismissAllActiveMenus(), "Menu bypassed flush guard");
    allowFlush = true;
    REQUIRE(juce::StringArray::fromTokens(tree.columnSettings(), ",", "").size() == 6,
        "Task table must persist six columns including Date");
    REQUIRE(snapshot(tree, "task-table-compact.png"), "Task snapshot");

    // Real table drop target dispatch: a nested task is dropped inside another
    // task row, not directly sent to a domain command by this test.
    arranger::WorkspaceModel dragModel;
    arranger::WorkspaceCommands dragCmd(dragModel);
    juce::String dragPage, from, to, nested;
    REQUIRE(dragCmd.createPage({}, "Test page", dragPage).wasOk(), "Drag owner page");
    REQUIRE(dragCmd.createTask(dragPage, "Parent A", from).wasOk(), "Drag source root");
    REQUIRE(dragCmd.createTask(dragPage, "Parent B", to).wasOk(), "Drag destination root");
    REQUIRE(dragCmd.createSubtask(from, "Nested task", nested).wasOk(), "Drag child");
    TaskTree dragTree(dragModel, [&](const auto& action) { return action(dragCmd); },
        [] { return true; }, [](arranger::TaskReference) {}, [] {}, [](juce::String) {});
    dragTree.setLookAndFeel(&theme);
    dragTree.setSize(1180, 360);
    dragTree.setTasks(arranger::WorkspaceQueries::allTasks(dragModel));
    auto* dragList = dynamic_cast<juce::ListBox*>(dragTree.getChildComponent(0));
    REQUIRE(dragList && dragList->getListBoxModel(), "Drag list");
    dragList->getListBoxModel()->listBoxItemClicked(0, click(*dragList, 10));
    REQUIRE(dragList->getListBoxModel()->getNumRows() == 4, "Nested drag rows");
    const auto targetRow = dragList->getRowPosition(3, true);
    const auto targetPoint = juce::Point<int>(200, dragList->getY() + targetRow.getY()
        + dragList->getRowHeight() / 2);
    const auto descriptor = "manager-task|" + nested + "|" + dragPage + "|" + from;
    const juce::DragAndDropTarget::SourceDetails drop {juce::var(descriptor), dragList, targetPoint};
    REQUIRE(dragTree.isInterestedInDragSource(drop), "Table ignored task drag source");
    dragTree.itemDragMove(drop);
    dragTree.itemDropped(drop);
    REQUIRE(dragModel.findTask(nested)->parentTaskId == to,
        "Dropping a nested row did not reparent the actual task");

    // Calendar cards must identify the original task's parent, even with
    // identical child titles in other projects and deeply nested subtrees.
    juce::String deepChild;
    REQUIRE(dragCmd.createSubtask(nested, "Edit stems", deepChild).wasOk(),
        "Create deep calendar label fixture");
    const auto parentCard = arranger::calendarCardLabels(dragModel, *dragModel.findTask(nested));
    const auto subCard = arranger::calendarCardLabels(dragModel, *dragModel.findTask(deepChild));
    REQUIRE(parentCard.context == "Test page · Parent B"
        && parentCard.compact == "Parent B › Nested task",
        "Calendar card omitted the page or immediate parent");
    REQUIRE(subCard.context == "Test page · Parent B › Nested task"
        && subCard.compact == "Nested task › Edit stems"
        && subCard.full == "Test page · Parent B › Nested task › Edit stems",
        "Deep calendar subtask card omitted a parent or its own title");
    const auto oldParent = dragModel.findTask(to)->properties;
    auto renamedParent = oldParent; renamedParent.name = "Rehearsal";
    REQUIRE(dragCmd.updateTask(to, oldParent, renamedParent).wasOk(),
        "Rename parent in live calendar model");
    REQUIRE(arranger::calendarCardLabels(dragModel, *dragModel.findTask(deepChild)).context
        == "Test page · Rehearsal › Nested task",
        "Calendar ancestry was not updated after parent rename");
    dragTree.setLookAndFeel(nullptr);

    // #38/0.3.1-Upd2: day-specific order is UI state, not a source tree
    // reorder; filtering a visible subset must preserve other IDs.
    const std::vector<juce::String> initialCards {"A", "B", "C"};
    const auto movedCards = arranger::moveDayCard(initialCards, "C", "A", false);
    REQUIRE(movedCards == std::vector<juce::String>({"C", "A", "B"}),
        "Moving an all-day card did not update visible ID order");
    REQUIRE(arranger::moveDayCard(initialCards, "invalid", "A", false) == initialCards,
        "Moving a stale card changed the visual order");
    REQUIRE(arranger::effectiveDayOrder({"A", "B", "C", "D"}, {"C", "A"})
            == std::vector<juce::String>({"C", "A", "B", "D"}),
        "Day ordering lost filtered or newly scheduled IDs");
    arranger::DayFocusSettings focused;
    focused.projectId = dragPage;
    focused.status = "WIP";
    focused.hideDone = true;
    focused.allDayHeight = 385;
    focused.dayOrder["2026-11-10"] = movedCards;
    focused.dayOrder["2026-11-11"] = {"B", "A"};
    const auto savedOrder = arranger::decodeDayFocus(arranger::encodeDayFocus(focused));
    REQUIRE(savedOrder.projectId == dragPage && savedOrder.status == "WIP"
        && savedOrder.hideDone && savedOrder.allDayHeight == 385
        && savedOrder.dayOrder.at("2026-11-10") == movedCards
        && savedOrder.dayOrder.at("2026-11-11") != movedCards,
        "Day-focus settings did not survive JSON round trip or separated dates");
    REQUIRE(arranger::decodeDayFocus("1,340").allDayHeight == 0
        && arranger::decodeDayFocus("{broken").dayOrder.empty(),
        "Old/corrupt calendar view settings were not safely ignored");
    juce::String otherPage, otherUnplanned;
    REQUIRE(dragCmd.createPage({}, "Other project", otherPage).wasOk()
        && dragCmd.createTask(otherPage, "Edit stems", otherUnplanned).wasOk(),
        "Create second project fixture");
    auto found = arranger::unplannedTaskIds(dragModel, "Edit stems", {});
    REQUIRE(std::find(found.begin(), found.end(), deepChild) != found.end()
        && std::find(found.begin(), found.end(), otherUnplanned) != found.end(),
        "Unplanned picker lost same-named tasks from different projects");
    found = arranger::unplannedTaskIds(dragModel, "Rehearsal", {});
    REQUIRE(std::find(found.begin(), found.end(), deepChild) != found.end()
        && std::find(found.begin(), found.end(), otherUnplanned) == found.end(),
        "Unplanned search did not use the actual parent task chain");
    focused.status.clear();
    found = arranger::unplannedTaskIds(dragModel, "Edit stems", focused);
    REQUIRE(found.size() == 1 && found[0] == deepChild,
        "Project focus did not hide unplanned tasks from another project");
    auto deepOriginal = dragModel.findTask(deepChild)->properties;
    auto deepDone = deepOriginal;
    deepDone.status = arranger::Status::done;
    REQUIRE(dragCmd.updateTask(deepChild, deepOriginal, deepDone).wasOk(),
        "Make a completed unplanned-task fixture");
    REQUIRE(arranger::unplannedTaskIds(dragModel, "Edit stems", focused).empty(),
        "Hide-DONE focus left a completed task in unplanned search");
    focused.hideDone = false;
    focused.status = "DONE";
    REQUIRE(arranger::unplannedTaskIds(dragModel, "Edit stems", focused).size() == 1,
        "DONE filter did not display the completed task");
    focused.status.clear();

    // #38/0.3.1: the SAME local task is scheduled by dropping its tree
    // descriptor into the persistent day sidebar (not a new task copy).
    const auto beforeCalendarTaskCount = dragModel.tasks.size();
    juce::String savedCalendarFocus;
    DayCalendarPanel calendar(dragModel,
        [&](const arranger::WorkspaceAction& action) { return action(dragCmd); },
        [] { return true; }, [] {}, [](arranger::TaskReference) {}, [] {},
        {}, [&](juce::String value) { savedCalendarFocus = value; });
    calendar.setLookAndFeel(&theme);
    calendar.setSize(340, 760);
    calendar.setDay("2026-11-10");
    const juce::DragAndDropTarget::SourceDetails toAllDay {
        juce::var("manager-task|" + nested + "|" + dragPage + "|" + to),
        &dragList[0], {55, 126}
    };
    REQUIRE(calendar.isInterestedInDragSource(toAllDay),
        "Calendar rejected an ordinary nested task");
    calendar.itemDropped(toAllDay);
    REQUIRE(dragModel.tasks.size() == beforeCalendarTaskCount
        && dragModel.findTask(nested)->ownerPageId == dragPage
        && dragModel.findTask(nested)->parentTaskId == to
        && dragModel.findTask(nested)->plan.start == "2026-11-10",
        "Dragging a task into all-day created a copy or changed ownership");
    arranger::CalendarEvent liveEvent;
    liveEvent.title = "Concert"; liveEvent.start = "2026-11-10T19:00";
    liveEvent.end = "2026-11-10T20:00";
    juce::String eventId;
    REQUIRE(dragCmd.createCalendarEvent(liveEvent, eventId).wasOk(),
        "Could not create standalone calendar event");
    calendar.refresh();
    juce::Viewport* calendarViewport = nullptr;
    for (int i = 0; i < calendar.getNumChildComponents(); ++i)
        if (auto* viewportCandidate =
            dynamic_cast<juce::Viewport*>(calendar.getChildComponent(i)))
            calendarViewport = viewportCandidate;
    REQUIRE(calendarViewport != nullptr, "Daily calendar timeline viewport missing");
    const int beforeStretch = calendarViewport->getY();
    const float grip = static_cast<float>(beforeStretch - 6);
    calendar.mouseDown(mouseAt(calendar, 100.0f, grip));
    calendar.mouseDrag(mouseAt(calendar, 100.0f, grip + 72.0f));
    calendar.mouseUp(mouseAt(calendar, 100.0f, grip + 72.0f));
    REQUIRE(calendarViewport->getY() >= beforeStretch + 60,
        "Dragging the lower all-day border failed to extend the day area");
    REQUIRE(arranger::decodeDayFocus(savedCalendarFocus).allDayHeight > 110,
        "All-day resized height was not saved outside the workspace model");
    for (auto scale : {1.0f, 1.5f, 2.0f})
    {
        const auto image = calendar.createComponentSnapshot(calendar.getLocalBounds(), true, scale);
        REQUIRE(image.isValid() && image.getWidth() >= 340,
            "Daily calendar sidebar DPI snapshot failed");
    }
    REQUIRE(snapshot(calendar, "calendar-sidebar.png"), "Calendar sidebar preview");
    calendar.setLookAndFeel(nullptr);



    arranger::BlockEditorSession::Blocks saved {{"heading-id","heading",{},{},false,juce::String::fromUTF8("Материалы проекта")},
        {"paragraph-id","text",juce::String::fromUTF8("Здесь можно писать описание, идеи и заметки прямо на странице."),{},false,{}}};
    bool pageWrites=true;
    BlockEditor pageEditor; pageEditor.setLookAndFeel(&theme); pageEditor.setSize(980,600);
    auto read=[&]() { return std::optional(saved); };
    auto write=[&](const auto& expected,const auto& value) { if (!pageWrites || expected!=saved) return juce::Result::fail("Page write conflict"); saved=value; return juce::Result::ok(); };
    REQUIRE(pageEditor.bind("page-fixture",read,write).wasOk(),"Page bind");
    auto* viewport=dynamic_cast<juce::Viewport*>(pageEditor.getChildComponent(0)); REQUIRE(viewport,"Document viewport");
    auto* canvas=viewport->getViewedComponent(); REQUIRE(canvas && canvas->getNumChildComponents()==3,"Expected document rows, no inspector");
    auto* heading=firstText(*canvas->getChildComponent(1)); REQUIRE(heading && heading->getText()==saved[0].name,"Name-only heading disappeared");
    REQUIRE(pageEditor.flush().wasOk() && saved[0].text.isEmpty(),"Unedited old block changed");
    heading->setText(juce::String::fromUTF8("План записи"),false); REQUIRE(pageEditor.flush().wasOk() && saved[0].text==heading->getText(),"Inline heading save");
    heading->setText(juce::String::fromUTF8("Материалы проекта"),false); REQUIRE(pageEditor.flush().wasOk() && saved[0].text==heading->getText(),"Reverting text to first shown value failed");
    const auto count=saved.size(); heading->onReturnKey(); juce::MessageManager::getInstance()->runDispatchLoopUntil(30);
    REQUIRE(saved.size()==count+1 && saved[0].id=="heading-id" && saved.back().id=="paragraph-id","Enter lost IDs/order");
    REQUIRE(pageEditor.undo().wasOk() && saved.size()==count,"Page undo"); REQUIRE(pageEditor.redo().wasOk() && saved.size()==count+1,"Page redo");
    auto* field=firstText(*canvas->getChildComponent(2)); REQUIRE(field,"Paragraph editor");
    pageWrites=false; field->setText("Retained page draft",false);
    REQUIRE(pageEditor.flush().failed() && field->getText()=="Retained page draft","Page failure lost text");
    REQUIRE(pageEditor.bind("other",read,write).failed(),"Navigation ignored failed page save");
    pageWrites=true; REQUIRE(pageEditor.flush().wasOk(),"Page retry");
    REQUIRE(snapshot(pageEditor,"page-document.png"),"Page snapshot");
    pageEditor.setLookAndFeel(nullptr); tree.setLookAndFeel(nullptr);
    std::cout<<"Compact rows, notes/subtask inline input, cancel/blur/failure, legacy identity, document Enter/Undo/persistence passed\n";
}
