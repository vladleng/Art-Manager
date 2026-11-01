#include "WorkspaceShell.h"

namespace
{
juce::String text(const char* value) { return juce::String::fromUTF8(value); }
void rowBackground(juce::Graphics& g, int width, int height, bool selected)
{
    if (selected) { g.setColour(juce::Colour(0xff33475f)); g.fillRoundedRectangle(2.0f, 1.0f, static_cast<float>(width - 4), static_cast<float>(height - 2), 5.0f); }
    g.setColour(juce::Colour(0xffe6edf5));
    g.setFont(juce::FontOptions(15.0f));
}
}

WorkspaceShell::WorkspaceShell(arranger::WorkspaceModel& data, std::unique_ptr<HubEditor> editor,
    Execute executeCommand, juce::String selectedView, std::function<void(juce::String)> saveView)
    : model(data), hub(std::move(editor)), runCommand(std::move(executeCommand)),
      persistView(std::move(saveView)), viewKey(std::move(selectedView)),
      tasks(model, runCommand, [this] { return flushEdits(); },
          [this](arranger::TaskReference ref) { openTask(ref); }, [this] { refresh(); },
          [this](juce::String id) { archiveTask(id); }, [this] { promptTask(); })
{
    brand.setText("Arranger Manager", juce::dontSendNotification);
    brand.setFont(juce::FontOptions(19.0f, juce::Font::bold));
    title.setFont(juce::FontOptions(26.0f, juce::Font::bold));
    for (auto* label : { &brand, &title, &breadcrumb, &description, &empty })
    {
        addAndMakeVisible(label);
        label->setColour(juce::Label::textColourId, juce::Colour(0xffdce6f1));
    }
    breadcrumb.setColour(juce::Label::textColourId, juce::Colour(0xff9eafc3));
    description.setColour(juce::Label::textColourId, juce::Colour(0xffb8c5d5));
    addPage.setButtonText(text("+ Страница"));
    addChild.setButtonText(text("+ Вложенная"));
    rename.setButtonText(text("Переименовать"));
    move.setButtonText(text("Переместить"));
    archiveButton.setButtonText(text("В архив"));
    for (auto* tab : { &blocksTab, &tasksTab })
    {
        tab->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff192028));
        tab->setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff23476f));
        tab->setColour(juce::TextButton::textColourOnId, juce::Colour(0xffecf3fb));
    }
    addPage.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff23476f));
    blocksTab.setButtonText(text("Блоки"));
    tasksTab.setButtonText(text("Задачи"));
    for (auto* button : { &addPage, &addChild, &rename, &move, &archiveButton, &blocksTab, &tasksTab }) addAndMakeVisible(button);
    for (auto* list : { &nav, &archive })
    {
        addAndMakeVisible(list);
        list->setRowHeight(list == &nav ? 44 : 52);
        list->setColour(juce::ListBox::backgroundColourId, juce::Colour(0xff192028));
        list->setMultipleSelectionEnabled(false);
    }
    addAndMakeVisible(tasks);
    addAndMakeVisible(blockEditor);
    addAndMakeVisible(hub.get());
    taskDetails = std::make_unique<TaskDetails>(model, [this](const auto& action) { return runCommand(action); },
        [this](juce::String id) { select("pageTasks:" + id); },
        [this](juce::String id) { archiveTask(id); },
        [this](juce::String id)
        {
            if (viewKey == "task:" + id)
                if (const auto* task = model.findTask(id)) title.setText(task->properties.name, juce::dontSendNotification);
        });
    addAndMakeVisible(taskDetails.get());

    addPage.onClick = [this] { promptPage({}); };
    addChild.onClick = [this] { promptPage(selectedPage()); };
    rename.onClick = [this] { promptPage({}, selectedPage()); };
    move.onClick = [this] { showMoveMenu(selectedPage(), move.getScreenBounds().getBottomLeft()); };
    archiveButton.onClick = [this] { archivePage(selectedPage()); };
    blocksTab.onClick = [this] { select("page:" + selectedPage()); };
    tasksTab.onClick = [this] { select("pageTasks:" + selectedPage()); };
    refresh();
}

