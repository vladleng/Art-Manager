#include "WorkspaceShell.h"
#include "WorkspaceViewRouting.h"
#include "WorkspaceTemplateCreation.h"

namespace
{
juce::String text(const char* value) { return juce::String::fromUTF8(value); }
void rowBackground(juce::Graphics& g, int width, int height, bool selected)
{
    if (selected) { g.setColour(mr::colour(mr::tokens::selected)); g.fillRoundedRectangle(2.0f, 1.0f, static_cast<float>(width - 4), static_cast<float>(height - 2), 5.0f); }
    g.setColour(mr::colour(mr::tokens::bright));
    g.setFont(juce::FontOptions(15.0f));
}
}

WorkspaceShell::WorkspaceShell(arranger::WorkspaceModel& data,
    Execute executeCommand, juce::String selectedView, std::function<void(juce::String)> saveView,
    juce::String savedColumns, std::function<void(juce::String)> saveColumns,
    juce::String savedTaskViews, std::function<void(juce::String)> saveTaskViews, juce::File templateFile)
    : model(data), templateStore(std::move(templateFile)), runCommand(std::move(executeCommand)),
      persistView(std::move(saveView)), viewKey(std::move(selectedView)),
      persistTaskViews(std::move(saveTaskViews)),
      tasks(model, runCommand, [this] { return flushEdits(); },
          [this](arranger::TaskReference ref) { openTask(ref); }, [this] { refresh(); },
          [this](juce::String id) { archiveTask(id); }, [this] { promptTask(); },
          std::move(savedColumns), std::move(saveColumns))
{
    setLookAndFeel(&theme);
    // Default workspace timezone is Asia/Krasnoyarsk (UTC+7, no DST).
    // Explicit day editing does not depend on the OS timezone.
    const auto utcMillis = juce::Time::getCurrentTime().toMilliseconds();
    todayDay = juce::Time(utcMillis + static_cast<juce::int64>(7 * 60 * 60 * 1000))
        .toISO8601(true).substring(0, 10);
    if (viewKey == "view:daw") viewKey = "view:legacy"; // Upgrade the old persisted tab without changing workspace data.
    brand.setText("Arranger Manager", juce::dontSendNotification);
    brand.setFont(juce::FontOptions(17.0f, juce::Font::bold));
    title.setFont(juce::FontOptions(24.0f, juce::Font::bold));
    for (auto* label : { &brand, &title, &breadcrumb, &description, &empty })
    {
        addAndMakeVisible(label);
        label->setColour(juce::Label::textColourId, mr::colour(mr::tokens::bright));
    }
    breadcrumb.setColour(juce::Label::textColourId, mr::colour(mr::tokens::muted));
    description.setColour(juce::Label::textColourId, mr::colour(mr::tokens::meta));
    addPage.setButtonText(text("+ Страница"));
    addNote.setButtonText(text("+ Заметка"));
    addChild.setButtonText(text("+ Вложенная"));
    rename.setButtonText(text("Переименовать"));
    move.setButtonText(text("Переместить"));
    archiveButton.setButtonText(text("В архив"));
    for (auto* button : { &addPage, &addNote, &addChild, &rename, &move, &archiveButton })
    {
        button->setColour(juce::TextButton::buttonColourId, mr::colour(mr::tokens::selected));
        addAndMakeVisible(button);
    }
    for (auto* list : { &nav, &archive })
    {
        addAndMakeVisible(list);
        list->setRowHeight(list == &nav ? 32 : 36);
        list->setColour(juce::ListBox::backgroundColourId, mr::colour(mr::tokens::panel));
        list->setMultipleSelectionEnabled(false);
    }
    addAndMakeVisible(tasks);
    addAndMakeVisible(searchBox);
    addAndMakeVisible(todayDateEditor);
    todayDateEditor.setText(todayDay, false);
    todayDateEditor.setTextToShowWhenEmpty("YYYY-MM-DD", mr::colour(mr::tokens::muted));
    todayDateEditor.setColour(juce::TextEditor::backgroundColourId, mr::colour(mr::tokens::panel));
    todayDateEditor.setColour(juce::TextEditor::textColourId, mr::colour(mr::tokens::bright));
    todayDateEditor.setColour(juce::TextEditor::outlineColourId, mr::colour(mr::tokens::outline));
    todayDateEditor.onReturnKey = [this]
    {
        const auto proposed = todayDateEditor.getText().trim();
        if (!arranger::isCivilDate(proposed))
        {
            report(juce::Result::fail("Use a valid YYYY-MM-DD date."));
            todayDateEditor.setText(todayDay, false);
            return;
        }
        if (!flushEdits()) return;
        todayDay = proposed;
        refresh();
    };
    for (auto* button : {&filterButton, &sortButton, &clearButton})
    {
        addAndMakeVisible(button);
        button->setColour(juce::TextButton::buttonColourId, mr::colour(mr::tokens::panel));
        button->setColour(juce::TextButton::textColourOffId, mr::colour(mr::tokens::meta));
    }
    searchBox.setTextToShowWhenEmpty(text("⌕ Поиск по задачам, страницам и заметкам"),
        mr::colour(mr::tokens::muted));
    searchBox.setColour(juce::TextEditor::backgroundColourId, mr::colour(mr::tokens::panel));
    searchBox.setColour(juce::TextEditor::textColourId, mr::colour(mr::tokens::bright));
    searchBox.setColour(juce::TextEditor::outlineColourId, mr::colour(mr::tokens::outline));
    searchBox.onTextChange = [this]
    {
        if (applyingView) return;
        auto& options = taskViews[taskViewKey()];
        options.search = searchBox.getText().substring(0, 256);
        saveTaskViewSettings();
        refresh(); // Update both task matches and sidebar page/note search hits.
    };
    filterButton.onClick = [this] { showFilterMenu(); };
    sortButton.onClick = [this] { showSortMenu(); };
    clearButton.onClick = [this]
    {
        if (!flushEdits()) return;
        taskViews[taskViewKey()] = {};
        saveTaskViewSettings();
        updateTaskProjection();
    };
    loadTaskViewSettings(savedTaskViews);
    addAndMakeVisible(blockEditor);
    addAndMakeVisible(legacySummary);
    legacySummary.setReadOnly(true);
    legacySummary.setMultiLine(true, true);
    legacySummary.setScrollbarsShown(true);
    legacySummary.setColour(juce::TextEditor::backgroundColourId, mr::colour(mr::tokens::panel));
    legacySummary.setColour(juce::TextEditor::textColourId, mr::colour(mr::tokens::bright));
    legacySummary.setColour(juce::TextEditor::outlineColourId, mr::colour(mr::tokens::outline));
    taskDetails = std::make_unique<TaskDetails>(model, [this](const auto& action) { return runCommand(action); },
        [this](juce::String id) { select("pageTasks:" + id); },
        [this](juce::String id) { archiveTask(id); },
        [this](juce::String id)
        {
            if (viewKey == "task:" + id)
                if (const auto* task = model.findTask(id)) title.setText(task->properties.name, juce::dontSendNotification);
        });
    addAndMakeVisible(taskDetails.get());

    templateLoad = templateStore.load();
    tasks.saveAsTemplate = [this](arranger::TaskReference ref) { captureAsTemplate(ref.ownerPageId, ref.taskId); };
    tasks.exportTask = [this](arranger::TaskReference ref) { exportPortable(arranger::PortableScope::taskSubtree, ref.taskId); };
    addPage.onClick = [this] { showTemplateMenu(true); };
    addNote.onClick = [this] { promptPage({}, {}, true); };
    addChild.onClick = [this] { showTemplateMenu(true, selectedPage()); };
    rename.onClick = [this] { promptPage({}, selectedPage()); };
    move.onClick = [this] { showMoveMenu(selectedPage(), move.getScreenBounds().getBottomLeft()); };
    archiveButton.onClick = [this] { archivePage(selectedPage()); };
    refresh();
}

