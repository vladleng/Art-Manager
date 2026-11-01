#include "TaskTree.h"
#include "SongNoteKeys.h"
namespace
{
juce::String tr(const char* value) { return juce::String::fromUTF8(value); }
constexpr arranger::Status statuses[] = {arranger::Status::pool, arranger::Status::todo, arranger::Status::wip,
    arranger::Status::draft, arranger::Status::wait, arranger::Status::done, arranger::Status::blocked};
int statusId(arranger::Status value)
{
    for (int i = 0; i < 7; ++i) if (statuses[i] == value) return i + 1;
    return 2;
}
struct Columns
{
    int name = 126, progress, priority, source;
    explicit Columns(int width) : progress(juce::jmax(240, width - 340)), priority(progress + 140), source(priority + 70) {}
};
void pill(juce::Graphics& g, juce::String caption, juce::Colour colour, int x, int y, int width, bool dot, mr::Icons* icons = nullptr, const char* iconId = nullptr)
{
    g.setColour(colour); g.fillRoundedRectangle(static_cast<float>(x), static_cast<float>(y), static_cast<float>(width), 20, 4);
    if (dot)
    {
        if (icons && iconId) icons->draw(iconId, g, {static_cast<float>(x + 7), static_cast<float>(y + 5), 9, 9});
        else { g.setColour(mr::colour(mr::tokens::meta)); g.drawEllipse(static_cast<float>(x + 9), static_cast<float>(y + 6), 7, 7, 1); }
    }
    g.setColour(juce::Colours::white); g.setFont(juce::FontOptions(12.0f));
    g.drawText(caption, x + (dot ? 21 : 0), y, width - (dot ? 21 : 0), 20, dot ? juce::Justification::centredLeft : juce::Justification::centred, true);
}
const arranger::TaskCheckpoint* checkpoint(const arranger::WorkspaceModel& model, const arranger::TaskTreeRow& row)
{
    if (const auto* task = arranger::WorkspaceQueries::generalTask(model, row.reference))
        for (const auto& cp : task->checkpoints) if (cp.id == row.checkpointId) return &cp;
    return nullptr;
}
juce::String note(const arranger::WorkspaceModel& model, const arranger::TaskTreeRow& row)
{
    if (const auto* task = arranger::WorkspaceQueries::generalTask(model, row.reference))
    {
        if (row.checkpointId.isEmpty()) return task->properties.notes;
        if (const auto* cp = checkpoint(model, row)) return cp->notes;
    }
    if (const auto* song = arranger::WorkspaceQueries::song(model, row.reference.songId))
    {
        const auto key = row.checkpointId.isEmpty() ? arranger::taskNoteKey(row.reference.taskId) : arranger::checkpointNoteKey(row.checkpointId);
        for (const auto& item : song->localNotes) if (item.key == key) return item.text;
    }
    return {};
}
}
TaskTree::TaskTree(arranger::WorkspaceModel& data, Execute run, std::function<bool()> flushPending,
    std::function<void(arranger::TaskReference)> openTask, std::function<void()> notify,
    std::function<void(juce::String)> archiveTask, std::function<void()> newTask)
    : model(data), execute(std::move(run)), before(std::move(flushPending)), open(std::move(openTask)),
      changed(std::move(notify)), create(std::move(newTask)), archive(std::move(archiveTask))
{
    addAndMakeVisible(list); list.setRowHeight(32); list.setMultipleSelectionEnabled(false);
    list.setColour(juce::ListBox::backgroundColourId, mr::colour(mr::tokens::canvas));
    list.setColour(juce::ListBox::outlineColourId, juce::Colours::transparentBlack);
    addChildComponent(inlineEditor); inlineEditor.setFont(juce::FontOptions(14.0f));
    inlineEditor.addKeyListener(this);
    inlineEditor.onReturnKey = [this] { flush(); };
    inlineEditor.onEscapeKey = [this] { cancelInline(); };
    inlineEditor.onFocusLost = [this] { if (!committing && editing) flush(); };
    setWantsKeyboardFocus(true);
}
void TaskTree::setTasks(std::vector<arranger::TaskReference> refs, bool only)
{
    references = std::move(refs); checkpointsOnly = only; rebuild();
}
void TaskTree::rebuild()
{
    const int selected = list.getSelectedRow();
    const auto key = selected >= 0 && selected < getNumRows() ? rows[static_cast<size_t>(selected)].key() : juce::String();
    rows = arranger::taskTreeRows(model, references, expanded, checkpointsOnly);
    list.updateContent(); list.setVisible(!rows.empty()); list.deselectAllRows();
    for (int i = 0; i < getNumRows(); ++i) if (rows[static_cast<size_t>(i)].key() == key) list.selectRow(i, true, false);
    list.repaint(); repaint(); positionInline();
}
void TaskTree::paint(juce::Graphics& g)
{
    g.fillAll(mr::colour(mr::tokens::canvas));
    const Columns columns(list.getVisibleContentWidth());
    g.setColour(mr::colour(mr::tokens::outline)); g.drawHorizontalLine(31, 0, static_cast<float>(getWidth()));
    g.setColour(mr::colour(mr::tokens::muted)); g.setFont(juce::FontOptions(12.0f));
    g.drawText(tr("Статус"), 26, 0, 100, 31, juce::Justification::centredLeft);
    g.drawText(checkpointsOnly ? tr("Подзадача") : tr("Задача"), columns.name, 0, columns.progress - columns.name, 31, juce::Justification::centredLeft);
    g.drawText(tr("Прогресс"), columns.progress + 12, 0, 135, 31, juce::Justification::centredLeft);
    g.drawText(tr("Приоритет"), columns.priority + 5, 0, 72, 31, juce::Justification::centredLeft);
    if (!checkpointsOnly) g.drawText(tr("Страница"), columns.source + 12, 0, getWidth() - columns.source - 16, 31, juce::Justification::centredLeft);
    if (rows.empty())
    {
        g.setColour(mr::colour(mr::tokens::muted));
        g.drawText(tr("Пока пусто. Правый клик — добавить задачу или чек-поинт."), 18, 55, getWidth() - 36, 50, juce::Justification::centredLeft, true);
    }
}
void TaskTree::resized() { list.setBounds(getLocalBounds().withTrimmedTop(32)); positionInline(); }
int TaskTree::getNumRows() { return static_cast<int>(rows.size()); }
void TaskTree::paintListBoxItem(int index, juce::Graphics& g, int width, int height, bool selected)
{
    if (index < 0 || index >= getNumRows()) return;
    const auto& row = rows[static_cast<size_t>(index)]; const Columns columns(width);
    if (selected)
    {
        g.setColour(mr::colour(mr::tokens::selected)); g.fillRoundedRectangle(1, 1, static_cast<float>(width - 2), static_cast<float>(height - 2), 5);
        g.setColour(mr::colour(mr::tokens::focus)); g.drawRoundedRectangle(1, 1, static_cast<float>(width - 2), static_cast<float>(height - 2), 5, 1);
    }
    g.setColour(mr::colour(mr::tokens::outline)); g.drawHorizontalLine(height - 1, 0, static_cast<float>(width));
    for (int depth = 0; depth < row.depth; ++depth) g.drawVerticalLine(12 + depth * 14, 0, static_cast<float>(height));
    const int indent = row.depth * 14;
    if (row.depth > 0) g.drawHorizontalLine(height / 2, static_cast<float>(indent), static_cast<float>(indent + 10));
    if (row.addChild)
    {
        g.setColour(mr::colour(mr::tokens::muted)); g.setFont(juce::FontOptions(13.0f));
        g.drawText(tr("+ Новая подзадача"), columns.name + indent + 18, 0, width - columns.name - indent - 24, height, juce::Justification::centredLeft, true);
        return;
    }
    if (row.notes)
    {
        auto caption = note(model, row); if (caption.isEmpty()) caption = tr("Заметок пока нет");
        g.setColour(mr::colour(mr::tokens::muted)); g.setFont(juce::FontOptions(13.0f));
        g.drawFittedText(caption, columns.name + indent + 20, 4, width - columns.name - indent - 36, height - 8, juce::Justification::centredLeft, 2, 1.0f);
        return;
    }
    juce::Path arrow;
    const float x = static_cast<float>(7 + indent), y = static_cast<float>(height / 2);
    if (expanded.contains(row.key())) { arrow.startNewSubPath(x, y - 3); arrow.lineTo(x + 8, y - 3); arrow.lineTo(x + 4, y + 4); }
    else { arrow.startNewSubPath(x + 3, y - 4); arrow.lineTo(x + 3, y + 4); arrow.lineTo(x + 7, y); }
    arrow.closeSubPath(); g.setColour(mr::colour(mr::tokens::bright)); g.fillPath(arrow);
    auto status = arranger::WorkspaceQueries::taskStatus(model, row.reference);
    auto name = arranger::WorkspaceQueries::taskName(model, row.reference);
    int priority = 0, done = 0, total = 0;
    if (const auto* task = arranger::WorkspaceQueries::generalTask(model, row.reference))
    {
        priority = task->properties.priority;
        if (row.checkpointId.isEmpty()) { const auto progress = arranger::WorkspaceQueries::taskProgress(*task); done = progress.first; total = progress.second; }
        else if (const auto* cp = checkpoint(model, row)) { name = cp->name; status = cp->status; }
    }
    else if (const auto* task = arranger::WorkspaceQueries::task(model, row.reference))
    {
        if (row.checkpointId.isEmpty()) for (const auto& cp : task->checkpoints) { ++total; if (cp.status == arranger::Status::done) ++done; }
        else for (const auto& cp : task->checkpoints) if (cp.id == row.checkpointId) { name = cp.name; status = cp.status; }
    }
    if (row.checkpointId.isNotEmpty()) { total = 1; done = status == arranger::Status::done ? 1 : 0; }
    pill(g, arranger::label(status), mr::statusFill(status), 24 + indent, 6, 74, true, &icons, mr::statusIcon(status));
    g.setColour(row.checkpointId.isEmpty() ? mr::colour(mr::tokens::meta) : mr::colour(mr::tokens::progressFill));
    g.setFont(juce::FontOptions(14.0f));
    const int nameX = columns.name + indent;
    if (row.checkpointId.isEmpty()) icons.draw("manager/actions/organizer", g, {static_cast<float>(nameX), static_cast<float>(height / 2 - 6), 12, 12});
    else
    {
        const float iconX = static_cast<float>(nameX + 3), iconY = static_cast<float>(height / 2 - 5);
        g.setColour(status == arranger::Status::done ? mr::colour(mr::tokens::progressFill) : mr::colour(mr::tokens::muted));
        g.drawRoundedRectangle(iconX, iconY, 10, 10, 2, 1.0f);
        if (status == arranger::Status::done)
        {
            juce::Path tick; tick.startNewSubPath(iconX + 2, iconY + 5);
            tick.lineTo(iconX + 4, iconY + 7); tick.lineTo(iconX + 8, iconY + 2);
            g.strokePath(tick, juce::PathStrokeType(1.2f));
        }
    }
    g.setColour(mr::colour(mr::tokens::bright));
    g.drawText(name, nameX + 20, 0, juce::jmax(0, columns.progress - nameX - 28), height, juce::Justification::centredLeft, true);
    const int percent = total > 0 ? done * 100 / total : 0;
    g.setFont(juce::FontOptions(12.0f)); g.setColour(mr::colour(mr::tokens::meta));
    g.drawText(juce::String(percent) + "%", columns.progress + 10, 0, 36, height, juce::Justification::centredLeft);
    g.setColour(mr::colour(mr::tokens::progressBg)); g.fillRoundedRectangle(static_cast<float>(columns.progress + 52), 14, 76, 4, 2);
    if (percent > 0) { g.setColour(mr::colour(mr::tokens::progressFill)); g.fillRoundedRectangle(static_cast<float>(columns.progress + 52), 14, 76.0f * static_cast<float>(percent) / 100.0f, 4, 2); }
    if (priority > 0 && row.checkpointId.isEmpty())
    {
        pill(g, "P" + juce::String(priority), mr::priorityFill(priority), columns.priority + 12, 6, 30, false);
    }
    if (!checkpointsOnly && row.checkpointId.isEmpty())
    {
        g.setColour(mr::colour(mr::tokens::muted)); g.setFont(juce::FontOptions(12.0f));
        g.drawText(arranger::WorkspaceQueries::breadcrumb(model, row.reference.ownerPageId), columns.source + 12, 0,
            juce::jmax(0, width - columns.source - 20), height, juce::Justification::centredLeft, true);
    }
    g.setColour(mr::colour(mr::tokens::outline));

}
void TaskTree::toggle(int index)
{
    if (index < 0 || index >= getNumRows()) return;
    const auto row = rows[static_cast<size_t>(index)]; if (row.notes || row.addChild) return;
    if (!expanded.erase(row.key())) expanded.insert(row.key());
    rebuild();
}
void TaskTree::listBoxItemClicked(int index, const juce::MouseEvent& event)
{
    if (index < 0 || index >= getNumRows()) return;
    const auto row = rows[static_cast<size_t>(index)];
    if (flush().failed()) return;
    const Columns columns(list.getVisibleContentWidth());
    if (event.mods.isPopupMenu()) { if (row.addChild) beginInline(row); else showMenu(row, event.getScreenPosition()); return; }
    if (row.addChild) { beginInline(row); return; }
    if (row.notes) return;
    if (event.x < 22 + row.depth * 14) { for (int i = 0; i < getNumRows(); ++i) if (rows[static_cast<size_t>(i)].key() == row.key()) { toggle(i); break; } return; }
    if (event.x < columns.name) { statusMenu(row,event.getScreenPosition()); return; }
    if (event.x >= columns.priority && event.x < columns.source && row.checkpointId.isEmpty())
    {
        if (const auto* task = arranger::WorkspaceQueries::generalTask(model,row.reference))
        {
            const auto expected = task->properties; juce::PopupMenu priorities; priorities.setLookAndFeel(&getLookAndFeel());
            for (int i = 1; i <= 4; ++i) priorities.addItem(i,"P" + juce::String(i),true,expected.priority == i);
            juce::Component::SafePointer<TaskTree> safe(this);
            priorities.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this),[safe,row,expected](int value)
            {
                if (!safe || value < 1 || value > 4) return;
                auto next = expected; next.priority = value;
                safe->apply([&](arranger::WorkspaceCommands& cmd) { return cmd.updateTask(row.reference.taskId,expected,next); });
            });
        }
        return;
    }
    const int nameX = columns.name + row.depth * 14;
    if (event.x >= nameX && event.x < nameX + 18)
    {
        if (row.checkpointId.isEmpty()) { if (open) open(row.reference); }
        else
        {
            auto value = arranger::Status::todo;
            if (const auto* cp = checkpoint(model,row)) value = cp->status;
            else if (const auto* task = arranger::WorkspaceQueries::task(model,row.reference)) for (const auto& cp : task->checkpoints) if (cp.id == row.checkpointId) value = cp.status;
            apply([&](arranger::WorkspaceCommands& cmd) { return cmd.setReferenceStatus(row.reference,row.checkpointId,value == arranger::Status::done ? arranger::Status::todo : arranger::Status::done); });
        }
    }
}
void TaskTree::listBoxItemDoubleClicked(int index, const juce::MouseEvent& event)
{
    if (event.mods.isPopupMenu() || index < 0 || index >= getNumRows()) return;
    beginInline(rows[static_cast<size_t>(index)]);
}
void TaskTree::returnKeyPressed(int index) { if (index >= 0 && index < getNumRows()) beginInline(rows[static_cast<size_t>(index)]); }
void TaskTree::backgroundClicked(const juce::MouseEvent& event) { if (event.mods.isPopupMenu()) showBackgroundMenu(event.getScreenPosition()); }
void TaskTree::mouseDown(const juce::MouseEvent& event) { if (event.mods.isPopupMenu()) showBackgroundMenu(event.getScreenPosition()); }
void TaskTree::showTaskMenu(const arranger::TaskReference& ref, juce::Point<int> position) { showMenu({ref, {}, false, 0}, position); }
void TaskTree::showBackgroundMenu(juce::Point<int> position)
{
    if (checkpointsOnly && !references.empty()) { showTaskMenu(references.front(), position); return; }
    if (!create) return;
    juce::PopupMenu menu; menu.setLookAndFeel(&getLookAndFeel()); menu.addItem(1, tr("Создать задачу"));
    juce::Component::SafePointer<TaskTree> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withTargetScreenArea({position.x, position.y, 1, 1}),
        [safe](int item) { if (safe != nullptr && item == 1 && safe->create) safe->create(); });
}
void TaskTree::apply(const arranger::WorkspaceAction& action)
{
    if (flush().failed() || (before && !before())) return;
    const auto result = execute(action);
    if (result.failed()) juce::NativeMessageBox::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
        tr("Не удалось сохранить"), result.getErrorMessage(), this);
    else { rebuild(); if (changed) changed(); }
}
void TaskTree::showMenu(arranger::TaskTreeRow row, juce::Point<int> position)
{
    if (before && !before()) return;
    const auto* task = arranger::WorkspaceQueries::generalTask(model, row.reference);
    const auto* cp = checkpoint(model, row);
    juce::PopupMenu menu; menu.setLookAndFeel(&getLookAndFeel());
    if (open) menu.addItem(1, task ? tr("Открыть задачу") : tr("Открыть песню в DAW"));
    if (task)
    {
        menu.addItem(2, cp ? tr("Изменить подзадачу / заметки") : tr("Переименовать / свойства / Notes"));
        juce::PopupMenu statusMenu;
        const auto currentStatus = cp ? cp->status : task->properties.status;
        for (int i = 0; i < 7; ++i) statusMenu.addItem(20 + i, arranger::label(statuses[i]), true, statuses[i] == currentStatus);
        menu.addSubMenu(tr("Статус"), statusMenu);
        if (!cp)
        {
            juce::PopupMenu priorities;
            for (int i = 1; i <= 4; ++i) priorities.addItem(30 + i, "P" + juce::String(i), true, task->properties.priority == i);
            menu.addSubMenu(tr("Приоритет"), priorities);
            menu.addSeparator(); menu.addItem(3, tr("Добавить подзадачу"));
            juce::PopupMenu restore; int item = 100;
            for (const auto& archived : task->checkpoints) if (archived.archivedAt.isNotEmpty()) restore.addItem(item++, archived.name);
            if (item > 100) menu.addSubMenu(tr("Восстановить чек-поинт"), restore);
        }
        menu.addSeparator(); menu.addItem(4, tr("Архивировать"));
    }
    if (create) { menu.addSeparator(); menu.addItem(5, tr("Создать задачу")); }
    std::vector<juce::String> archivedIds;
    if (task) for (const auto& item : task->checkpoints) if (item.archivedAt.isNotEmpty()) archivedIds.push_back(item.id);
    juce::Component::SafePointer<TaskTree> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withTargetScreenArea({position.x, position.y, 1, 1}),
        [safe, row, archivedIds](int item)
        {
            if (safe == nullptr || item == 0) return;
            const auto ref = row.reference;
            if (item == 1) { if (safe->open) safe->open(ref); return; }
            if (item == 2) { if (row.checkpointId.isEmpty()) safe->editTask(ref); else safe->editCheckpoint(ref, row.checkpointId); return; }
            if (item == 3) { auto add = row; add.checkpointId.clear(); add.notes = false; add.addChild = true; safe->expanded.insert("task:" + ref.taskId); safe->rebuild(); safe->beginInline(add); return; }
            if (item == 5) { if (safe->create) safe->create(); return; }
            if (item == 4 && row.checkpointId.isEmpty()) { if (safe->archive) safe->archive(ref.taskId); return; }
            // Resolve fresh values after pending editor fields have been flushed. No model pointers survive a menu.
            if (safe->before && !safe->before()) return;
            const auto* task = arranger::WorkspaceQueries::generalTask(safe->model, ref);
            if (!task) return;
            if (item >= 100 && item - 100 < static_cast<int>(archivedIds.size()))
            {
                const auto cpId = archivedIds[static_cast<size_t>(item - 100)];
                safe->apply([&](arranger::WorkspaceCommands& cmd) { return cmd.restoreCheckpoint(ref.taskId, cpId); }); return;
            }
            if (row.checkpointId.isNotEmpty())
            {
                const auto* found = checkpoint(safe->model, row); if (!found) return;
                const auto expected = *found; auto value = expected;
                if (item >= 20 && item < 27) value.status = statuses[item - 20];
                safe->apply([&](arranger::WorkspaceCommands& cmd)
                { return item == 4 ? cmd.archiveCheckpoint(ref.taskId, row.checkpointId) : cmd.updateCheckpoint(ref.taskId, expected, value); });
            }
            else
            {
                const auto expected = task->properties; auto value = expected;
                if (item >= 20 && item < 27) value.status = statuses[item - 20];
                else if (item >= 31 && item <= 34) value.priority = item - 30;
                else return;
                safe->apply([&](arranger::WorkspaceCommands& cmd) { return cmd.updateTask(ref.taskId, expected, value); });
            }
        });
}
void TaskTree::editTask(arranger::TaskReference ref)
{
    if (before && !before()) return;
    const auto* task = arranger::WorkspaceQueries::generalTask(model, ref); if (!task) return;
    const auto expected = task->properties;
    auto* dialog = new juce::AlertWindow(tr("Свойства задачи"), {}, juce::MessageBoxIconType::NoIcon, this);
    dialog->addTextEditor("name", expected.name, tr("Название"));
    auto notesEditor = std::make_shared<juce::TextEditor>("Notes");
    notesEditor->setMultiLine(true); notesEditor->setReturnKeyStartsNewLine(true);
    notesEditor->setText(expected.notes, false); notesEditor->setSize(440, 100);
    dialog->addCustomComponent(notesEditor.get());
    dialog->addComboBox("status", {"POOL", "TODO", "WIP", "DRAFT", "WAIT", "DONE", "BLOCKED"}, tr("Статус"));
    dialog->getComboBoxComponent("status")->setSelectedId(statusId(expected.status));
    dialog->addComboBox("priority", {"P1", "P2", "P3", "P4"}, tr("Приоритет"));
    dialog->getComboBoxComponent("priority")->setSelectedId(expected.priority);
    dialog->addButton(tr("Сохранить"), 1); dialog->addButton(tr("Отмена"), 0, juce::KeyPress(juce::KeyPress::escapeKey));
    juce::Component::SafePointer<TaskTree> safe(this);
    dialog->enterModalState(true, juce::ModalCallbackFunction::create([safe, dialog, ref, expected, notesEditor](int choice)
    {
        if (safe == nullptr || choice != 1) return;
        auto value = expected; value.name = dialog->getTextEditorContents("name"); value.notes = notesEditor->getText();
        value.status = statuses[juce::jlimit(1, 7, dialog->getComboBoxComponent("status")->getSelectedId()) - 1];
        value.priority = dialog->getComboBoxComponent("priority")->getSelectedId();
        safe->apply([&](arranger::WorkspaceCommands& cmd) { return cmd.updateTask(ref.taskId, expected, value); });
    }), true);
}
void TaskTree::editCheckpoint(arranger::TaskReference ref, juce::String cpId)
{
    if (before && !before()) return;
    const auto* task = arranger::WorkspaceQueries::generalTask(model, ref); if (!task) return;
    const bool adding = cpId.isEmpty();
    const auto* cp = checkpoint(model, {ref, cpId, false, 0}); if (!adding && !cp) return;
    const auto expected = adding ? arranger::TaskCheckpoint{} : *cp;
    auto* dialog = new juce::AlertWindow(adding ? tr("Новый чек-поинт") : tr("Изменить чек-поинт"), {}, juce::MessageBoxIconType::NoIcon, this);
    dialog->addTextEditor("name", expected.name, tr("Название"));
    std::shared_ptr<juce::TextEditor> notesEditor;
    if (!adding)
    {
        notesEditor = std::make_shared<juce::TextEditor>("Notes");
        notesEditor->setMultiLine(true); notesEditor->setReturnKeyStartsNewLine(true);
        notesEditor->setText(expected.notes, false); notesEditor->setSize(440, 100);
        dialog->addCustomComponent(notesEditor.get());
        dialog->addComboBox("status", {"POOL", "TODO", "WIP", "DRAFT", "WAIT", "DONE", "BLOCKED"}, tr("Статус"));
        dialog->getComboBoxComponent("status")->setSelectedId(statusId(expected.status));
    }
    dialog->addButton(tr("Сохранить"), 1); dialog->addButton(tr("Отмена"), 0, juce::KeyPress(juce::KeyPress::escapeKey));
    juce::Component::SafePointer<TaskTree> safe(this);
    dialog->enterModalState(true, juce::ModalCallbackFunction::create([safe, dialog, ref, expected, adding, notesEditor](int choice)
    {
        if (safe == nullptr || choice != 1) return;
        auto value = expected; value.name = dialog->getTextEditorContents("name");
        if (!adding)
        {
            value.notes = notesEditor->getText();
            value.status = statuses[juce::jlimit(1, 7, dialog->getComboBoxComponent("status")->getSelectedId()) - 1];
        }
        juce::String created;
        safe->apply([&](arranger::WorkspaceCommands& cmd)
        { return adding ? cmd.createCheckpoint(ref.taskId, value.name, created) : cmd.updateCheckpoint(ref.taskId, expected, value); });
    }), true);
}