void WorkspaceShell::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff12181e));
    g.setColour(juce::Colour(0xff192028));
    g.fillRect(0, 0, 240, getHeight());
    g.setColour(juce::Colour(0xff2b3540));
    g.drawVerticalLine(240, 0.0f, static_cast<float>(getHeight()));
}

void WorkspaceShell::resized()
{
    brand.setBounds(14, 12, 210, 32);
    addPage.setBounds(12, 54, 216, 30);
    nav.setBounds(10, 98, 220, juce::jmax(20, getHeight() - 110));
    auto content = getLocalBounds().withTrimmedLeft(250).reduced(14);
    hub->setBounds(content);
    breadcrumb.setBounds(content.removeFromTop(28));
    title.setBounds(content.removeFromTop(48));
    description.setBounds(content.removeFromTop(32));
    content.removeFromTop(8);
    auto tabs = content.removeFromTop(32);
    blocksTab.setBounds(tabs.removeFromLeft(90).reduced(2));
    tasksTab.setBounds(tabs.removeFromLeft(110).reduced(2));
    content.removeFromTop(6);
    tasks.setBounds(content);
    archive.setBounds(content);
    blockEditor.setBounds(content);
    taskDetails->setBounds(content);
    empty.setBounds(content.withHeight(80));
}

juce::String WorkspaceShell::selectedPage() const
{
    if (viewKey.startsWith("page:")) return viewKey.substring(5);
    if (viewKey.startsWith("pageTasks:")) return viewKey.substring(10);
    return {};
}

void WorkspaceShell::refresh()
{
    const int oldArchiveIndex = archive.getSelectedRow();
    const juce::String oldArchiveId = oldArchiveIndex >= 0 && oldArchiveIndex < static_cast<int>(archived.size())
        ? archived[static_cast<size_t>(oldArchiveIndex)] : juce::String();
    rebuilding = true;
    navigation = {{"view:daw", text("DAW"), 0}, {"view:tasks", text("Все задачи"), 0}};
    for (const auto& row : arranger::WorkspaceQueries::activePages(model))
        if (const auto* page = model.findPage(row.id))
            navigation.push_back({"page:" + page->id, page->title, row.depth});
    archived = arranger::WorkspaceQueries::archivedRoots(model);
    for (const auto& task : model.tasks) if (task.archivedAt.isNotEmpty()) archived.push_back("task:" + task.id);
    navigation.push_back({"view:archive", text("Архив") + " (" + juce::String(static_cast<int>(archived.size())) + ")", 0});
    juce::String navKey = viewKey;
    if (viewKey.startsWith("pageTasks:")) navKey = "page:" + selectedPage();
    if (viewKey.startsWith("task:"))
    {
        const auto* task = model.findTask(viewKey.substring(5));
        if (task && !arranger::WorkspaceQueries::taskArchived(model, task->id)) navKey = "page:" + task->ownerPageId;
    }
    int selected = -1;
    for (int i = 0; i < static_cast<int>(navigation.size()); ++i)
        if (navigation[static_cast<size_t>(i)].key == navKey) selected = i;
    if (selected < 0) { viewKey = "view:daw"; selected = 0; }
    nav.updateContent();
    nav.selectRow(selected);
    nav.repaint();
    taskReferences = arranger::WorkspaceQueries::allTasks(model);
    if (viewKey.startsWith("pageTasks:"))
        std::erase_if(taskReferences, [&](const auto& ref) { return ref.ownerPageId != selectedPage(); });
    tasks.setTasks(taskReferences);
    archive.updateContent();
    archive.deselectAllRows();
    for (int i = 0; i < static_cast<int>(archived.size()); ++i)
        if (archived[static_cast<size_t>(i)] == oldArchiveId) archive.selectRow(i);
    archive.repaint();
    rebuilding = false;
    if (persistView) persistView(viewKey);
    showDetails();
}