WorkspaceShell::~WorkspaceShell()
{
    juce::PopupMenu::dismissAllActiveMenus();
    setLookAndFeel(nullptr);
}

void WorkspaceShell::paint(juce::Graphics& g)
{
    g.fillAll(mr::colour(mr::tokens::canvas));
    g.setColour(mr::colour(mr::tokens::panel));
    g.fillRect(0, 0, 240, getHeight());
    g.setColour(mr::colour(mr::tokens::outline));
    g.drawVerticalLine(240, 0.0f, static_cast<float>(getHeight()));
}

void WorkspaceShell::resized()
{
    brand.setBounds(14, 12, 210, 32);
    addPage.setBounds(12, 54, 104, 30);
    addNote.setBounds(124, 54, 104, 30);
    nav.setBounds(10, 98, 220, juce::jmax(20, getHeight() - 110));
    auto content = getLocalBounds().withTrimmedLeft(250).reduced(14);
    breadcrumb.setBounds(content.removeFromTop(28));
    title.setBounds(content.removeFromTop(48));
    description.setBounds(content.removeFromTop(24));
    content.removeFromTop(8);
    // Note pages and task pages are separate kinds; no redundant two-tab UI.
    content.removeFromTop(6);
    // Notion-style minimal table toolbar, not part of the data table columns.
    const auto taskToolbar = tasks.isVisible() && viewKey != "view:legacy";
    if (taskToolbar)
    {
        auto toolbar = content.removeFromTop(36);
        auto right = toolbar.removeFromRight(260);
        clearButton.setBounds(right.removeFromRight(62).reduced(2));
        sortButton.setBounds(right.removeFromRight(96).reduced(2));
        filterButton.setBounds(right.reduced(2));
        if (viewKey == "view:today")
            todayDateEditor.setBounds(toolbar.removeFromRight(135).reduced(3));
        else todayDateEditor.setBounds({});
        searchBox.setBounds(toolbar.reduced(2, 3));
        content.removeFromTop(4);
    }
    else
    {
        searchBox.setBounds({});
        todayDateEditor.setBounds({});
        filterButton.setBounds({});
        sortButton.setBounds({});
        clearButton.setBounds({});
    }
    if (viewKey == "view:legacy")
        legacySummary.setBounds(content.removeFromTop(juce::jmin(220, content.getHeight() / 2)).reduced(0, 4));
    else
        legacySummary.setBounds({});
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
    if (viewKey.startsWith("pageBlocks:")) return viewKey.substring(11);
    return {};
}

