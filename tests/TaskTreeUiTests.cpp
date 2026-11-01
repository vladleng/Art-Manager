#include "TaskTree.h"
#include "BlockEditor.h"
#include <iostream>
static bool check(bool value, const char* message) { if (!value) std::cerr << message << '\n'; return value; }
#define REQUIRE(value, message) do { if (!check((value), message)) return 1; } while (false)
static juce::MouseEvent click(juce::Component& component, float x)
{
    const auto now = juce::Time::getCurrentTime();
    return {juce::Desktop::getInstance().getMainMouseSource(), {x, 16}, juce::ModifierKeys::leftButtonModifier,
        1, 0, 0, 0, 0, &component, &component, now, {x, 16}, now, 1, false};
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

    // Notes now live in the right-hand column, not in an expandable detail row.
    view->listBoxItemClicked(1, click(*list, 10));
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
    view->listBoxItemDoubleClicked(1, click(*list, 748));
    REQUIRE(edit->isVisible() && edit->isMultiLine() && opened == 0, "Notes did not open in the column");
    edit->setText(juce::String::fromUTF8("Проверить демо перед записью."), false);
    REQUIRE(tree.flush().wasOk() && model.findTask(first)->properties.notes == edit->getText(),
        "Notes did not save from column");
    view->listBoxItemDoubleClicked(1, click(*list, 200));
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

    view->listBoxItemDoubleClicked(2, click(*list, 748));
    failWrites = true; edit->setText("Unsaved child notes", false);
    REQUIRE(tree.flush().failed() && edit->isVisible() && edit->getText() == "Unsaved child notes",
        "Failed write lost draft");
    REQUIRE(model.findTask(child)->properties.notes != "Unsaved child notes", "Failed write mutated data");
    failWrites = false; edit->onEscapeKey();

    view->listBoxItemDoubleClicked(2, click(*list, 748));
    edit->setText("Child note after blur", false); edit->onFocusLost();
    REQUIRE(model.findTask(child)->properties.notes == "Child note after blur", "Blur did not save child notes");

    // Legacy song Notes preserve their original stable key in the same column.
    view->listBoxItemDoubleClicked(0, click(*list, 748));
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
    REQUIRE(tree.columnSettings().isNotEmpty(), "Column settings unavailable");
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
    dragTree.setLookAndFeel(nullptr);



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