void WorkspaceShell::select(const juce::String& key)
{
    if (!flushEdits())
    {
        rebuilding = true;
        for (int i = 0; i < static_cast<int>(navigation.size()); ++i)
            if (navigation[static_cast<size_t>(i)].key == viewKey) nav.selectRow(i);
        rebuilding = false;
        return;
    }
    viewKey = key;
    refresh();
}

void WorkspaceShell::showDetails()
{
    const bool daw = viewKey == "view:daw", allTasks = viewKey == "view:tasks", archivedView = viewKey == "view:archive";
    const bool pageTasks = viewKey.startsWith("pageTasks:");
    hub->setVisible(daw); // DAW focus transition may save/replace the model.
    std::optional<arranger::WorkspacePage> page;
    if (const auto* found = model.findPage(selectedPage())) page = *found;
    std::optional<arranger::WorkspaceTask> task;
    if (viewKey.startsWith("task:")) if (const auto* found = model.findTask(viewKey.substring(5))) task = *found;
    const bool ordinary = page && page->kind == "page", taskView = task.has_value();
    tasks.setVisible(allTasks || (ordinary && pageTasks));
    archive.setVisible(archivedView);
    taskDetails->setVisible(taskView);
    for (auto* component : std::initializer_list<juce::Component*>{ &title, &breadcrumb, &description }) component->setVisible(!daw);
    for (auto* button : { &addChild, &rename, &move, &archiveButton }) button->setVisible(false);
    for (auto* button : { &blocksTab, &tasksTab }) button->setVisible(ordinary);
    blocksTab.setToggleState(ordinary && !pageTasks, juce::dontSendNotification);
    tasksTab.setToggleState(ordinary && pageTasks, juce::dontSendNotification);
    blockEditor.setVisible(ordinary && !pageTasks);
    if (ordinary && !pageTasks)
    {
        const auto pageId = page->id;
        blockEditor.bind(pageId, [this, pageId]() -> std::optional<arranger::BlockEditorSession::Blocks>
        {
            const auto* current = model.findPage(pageId);
            if (!current || current->kind != "page" || arranger::WorkspaceQueries::isArchived(model, pageId)) return {};
            return current->blocks;
        }, [this, pageId](const auto& expected, const auto& blocks)
        {
            return runCommand([&](arranger::WorkspaceCommands& cmd) { return cmd.replacePageBlocks(pageId, expected, blocks); });
        });
    }
    else blockEditor.bind({}, {}, {});
    taskDetails->bind(taskView ? task->id : juce::String());
    empty.setVisible(false);
    if (allTasks)
    {
        breadcrumb.setText(text("Рабочее пространство"), juce::dontSendNotification);
        title.setText(text("Все задачи"), juce::dontSendNotification);
        description.setText(text("Задач: ") + juce::String(static_cast<int>(taskReferences.size()))
            + text("  ·  Стрелка — раскрыть · правый клик — действия"), juce::dontSendNotification);
    }
    else if (archivedView)
    {
        breadcrumb.setText(text("Рабочее пространство"), juce::dontSendNotification);
        title.setText(text("Архив"), juce::dontSendNotification);
        description.setText(text("Правый клик — восстановить. Для задачи сначала восстановите страницу-владельца."), juce::dontSendNotification);
        if (archived.empty()) { empty.setText(text("Архив пуст."), juce::dontSendNotification); empty.setVisible(true); }
    }
    else if (ordinary)
    {
        breadcrumb.setText(arranger::WorkspaceQueries::breadcrumb(model, page->id), juce::dontSendNotification);
        title.setText(page->title, juce::dontSendNotification);
        int count = 0;
        for (const auto& item : model.tasks)
            if (item.ownerPageId == page->id && !arranger::WorkspaceQueries::taskArchived(model, item.id)) ++count;
        tasksTab.setButtonText(text("Задачи (") + juce::String(count) + ")");
        description.setText(text("Задач страницы: ") + juce::String(count) + text(" · Стрелка — раскрыть · правый клик — действия"), juce::dontSendNotification);
    }
    else if (taskView)
    {
        breadcrumb.setText(arranger::WorkspaceQueries::breadcrumb(model, task->ownerPageId), juce::dontSendNotification);
        title.setText(task->properties.name, juce::dontSendNotification);
        description.setText(text("Задача страницы · свойства, Notes, чек-поинты и описание"), juce::dontSendNotification);
    }

}