void WorkspaceShell::refresh()
{
    const int oldArchiveIndex = archive.getSelectedRow();
    const juce::String oldArchiveId = oldArchiveIndex >= 0 && oldArchiveIndex < static_cast<int>(archived.size())
        ? archived[static_cast<size_t>(oldArchiveIndex)] : juce::String();
    rebuilding = true;
    navigation = {{"view:today", text("Сегодня"), 0},
                  {"view:tasks", text("Все задачи"), 0}, {"view:legacy", text("Архив песен"), 0}};
    const auto globalSearch = viewKey == "view:tasks"
        ? taskViews["view:tasks"].search.trim() : juce::String();
    for (const auto& row : arranger::WorkspaceQueries::activePages(model))
        if (const auto* page = model.findPage(row.id))
        {
            if (globalSearch.isNotEmpty() && !arranger::pageMatchesSearch(model, *page, globalSearch))
                continue;
            const auto marker = page->kind == "note" ? text("✎ ") : juce::String();
            navigation.push_back({"page:" + page->id,
                marker + (globalSearch.isNotEmpty()
                    ? arranger::WorkspaceQueries::breadcrumb(model, page->id) : page->title),
                globalSearch.isNotEmpty() ? 0 : row.depth});
        }
    archived = arranger::WorkspaceQueries::archivedRoots(model);
    for (const auto& task : model.tasks) if (task.archivedAt.isNotEmpty()) archived.push_back("task:" + task.id);
    navigation.push_back({"view:archive", text("Архив") + " (" + juce::String(static_cast<int>(archived.size())) + ")", 0});
    juce::String navKey = viewKey;
    if (viewKey.startsWith("pageTasks:") || viewKey.startsWith("pageBlocks:")) navKey = "page:" + selectedPage();
    if (viewKey.startsWith("task:"))
    {
        const auto* task = model.findTask(viewKey.substring(5));
        if (task && !arranger::WorkspaceQueries::taskArchived(model, task->id)) navKey = "page:" + task->ownerPageId;
    }
    int selected = -1;
    for (int i = 0; i < static_cast<int>(navigation.size()); ++i)
        if (navigation[static_cast<size_t>(i)].key == navKey) selected = i;
    if (selected < 0) { viewKey = "view:tasks"; selected = 0; }
    nav.updateContent();
    nav.selectRow(selected);
    nav.repaint();
    taskReferences = arranger::WorkspaceQueries::allTasks(model);
    if (viewKey.startsWith("pageTasks:") || viewKey.startsWith("page:") || viewKey.startsWith("pageBlocks:"))
        std::erase_if(taskReferences, [&](const auto& ref) { return ref.ownerPageId != selectedPage(); });
    else if (viewKey == "view:legacy")
        std::erase_if(taskReferences, [](const auto& ref) { return ref.songId.isEmpty(); });
    else if (viewKey == "view:today")
    {
        const auto today = arranger::tasksForDay(model, todayDay);
        std::set<juce::String> visible;
        for (const auto& ref : today.planned) visible.insert(ref.taskId);
        for (const auto& ref : today.overdue) visible.insert(ref.taskId);
        std::erase_if(taskReferences, [&](const auto& ref)
        {
            return ref.songId.isNotEmpty() || !visible.contains(ref.taskId);
        });
    }
    updateTaskProjection();
    archive.updateContent();
    archive.deselectAllRows();
    for (int i = 0; i < static_cast<int>(archived.size()); ++i)
        if (archived[static_cast<size_t>(i)] == oldArchiveId) archive.selectRow(i);
    archive.repaint();
    rebuilding = false;
    if (persistView) persistView(viewKey);
    showDetails();
    resized();
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
    const bool legacy = viewKey == "view:legacy", allTasks = viewKey == "view:tasks",
        todayView = viewKey == "view:today", archivedView = viewKey == "view:archive";
    const bool oldBlocks = viewKey.startsWith("pageBlocks:");
    std::optional<arranger::WorkspacePage> page;
    if (const auto* found = model.findPage(selectedPage())) page = *found;
    std::optional<arranger::WorkspaceTask> task;
    if (viewKey.startsWith("task:")) if (const auto* found = model.findTask(viewKey.substring(5))) task = *found;
    const bool ordinary = page && page->kind == "page";
    const bool notePage = page && page->kind == "note";
    const bool taskView = task.has_value();
    const bool showBlocks = notePage || (ordinary && oldBlocks);
    tasks.setVisible(allTasks || todayView || legacy || (ordinary && !oldBlocks));
    todayDateEditor.setVisible(todayView);
    legacySummary.setVisible(legacy);
    archive.setVisible(archivedView);
    taskDetails->setVisible(taskView);
    for (auto* component : std::initializer_list<juce::Component*>{ &title, &breadcrumb, &description }) component->setVisible(true);
    for (auto* button : { &addChild, &rename, &move, &archiveButton }) button->setVisible(false);
    blockEditor.setVisible(showBlocks);
    if (showBlocks)
    {
        const auto pageId = page->id;
        blockEditor.bind(pageId, [this, pageId]() -> std::optional<arranger::BlockEditorSession::Blocks>
        {
            const auto* current = model.findPage(pageId);
            if (!current || !arranger::WorkspaceQueries::isWorkspacePage(*current)
                || arranger::WorkspaceQueries::isArchived(model, pageId)) return {};
            return current->blocks;
        }, [this, pageId](const auto& expected, const auto& blocks)
        {
            return runCommand([&](arranger::WorkspaceCommands& cmd) { return cmd.replacePageBlocks(pageId, expected, blocks); });
        });
    }
    else blockEditor.bind({}, {}, {});
    taskDetails->bind(taskView ? task->id : juce::String());
    empty.setVisible(false);
    if (legacy)
    {
        breadcrumb.setText(text("Рабочее пространство / архив"), juce::dontSendNotification);
        title.setText(text("Сохранённые песни Studio Pro"), juce::dontSendNotification);
        description.setText(text("Локальные данные и задачи — без чтения .song. Исходные файлы не изменяются."), juce::dontSendNotification);
        legacySummary.setText(arranger::WorkspaceQueries::legacyCatalogSummary(model), false);
    }
    else if (todayView)
    {
        const auto result = arranger::tasksForDay(model, todayDay);
        breadcrumb.setText(text("Рабочее пространство / Сегодня"), juce::dontSendNotification);
        title.setText(text("Сегодня"), juce::dontSendNotification);
        description.setText(todayDay + " · " + model.timezone
            + text(" · в плане: ") + juce::String(static_cast<int>(result.planned.size()))
            + text(" · просрочено: ") + juce::String(static_cast<int>(result.overdue.size()))
            + text(" · завершено: ") + juce::String(static_cast<int>(result.completed.size())),
            juce::dontSendNotification);
        if (taskReferences.empty())
        {
            empty.setText(text("На выбранный день нет задач. Планирование — ПКМ по задаче в списке."),
                juce::dontSendNotification);
            empty.setVisible(true);
        }
    }
    else if (allTasks)
    {
        breadcrumb.setText(text("Рабочее пространство"), juce::dontSendNotification);
        title.setText(text("Все задачи"), juce::dontSendNotification);
        description.setText(juce::String(static_cast<int>(taskReferences.size()))
            + text(" задач"), juce::dontSendNotification);
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
        description.setText(oldBlocks ? text("Ранее сохранённые материалы страницы")
            : juce::String(count) + text(" задач"), juce::dontSendNotification);
    }
    else if (notePage)
    {
        breadcrumb.setText(arranger::WorkspaceQueries::breadcrumb(model, page->id), juce::dontSendNotification);
        title.setText(page->title, juce::dontSendNotification);
        description.setText(text("Заметки на полях"), juce::dontSendNotification);
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
    auto result = tasks.flush();
    if (result.wasOk()) result = blockEditor.flush();
    if (result.wasOk()) result = taskDetails->flush();
    if (result.failed()) report(result);
    return result.wasOk();
}
void WorkspaceShell::report(const juce::Result& result)
{
    juce::NativeMessageBox::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
        text("Не удалось сохранить изменение"), result.getErrorMessage(), this);
}

