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
    arranger::WorkspaceModel model;
    model.catalog.addSong("C:/Fixture/legacy.song");
    const auto legacyId = model.catalog.addTask(model.catalog.songs[0].path,"Local legacy task"); model.reconcileCatalogPages();
    arranger::WorkspaceCommands commands(model); juce::String page, first, second;
    REQUIRE(commands.createPage({},juce::String::fromUTF8("Разное"),page).wasOk(),"Page");
    REQUIRE(commands.createTask(page,juce::String::fromUTF8("Идеи для MRS"),first).wasOk(),"Task");
    REQUIRE(commands.createTask(page,juce::String::fromUTF8("Репетиция"),second).wasOk(),"Second task");
    for (const auto* name : {"Гармония","Рыба","Репетиция","Доработка"})
    { juce::String id; REQUIRE(commands.createCheckpoint(first,juce::String::fromUTF8(name),id).wasOk(),"Checkpoint"); }
    bool failWrites=false, allowFlush=true; int opened=0;
    mr::ManagerTheme theme;
    TaskTree tree(model,[&](const arranger::WorkspaceAction& action) { return failWrites ? juce::Result::fail("Disk failure fixture") : action(commands); },
        [&] { return allowFlush; },[&](arranger::TaskReference) { ++opened; },[] {},[](juce::String) {});
    tree.setLookAndFeel(&theme); tree.setSize(1180,520); const auto refs=arranger::WorkspaceQueries::allTasks(model); tree.setTasks(refs);
    auto* list=dynamic_cast<juce::ListBox*>(tree.getChildComponent(0)); REQUIRE(list,"List");
    auto* view=list->getListBoxModel(); REQUIRE(view->getNumRows()==3 && list->getRowHeight()==32,"Compact collapsed rows");
    view->listBoxItemClicked(1,click(*list,10)); REQUIRE(view->getNumRows()==9,"Task expands notes, four children, inline add");
    auto* edit=dynamic_cast<juce::TextEditor*>(tree.getChildComponent(1)); REQUIRE(edit,"Inline editor");
    view->listBoxItemDoubleClicked(2,click(*list,200)); REQUIRE(edit->isVisible() && opened==0,"Notes opened source instead of inline editing");
    edit->setText(juce::String::fromUTF8("Проверить демо перед записью."),false);
    REQUIRE(tree.flush().wasOk() && model.findTask(first)->properties.notes==edit->getText(),"Notes not saved from actual field");
    view->listBoxItemDoubleClicked(1,click(*list,200)); edit->setText("Cancelled name",false); edit->onEscapeKey();
    REQUIRE(!edit->isVisible() && model.findTask(first)->properties.name!= "Cancelled name","Escape changed model");
    view->listBoxItemClicked(3,click(*list,25)); REQUIRE(view->getNumRows()==10,"Subtask note expansion");
    view->listBoxItemClicked(8,click(*list,200)); REQUIRE(edit->isVisible(),"Inline add row did not activate");
    edit->setText(juce::String::fromUTF8("Записать финальный дубль"),false); edit->onReturnKey();
    REQUIRE(model.findTask(first)->checkpoints.size()==5 && !edit->isVisible(),"Enter did not create exactly one subtask");
    view->listBoxItemDoubleClicked(2,click(*list,200)); failWrites=true; edit->setText("Unsaved draft",false);
    REQUIRE(tree.flush().failed() && edit->isVisible() && edit->getText()=="Unsaved draft","Failed write lost draft");
    REQUIRE(model.findTask(first)->properties.notes!="Unsaved draft","Failed write changed live data");
    failWrites=false; edit->onEscapeKey();
    view->listBoxItemDoubleClicked(2,click(*list,200)); edit->setText(juce::String::fromUTF8("Автосохранение при выходе из ячейки"),false); edit->onFocusLost();
    REQUIRE(model.findTask(first)->properties.notes==edit->getText(),"Blur did not save notes");
    view->listBoxItemClicked(0,click(*list,10));
    view->listBoxItemDoubleClicked(1,click(*list,200)); edit->setText("Local notes without DAW",false); REQUIRE(tree.flush().wasOk(),"Legacy inline notes save");
    REQUIRE(model.catalog.localNote(model.catalog.songs[0].path,arranger::taskNoteKey(legacyId))=="Local notes without DAW","Legacy note identity");
    view->listBoxItemClicked(2,click(*list,200)); edit->setText("Legacy subtask",false); edit->onReturnKey();
    REQUIRE(model.catalog.songs[0].tasks[0].checkpoints.size()==1,"Legacy inline subtask failed");
    const auto cp=model.findTask(first)->checkpoints[0].id; const int beforeArchive=view->getNumRows();
    REQUIRE(commands.archiveCheckpoint(first,cp).wasOk(),"Archive"); tree.setTasks(refs);
    REQUIRE(view->getNumRows()==beforeArchive-2,"Archived notes/child leaked");
    REQUIRE(commands.restoreCheckpoint(first,cp).wasOk(),"Restore"); tree.setTasks(refs);
    REQUIRE(view->getNumRows()==beforeArchive,"Restore lost expansion");
    allowFlush=false; tree.showTaskMenu(refs[1],{0,0}); REQUIRE(!juce::PopupMenu::dismissAllActiveMenus(),"Menu bypassed flush guard"); allowFlush=true;
    REQUIRE(snapshot(tree,"task-table-compact.png"),"Task snapshot");

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