bool WorkspaceShell::execute(const arranger::WorkspaceAction& action)
{
    if (!flushEdits()) return false;
    const auto result = runCommand(action);
    if (result.failed()) { report(result); return false; }
    refresh();
    return true;
}
bool WorkspaceShell::flushEdits()
{
    auto result = blockEditor.flush();
    if (result.wasOk()) result = taskDetails->flush();
    if (result.failed()) report(result);
    return result.wasOk();
}
void WorkspaceShell::report(const juce::Result& result)
{
    juce::NativeMessageBox::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
        text("Не удалось сохранить изменение"), result.getErrorMessage(), this);
}

void WorkspaceShell::promptPage(const juce::String& parentId, const juce::String& renameId)
{
    if (!flushEdits()) return;
    const auto* existing = model.findPage(renameId);
    const bool renaming = existing != nullptr;
    const auto currentTitle = renaming ? existing->title : juce::String();
    auto* dialog = new juce::AlertWindow(renaming ? text("Переименовать страницу") : text("Новая страница"),
        text("Название страницы"), juce::MessageBoxIconType::NoIcon, this);
    dialog->addTextEditor("title", currentTitle, text("Название"));
    dialog->addButton(text("Сохранить"), 1, juce::KeyPress(juce::KeyPress::returnKey));
    dialog->addButton(text("Отмена"), 0, juce::KeyPress(juce::KeyPress::escapeKey));
    juce::Component::SafePointer<WorkspaceShell> safe(this);
    dialog->enterModalState(true, juce::ModalCallbackFunction::create([safe, dialog, parentId, renameId](int result)
    {
        if (safe == nullptr || result != 1) return;
        const auto title = dialog->getTextEditorContents("title");
        juce::String id = renameId;
        const bool changed = safe->execute([&](arranger::WorkspaceCommands& commands)
        {
            return renameId.isNotEmpty() ? commands.renamePage(renameId, title) : commands.createPage(parentId, title, id);
        });
        if (changed) safe->select("page:" + id);
    }), true);
}

void WorkspaceShell::showPageMenu(const juce::String& id, juce::Point<int> position)
{
    juce::PopupMenu menu;
    menu.addItem(1, text("Новая вложенная страница"));
    menu.addItem(2, text("Переименовать"));
    menu.addItem(3, text("Переместить"));
    menu.addSeparator();
    menu.addItem(4, text("В архив"));
    juce::Component::SafePointer<WorkspaceShell> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&nav).withTargetScreenArea({position.x, position.y, 1, 1}),
        [safe, id, position](int item)
        {
            if (safe == nullptr) return;
            if (item == 1) safe->promptPage(id);
            else if (item == 2) safe->promptPage({}, id);
            else if (item == 3) safe->showMoveMenu(id, position);
            else if (item == 4) safe->archivePage(id);
        });
}