void WorkspaceShell::promptPage(const juce::String& parentId, const juce::String& renameId, bool asNote)
{
    if (!flushEdits()) return;
    const auto* existing = model.findPage(renameId);
    const bool renaming = existing != nullptr;
    const auto currentTitle = renaming ? existing->title : juce::String();
    auto* dialog = new juce::AlertWindow(renaming ? text("Переименовать страницу")
        : asNote ? text("Новая заметка") : text("Новая страница"),
        text("Название страницы"), juce::MessageBoxIconType::NoIcon, this);
    dialog->addTextEditor("title", currentTitle, text("Название"));
    dialog->addButton(text("Сохранить"), 1, juce::KeyPress(juce::KeyPress::returnKey));
    dialog->addButton(text("Отмена"), 0, juce::KeyPress(juce::KeyPress::escapeKey));
    juce::Component::SafePointer<WorkspaceShell> safe(this);
    dialog->enterModalState(true, juce::ModalCallbackFunction::create([safe, dialog, parentId, renameId, asNote](int result)
    {
        if (safe == nullptr || result != 1) return;
        const auto title = dialog->getTextEditorContents("title");
        juce::String id = renameId;
        const bool changed = safe->execute([&](arranger::WorkspaceCommands& commands)
        {
            return renameId.isNotEmpty() ? commands.renamePage(renameId, title)
                : asNote ? commands.createNotePage(parentId, title, id) : commands.createPage(parentId, title, id);
        });
        if (changed) safe->select("page:" + id);
    }), true);
}