void TaskTree::beginInline(arranger::TaskTreeRow row)
{
    if (flush().failed() || (before && !before())) return;
    expectedText = row.notes ? note(model, row) : arranger::WorkspaceQueries::taskName(model, row.reference);
    if (!row.notes && row.checkpointId.isNotEmpty())
    {
        if (const auto* cp = checkpoint(model, row)) expectedText = cp->name;
        else if (const auto* task = arranger::WorkspaceQueries::task(model, row.reference))
            for (const auto& cp : task->checkpoints) if (cp.id == row.checkpointId) expectedText = cp.name;
    }
    editing = row;
    inlineEditor.setColour(juce::TextEditor::focusedOutlineColourId, mr::colour(mr::tokens::focus));
    inlineEditor.setTooltip({});
    inlineEditor.setMultiLine(row.notes); inlineEditor.setReturnKeyStartsNewLine(row.notes);
    inlineEditor.setText(row.addChild ? juce::String() : expectedText, false);
    inlineEditor.setTextToShowWhenEmpty(row.addChild ? tr("Название подзадачи · Enter — создать") : tr("Введите заметку · Ctrl+Enter — сохранить"), mr::colour(mr::tokens::muted));
    positionInline(); inlineEditor.setVisible(true); inlineEditor.toFront(true);
    inlineEditor.grabKeyboardFocus(); inlineEditor.moveCaretToEnd();
}
void TaskTree::positionInline()
{
    if (!editing) return;
    for (int i = 0; i < getNumRows(); ++i) if (rows[static_cast<size_t>(i)].key() == editing->key())
    {
        auto bounds = list.getRowPosition(i, true).translated(list.getX(), list.getY());
        const Columns columns(list.getVisibleContentWidth());
        const int left = editing->notes ? columns.name + editing->depth * 14 + 20 : columns.name + editing->depth * 14;
        bounds.removeFromLeft(left); bounds.removeFromRight(10);
        if (editing->notes) bounds.setHeight(juce::jmin(88, juce::jmax(32, getHeight() - bounds.getY())));
        inlineEditor.setBounds(bounds.reduced(1)); return;
    }
}
void TaskTree::listWasScrolled() { positionInline(); }
void TaskTree::cancelInline(bool refocus)
{
    editing.reset(); inlineEditor.setVisible(false); if (refocus) list.grabKeyboardFocus();
}
juce::Result TaskTree::flush()
{
    if (!editing || committing) return juce::Result::ok();
    const auto row = *editing; const auto value = inlineEditor.getText();
    if ((!row.addChild && value == expectedText) || (row.addChild && value.trim().isEmpty())) { cancelInline(false); return juce::Result::ok(); }
    committing = true;
    const auto result = execute([&](arranger::WorkspaceCommands& cmd)
    {
        if (row.addChild) { juce::String id; return cmd.createTaskCheckpoint(row.reference, value, id); }
        return cmd.updateTaskText(row.reference, row.checkpointId, row.notes ? arranger::TaskTextField::notes : arranger::TaskTextField::name, expectedText, value);
    });
    if (result.wasOk())
    {
        cancelInline(false); rebuild(); if (changed) changed();
    }
    else
    {
        inlineEditor.setColour(juce::TextEditor::focusedOutlineColourId, mr::colour(mr::tokens::blockedDot));
        inlineEditor.setTooltip(result.getErrorMessage()); inlineEditor.setVisible(true); inlineEditor.grabKeyboardFocus();
    }
    committing = false; return result;
}
bool TaskTree::keyPressed(const juce::KeyPress& key, juce::Component*)
{
    if (key.getKeyCode() == juce::KeyPress::returnKey && (key.getModifiers().isCtrlDown() || key.getModifiers().isCommandDown())) { flush(); return true; }
    return false;
}
void TaskTree::statusMenu(arranger::TaskTreeRow row, juce::Point<int> position)
{
    if (flush().failed() || (before && !before())) return;
    juce::PopupMenu menu; menu.setLookAndFeel(&getLookAndFeel());
    for (int i = 0; i < 7; ++i) menu.addItem(i + 1, arranger::label(statuses[i]));
    juce::Component::SafePointer<TaskTree> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withTargetScreenArea({position.x,position.y,1,1}), [safe,row](int choice)
    {
        if (safe && choice >= 1 && choice <= 7) safe->apply([&](arranger::WorkspaceCommands& cmd) { return cmd.setReferenceStatus(row.reference,row.checkpointId,statuses[choice - 1]); });
    });
}