void WorkspaceShell::showMoveMenu(const juce::String& id, juce::Point<int> position)
{
    if (id.isEmpty()) return;
    juce::PopupMenu menu;
    menu.addItem(1, text("На верхний уровень"));
    std::vector<juce::String> targets;
    for (const auto& row : arranger::WorkspaceQueries::activePages(model))
        if (!arranger::WorkspaceQueries::isDescendant(model, row.id, id))
        {
            targets.push_back(row.id);
            menu.addItem(static_cast<int>(targets.size()) + 1, arranger::WorkspaceQueries::breadcrumb(model, row.id));
        }
    juce::Component::SafePointer<WorkspaceShell> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withTargetScreenArea({position.x, position.y, 1, 1}),
        [safe, id, targets](int item)
        {
            if (safe == nullptr || item <= 0 || item > static_cast<int>(targets.size()) + 1) return;
            const auto parent = item == 1 ? juce::String() : targets[static_cast<size_t>(item - 2)];
            if (safe->execute([&](arranger::WorkspaceCommands& commands) { return commands.movePage(id, parent); }))
                safe->select("page:" + id);
        });
}

void WorkspaceShell::archivePage(const juce::String& id)
{
    if (execute([&](arranger::WorkspaceCommands& commands) { return commands.archivePage(id); })) select("view:archive");
}
void WorkspaceShell::restoreArchived(const juce::String& value)
{
    const auto id = value;
    if (id.startsWith("task:"))
    {
        if (execute([&](arranger::WorkspaceCommands& cmd) { return cmd.restoreTask(id.substring(5)); })) select(id);
    }
    else if (execute([&](arranger::WorkspaceCommands& cmd) { return cmd.restorePage(id); })) select("page:" + id);
}
void WorkspaceShell::archiveTask(const juce::String& id)
{
    const auto taskId = id;
    if (execute([&](arranger::WorkspaceCommands& cmd) { return cmd.archiveTask(taskId); })) select("view:archive");
}
void WorkspaceShell::openTask(arranger::TaskReference ref)
{
    if (ref.songId.isEmpty()) { select("task:" + ref.taskId); return; }
    const auto* song = arranger::WorkspaceQueries::song(model, ref.songId);
    if (!song) return;
    const auto path = song->path;
    select("view:daw");
    if (viewKey == "view:daw") hub->openCatalogSong(path);
}
void WorkspaceShell::promptTask()
{
    if (!flushEdits()) return;
    auto* dialog = new juce::AlertWindow(text("Новая задача"), {}, juce::MessageBoxIconType::NoIcon, this);
    dialog->addTextEditor("name", {}, text("Название"));
    std::vector<juce::String> owners;
    juce::StringArray labels;
    for (const auto& row : arranger::WorkspaceQueries::activePages(model))
    {
        owners.push_back(row.id); labels.add(arranger::WorkspaceQueries::breadcrumb(model, row.id));
    }
    if (owners.empty()) { delete dialog; report(juce::Result::fail(text("Сначала создайте обычную страницу."))); return; }
    dialog->addComboBox("owner", labels, text("Страница"));
    int selected = 1;
    for (int i = 0; i < static_cast<int>(owners.size()); ++i) if (owners[static_cast<size_t>(i)] == selectedPage()) selected = i + 1;
    dialog->getComboBoxComponent("owner")->setSelectedId(selected);
    dialog->addButton(text("Создать"), 1, juce::KeyPress(juce::KeyPress::returnKey));
    dialog->addButton(text("Отмена"), 0, juce::KeyPress(juce::KeyPress::escapeKey));
    juce::Component::SafePointer<WorkspaceShell> safe(this);
    dialog->enterModalState(true, juce::ModalCallbackFunction::create([safe, dialog, owners](int choice)
    {
        if (safe == nullptr || choice != 1) return;
        const int index = dialog->getComboBoxComponent("owner")->getSelectedId() - 1;
        if (index < 0 || index >= static_cast<int>(owners.size())) return;
        const auto name = dialog->getTextEditorContents("name"); juce::String created;
        if (safe->execute([&](arranger::WorkspaceCommands& cmd)
            { return cmd.createTask(owners[static_cast<size_t>(index)], name, created); })) safe->select("task:" + created);
    }), true);
}