void WorkspaceShell::exportPortable(arranger::PortableScope scope, const juce::String& selectedId)
{
    if (!flushEdits()) return;
    juce::String payload;
    arranger::PortablePreview preview;
    const auto result = arranger::WorkspacePortable::makeExport(model, scope, selectedId, payload, preview);
    if (result.failed()) { report(result); return; }

    auto* owner = this;
    juce::Component::SafePointer<WorkspaceShell> safe(owner);
    juce::AlertWindow::showOkCancelBox(juce::MessageBoxIconType::InfoIcon,
        text("Экспорт данных"),
        text("Предпросмотр содержимого экспорта:") + "\n\n" + preview.description()
            + "\n\n" + text("Содержит только локальные страницы, задачи и Notes. "
                             "Файлы DAW, медиа, пути и секреты не включаются."),
        text("Выбрать файл…"), text("Отмена"), this,
        juce::ModalCallbackFunction::create([safe, payload](int choice)
        {
            if (!safe || choice != 1) return;
            const auto suggested = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                .getChildFile("Art-Manager-export.artmanager.json");
            safe->exportChooser = std::make_unique<juce::FileChooser>(
                text("Куда сохранить экспорт"), suggested, "*.json");
            safe->exportChooser->launchAsync(juce::FileBrowserComponent::saveMode
                | juce::FileBrowserComponent::canSelectFiles
                | juce::FileBrowserComponent::warnAboutOverwriting,
                [safe, payload](const juce::FileChooser& chooser)
                {
                    if (!safe) return;
                    const auto destination = chooser.getResult();
                    if (destination == juce::File()) return;
                    const auto status = arranger::WorkspacePortable::writeExport(destination, payload);
                    if (status.failed()) safe->report(status);
                    else juce::NativeMessageBox::showMessageBoxAsync(
                        juce::MessageBoxIconType::InfoIcon, text("Экспорт готов"),
                        destination.getFullPathName(), safe);
                });
        }));
}

void WorkspaceShell::showPageMenu(const juce::String& id, juce::Point<int> position)
{
    juce::PopupMenu menu; menu.setLookAndFeel(&getLookAndFeel());
    menu.addItem(1, text("Новая вложенная страница…"));
    if (const auto* page = model.findPage(id); page && page->kind == "page")
        menu.addItem(7, text("Сохранить как шаблон…"));
    if (const auto* page = model.findPage(id); page && (page->kind == "page" || page->kind == "note"))
        menu.addItem(8, text("Экспортировать страницу с содержимым…"));
    menu.addItem(5, text("Новая вложенная заметка"));
    menu.addItem(2, text("Переименовать"));
    menu.addItem(3, text("Переместить"));
    if (const auto* page = model.findPage(id))
        if (page->kind == "page" && !page->blocks.empty())
        {
            menu.addItem(6, text("Открыть прежние материалы"));
            menu.addItem(9, text("Перенести прежние материалы в заметку…"));
        }
    menu.addSeparator();
    menu.addItem(4, text("В архив"));
    juce::Component::SafePointer<WorkspaceShell> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&nav).withTargetScreenArea({position.x, position.y, 1, 1}),
        [safe, id, position](int item)
        {
            if (safe == nullptr) return;
            if (item == 1) safe->showTemplateMenu(true, id);
            else if (item == 7) safe->captureAsTemplate(id);
            else if (item == 5) safe->promptPage(id, {}, true);
            else if (item == 2) safe->promptPage({}, id);
            else if (item == 3) safe->showMoveMenu(id, position);
            else if (item == 4) safe->archivePage(id);
            else if (item == 6) safe->select("pageBlocks:" + id);
            else if (item == 8) safe->exportPortable(arranger::PortableScope::pageSubtree, id);
            else if (item == 9)
            {
                if (!safe->flushEdits()) return;
                juce::AlertWindow::showOkCancelBox(juce::MessageBoxIconType::QuestionIcon,
                    text("Перенести прежние материалы"),
                    text("Блоки будут перенесены с сохранением ID в новую заметку внутри страницы. "
                         "Перед записью будет создан backup. Продолжить?"),
                    text("Перенести"), text("Отмена"), safe,
                    juce::ModalCallbackFunction::create([safe, id](int choice)
                    {
                        if (!safe || choice != 1) return;
                        juce::String noteId;
                        if (safe->execute([&](arranger::WorkspaceCommands& cmd)
                            { return cmd.moveLegacyBlocksToNote(id, noteId); }))
                            safe->select("page:" + noteId);
                    }));
            }
        });
}

void WorkspaceShell::showMoveMenu(const juce::String& id, juce::Point<int> position)
{
    if (id.isEmpty()) return;
    juce::PopupMenu menu; menu.setLookAndFeel(&getLookAndFeel());
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
    // Legacy tasks remain editable in the local catalog; no external .song or DAW is opened.
    select("view:legacy");
}
void WorkspaceShell::promptTask() { showTemplateMenu(false); }

void WorkspaceShell::showTemplateMenu(bool project, juce::String parentId)
{
    if (!flushEdits()) return;
    if (templateLoad.failed()) { report(templateLoad); return; }
    auto choices = arranger::builtinTemplates(project);
    // Empty project remains the first, familiar creation action.
    if (project)
    {
        arranger::WorkspaceTemplate blank;
        blank.content.pages.push_back({juce::Uuid().toString(), {}, text("Пустой проект"), "page", {}, {}});
        choices.insert(choices.begin(), blank);
    }
    juce::PopupMenu menu; menu.setLookAndFeel(&theme);
    menu.addSectionHeader(project ? text("Новый проект") : text("Новая задача"));
    for (int i = 0; i < static_cast<int>(choices.size()); ++i) menu.addItem(100 + i, choices[static_cast<size_t>(i)].title());
    menu.addSeparator();
    for (const auto& definition : templateStore.definitions)
        if (definition.project == project)
        {
            menu.addItem(100 + static_cast<int>(choices.size()), definition.title());
            choices.push_back(definition);
        }
    menu.addSeparator(); menu.addItem(1, text("+ Новый шаблон…"), templateLoad.wasOk());
    if (templateLoad.failed()) menu.addItem(2, text("Каталог шаблонов повреждён — файл сохранён"), false);
    juce::PopupMenu edit, remove;
    std::vector<arranger::WorkspaceTemplate> custom;
    for (const auto& definition : templateStore.definitions) if (definition.project == project)
    {
        edit.addItem(1000 + static_cast<int>(custom.size()), definition.title());
        remove.addItem(2000 + static_cast<int>(custom.size()), definition.title());
        custom.push_back(definition);
    }
    if (!custom.empty()) { menu.addSubMenu(text("Редактировать шаблон"), edit); menu.addSubMenu(text("Удалить шаблон"), remove); }
    juce::Component::SafePointer<WorkspaceShell> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(project ? static_cast<juce::Component*>(&addPage) : &tasks)
        .withStandardItemHeight(28), [safe, project, parentId, choices, custom](int item)
    {
        if (!safe) return;
        if (item == 1)
        {
            auto definition = arranger::builtinTemplates(project).front();
            definition.content.pages.front().title = text("Новый шаблон");
            if (project) definition.content.tasks.clear();
            safe->editTemplate(std::move(definition));
        }
        else if (item >= 2000 && item < 2000 + static_cast<int>(custom.size()))
        { const auto result = safe->templateStore.remove(custom[static_cast<size_t>(item - 2000)].id()); if (result.failed()) safe->report(result); }
        else if (item >= 1000 && item < 1000 + static_cast<int>(custom.size())) safe->editTemplate(custom[static_cast<size_t>(item - 1000)]);
        else if (item >= 100 && item < 100 + static_cast<int>(choices.size())) safe->createFromTemplate(choices[static_cast<size_t>(item - 100)], parentId);
    });
}

void WorkspaceShell::editTemplate(arranger::WorkspaceTemplate definition)
{
    if (!flushEdits()) return;
    if (templateLoad.failed()) { report(templateLoad); return; }
    juce::Component::SafePointer<WorkspaceShell> safe(this);
    auto editor = std::make_unique<WorkspaceTemplateEditor>(std::move(definition), [safe](const auto& value)
    { return safe ? safe->templateStore.put(value) : juce::Result::fail("Workspace closed."); });
    auto& callout = juce::CallOutBox::launchAsynchronously(std::move(editor), addPage.getScreenBounds(), nullptr);
    callout.setDismissalMouseClicksAreAlwaysConsumed(true);
}

void WorkspaceShell::captureAsTemplate(juce::String pageId, juce::String taskId)
{
    if (!flushEdits()) return;
    auto definition = arranger::captureTemplate(model, pageId, taskId);
    const auto result = definition.validate();
    if (result.failed()) { report(result); return; }
    editTemplate(std::move(definition));
}