int WorkspaceShell::NavModel::getNumRows() { return static_cast<int>(owner.navigation.size()); }
void WorkspaceShell::NavModel::paintListBoxItem(int index, juce::Graphics& g, int width, int height, bool selected)
{
    if (index < 0 || index >= getNumRows()) return;
    rowBackground(g, width, height, selected);
    const auto& row = owner.navigation[static_cast<size_t>(index)];
    const int indent = 10 + juce::jmin(row.depth, 6) * 16;
    g.drawText(row.title, indent, 0, juce::jmax(0, width - indent - 10), height, juce::Justification::centredLeft, true);
}
void WorkspaceShell::NavModel::selectedRowsChanged(int index)
{
    if (!owner.rebuilding && index >= 0 && index < getNumRows()) owner.select(owner.navigation[static_cast<size_t>(index)].key);
}
void WorkspaceShell::NavModel::listBoxItemClicked(int index, const juce::MouseEvent& event)
{
    if (index < 0 || index >= getNumRows() || !event.mods.isPopupMenu()) return;
    const auto key = owner.navigation[static_cast<size_t>(index)].key;
    if (key.startsWith("page:")) owner.showPageMenu(key.substring(5), event.getScreenPosition());
}
void WorkspaceShell::NavModel::listBoxItemDoubleClicked(int index, const juce::MouseEvent&)
{
    if (index < 0 || index >= getNumRows()) return;
    const auto key = owner.navigation[static_cast<size_t>(index)].key;
    if (key.startsWith("page:")) owner.promptPage({}, key.substring(5));
}
int WorkspaceShell::ArchiveModel::getNumRows() { return static_cast<int>(owner.archived.size()); }
void WorkspaceShell::ArchiveModel::paintListBoxItem(int index, juce::Graphics& g, int width, int height, bool selected)
{
    if (index < 0 || index >= getNumRows()) return;
    rowBackground(g, width, height, selected);
    const auto id = owner.archived[static_cast<size_t>(index)];
    juce::String title, ownerId;
    if (id.startsWith("task:"))
    {
        const auto* task = owner.model.findTask(id.substring(5)); if (!task) return;
        title = text("Задача: ") + task->properties.name; ownerId = task->ownerPageId;
    }
    else
    {
        const auto* page = owner.model.findPage(id); if (!page) return;
        title = text("Страница: ") + page->title; ownerId = page->id;
    }
    g.drawText(title, 12, 2, width - 24, 26, juce::Justification::centredLeft, true);
    g.setColour(juce::Colour(0xffa9bbce)); g.setFont(juce::FontOptions(13.0f));
    g.drawText(arranger::WorkspaceQueries::breadcrumb(owner.model, ownerId), 12, 29, width - 24, height - 30, juce::Justification::centredLeft, true);
}
void WorkspaceShell::ArchiveModel::listBoxItemClicked(int index, const juce::MouseEvent& event)
{
    if (!event.mods.isPopupMenu() || index < 0 || index >= getNumRows()) return;
    const auto id = owner.archived[static_cast<size_t>(index)];
    juce::PopupMenu menu; menu.addItem(1, text("Восстановить"));
    juce::Component::SafePointer<WorkspaceShell> safe(&owner);
    const auto point = event.getScreenPosition();
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&owner.archive)
        .withTargetScreenArea({point.x, point.y, 1, 1}), [safe, id](int choice)
    {
        if (safe != nullptr && choice == 1) safe->restoreArchived(id);
    });
}
void WorkspaceShell::ArchiveModel::listBoxItemDoubleClicked(int index, const juce::MouseEvent& event)
{
    if (!event.mods.isPopupMenu() && index >= 0 && index < getNumRows())
        owner.restoreArchived(owner.archived[static_cast<size_t>(index)]);
}