void WorkspaceShell::createFromTemplate(arranger::WorkspaceTemplate definition, juce::String parentId)
{
    if (!flushEdits()) return;
    juce::StringArray labels; std::vector<juce::String> owners;
    int selected = 0;
    if (!definition.project)
    {
        for (const auto& row : arranger::WorkspaceQueries::activePages(model))
            if (const auto* page = model.findPage(row.id); page && page->kind == "page")
            {
                if (row.id == selectedPage()) selected = static_cast<int>(owners.size());
                owners.push_back(row.id); labels.add(arranger::WorkspaceQueries::breadcrumb(model, row.id));
            }
        if (owners.empty()) { report(juce::Result::fail(text("Сначала создайте проект."))); return; }
    }
    juce::Component::SafePointer<WorkspaceShell> safe(this);
    auto panel = std::make_unique<WorkspaceTemplateCreation>(definition.title(), labels, owners, selected,
        [safe, definition, parentId](juce::String name, juce::String owner)
        {
            if (!safe) return juce::Result::fail("Workspace closed.");
            if (!safe->flushEdits()) return juce::Result::fail("Save pending edits first.");
            juce::String created;
            const auto result = safe->runCommand([&](arranger::WorkspaceCommands& cmd)
                { return cmd.createFromTemplate(definition, definition.project ? parentId : owner, name, created); });
            if (result.wasOk())
            {
                const auto destination = definition.project ? "page:" + created : arranger::viewAfterTaskCreation(safe->viewKey, owner);
                safe->select(destination);
            }
            return result;
        });
    auto& callout = juce::CallOutBox::launchAsynchronously(std::move(panel), addPage.getScreenBounds(), nullptr);
    callout.setDismissalMouseClicksAreAlwaysConsumed(true);
}

int WorkspaceShell::NavModel::getNumRows() { return static_cast<int>(owner.navigation.size()); }
void WorkspaceShell::NavModel::paintListBoxItem(int index, juce::Graphics& g, int width, int height, bool selected)
{
    if (index < 0 || index >= getNumRows()) return;
    rowBackground(g, width, height, selected);
    const auto& row = owner.navigation[static_cast<size_t>(index)];
    if (row.key.startsWith("page:") && row.key.substring(5) == owner.dropPageId)
    {
        g.setColour(mr::colour(mr::tokens::focus));
        g.drawRoundedRectangle(2.0f, 1.0f, static_cast<float>(width - 4),
            static_cast<float>(height - 2), 4.0f, 2.0f);
    }
    g.setColour(mr::colour(mr::tokens::bright)); // Drop outline never changes row text.
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
    g.setColour(mr::colour(mr::tokens::muted)); g.setFont(juce::FontOptions(13.0f));
    g.drawText(arranger::WorkspaceQueries::breadcrumb(owner.model, ownerId), 12, 29, width - 24, height - 30, juce::Justification::centredLeft, true);
}
void WorkspaceShell::ArchiveModel::listBoxItemClicked(int index, const juce::MouseEvent& event)
{
    if (!event.mods.isPopupMenu() || index < 0 || index >= getNumRows()) return;
    const auto id = owner.archived[static_cast<size_t>(index)];
    juce::PopupMenu menu; menu.setLookAndFeel(&owner.getLookAndFeel()); menu.addItem(1, text("Восстановить"));
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

juce::String WorkspaceShell::dropPageAt(juce::Point<int> position) const
{
    const auto local = nav.getLocalPoint(this, position);
    if (!nav.getLocalBounds().contains(local)) return {};
    const auto index = nav.getRowContainingPosition(local.x, local.y);
    if (index < 0 || index >= static_cast<int>(navigation.size())) return {};
    const auto key = navigation[static_cast<size_t>(index)].key;
    if (!key.startsWith("page:")) return {};
    const auto pageId = key.substring(5);
    const auto* page = model.findPage(pageId);
    if (!page || page->kind != "page" || arranger::WorkspaceQueries::isArchived(model, pageId))
        return {};
    return pageId;
}
bool WorkspaceShell::isInterestedInDragSource(const SourceDetails& source)
{
    return source.description.toString().startsWith("manager-task|");
}
void WorkspaceShell::itemDragMove(const SourceDetails& source)
{
    const auto next = dropPageAt(source.localPosition);
    if (next == dropPageId) return;
    dropPageId = next; nav.repaint();
}
void WorkspaceShell::itemDragExit(const SourceDetails&)
{
    dropPageId.clear(); nav.repaint();
}
void WorkspaceShell::itemDropped(const SourceDetails& source)
{
    const auto destination = dropPageAt(source.localPosition);
    dropPageId.clear(); nav.repaint();
    const auto fields = juce::StringArray::fromTokens(source.description.toString(), "|", "");
    if (destination.isEmpty() || fields.size() != 4 || fields[0] != "manager-task") return;
    const auto* task = model.findTask(fields[1]);
    if (!task || arranger::WorkspaceQueries::taskArchived(model, task->id)) return;
    if (execute([&](arranger::WorkspaceCommands& commands)
        { return commands.moveTask(fields[1], fields[2], fields[3] == "root" ? juce::String() : fields[3],
            destination, {}); }))
        select("pageTasks:" + destination);
}


juce::String WorkspaceShell::taskViewKey() const
{
    if (viewKey.startsWith("page:") || viewKey.startsWith("pageTasks:") || viewKey.startsWith("pageBlocks:"))
        return "page:" + selectedPage();
    return viewKey == "view:today" ? "view:today"
        : viewKey == "view:legacy" ? "view:legacy" : "view:tasks";
}
void WorkspaceShell::loadTaskViewSettings(const juce::String& saved)
{
    taskViews.clear();
    const auto raw = juce::JSON::parse(saved);
    if (auto* object = raw.getDynamicObject())
    {
        const auto serializedViews = object->getProperty("views");
        if (auto* views = serializedViews.getArray())
            for (const auto& item : *views)
            {
                const auto key = item.getProperty("key", {}).toString();
                if (key == "view:tasks" || key == "view:today" || key.startsWith("page:"))
                    taskViews[key] = arranger::decodeTaskView(item.getProperty("options", {}).toString());
            }
    }
}
void WorkspaceShell::saveTaskViewSettings()
{
    if (!persistTaskViews) return;
    auto result = std::make_unique<juce::DynamicObject>();
    juce::Array<juce::var> views;
    for (const auto& [key, options] : taskViews)
        if (key == "view:tasks" || key == "view:today" || key.startsWith("page:"))
        {
            auto item = std::make_unique<juce::DynamicObject>();
            item->setProperty("key", key);
            item->setProperty("options", arranger::encodeTaskView(options));
            views.add(juce::var(item.release()));
        }
    result->setProperty("views", views);
    persistTaskViews(juce::JSON::toString(juce::var(result.release())));
}
void WorkspaceShell::updateTaskProjection()
{
    const auto options = taskViews[taskViewKey()];
    applyingView = true;
    if (searchBox.getText() != options.search) searchBox.setText(options.search, false);
    applyingView = false;
    filterButton.setButtonText(options.status.isNotEmpty() || options.priority > 0
        ? text("Фильтр •") : text("Фильтр"));
    sortButton.setButtonText(options.sort == arranger::TaskViewSort::manual ? text("Сортировка")
        : text("Сорт •"));
    clearButton.setButtonText(text("Сброс"));
    const bool any = options.search.isNotEmpty() || options.status.isNotEmpty()
        || options.priority != 0 || options.sort != arranger::TaskViewSort::manual;
    clearButton.setEnabled(any);
    tasks.setViewOptions(options);
    tasks.setTasks(arranger::projectTaskView(model, taskReferences, options));
    nav.repaint();
}
void WorkspaceShell::showFilterMenu()
{
    if (!flushEdits()) return;
    const auto options = taskViews[taskViewKey()];
    juce::PopupMenu menu;
    menu.setLookAndFeel(&getLookAndFeel());
    juce::PopupMenu statuses;
    statuses.addItem(100, text("Все статусы"), true, options.status.isEmpty());
    int idx = 101;
    for (auto st : {arranger::Status::pool, arranger::Status::todo, arranger::Status::wip,
                    arranger::Status::draft, arranger::Status::wait, arranger::Status::done,
                    arranger::Status::blocked})
    {
        statuses.addItem(idx++, arranger::label(st), true,
            options.status == arranger::label(st));
    }
    juce::PopupMenu priorities;
    priorities.addItem(200, text("Все приоритеты"), true, options.priority == 0);
    for (int priority = 1; priority <= 4; ++priority)
        priorities.addItem(200 + priority, "P" + juce::String(priority), true,
            options.priority == priority);
    menu.addSubMenu(text("Статус"), statuses);
    menu.addSubMenu(text("Приоритет"), priorities);
    menu.addSeparator();
    menu.addItem(1, text("Очистить фильтры"));
    juce::Component::SafePointer<WorkspaceShell> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&filterButton),
        [safe](int selected)
        {
            if (!safe || selected == 0) return;
            auto& state = safe->taskViews[safe->taskViewKey()];
            if (selected == 1) { state.status.clear(); state.priority = 0; }
            else if (selected >= 100 && selected <= 107)
            {
                const juce::String statuses[] = {"", "POOL", "TODO", "WIP", "DRAFT", "WAIT", "DONE", "BLOCKED"};
                state.status = statuses[selected - 100];
            }
            else if (selected >= 200 && selected <= 204) state.priority = selected - 200;
            else return;
            safe->saveTaskViewSettings();
            safe->updateTaskProjection();
        });
}
void WorkspaceShell::showSortMenu()
{
    if (!flushEdits()) return;
    juce::PopupMenu menu;
    menu.setLookAndFeel(&getLookAndFeel());
    const auto value = taskViews[taskViewKey()].sort;
    menu.addItem(1, text("Ручной порядок"), true, value == arranger::TaskViewSort::manual);
    menu.addSeparator();
    menu.addItem(2, text("Название A → Я"), true, value == arranger::TaskViewSort::nameAsc);
    menu.addItem(3, text("Название Я → A"), true, value == arranger::TaskViewSort::nameDesc);
    menu.addItem(4, text("Приоритет P1 → P4"), true, value == arranger::TaskViewSort::priority);
    juce::Component::SafePointer<WorkspaceShell> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&sortButton),
        [safe](int selected)
        {
            if (!safe || selected < 1 || selected > 4) return;
            safe->taskViews[safe->taskViewKey()].sort =
                static_cast<arranger::TaskViewSort>(selected - 1);
            safe->saveTaskViewSettings();
            safe->updateTaskProjection();
        });
}
