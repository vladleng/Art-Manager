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
juce::Colour statusColour(arranger::Status value)
{
    switch (value)
    {
        case arranger::Status::done: return juce::Colour(0xff38ba7a);
        case arranger::Status::wip: return juce::Colour(0xff429ded);
        case arranger::Status::wait: return juce::Colour(0xffd7a535);
        case arranger::Status::blocked: return juce::Colour(0xffdf6464);
        case arranger::Status::draft: return juce::Colour(0xffa48be0);
        default: return juce::Colour(0xff9299a1);
    }
}
struct Columns
{
    int name = 176, progress, priority, source;
    explicit Columns(int width) : progress(juce::jmax(290, width - 390)), priority(progress + 150), source(priority + 74) {}
};
void pill(juce::Graphics& g, juce::String caption, juce::Colour colour, int x, int y, int width, bool dot)
{
    g.setColour(colour.withAlpha(0.35f)); g.fillRoundedRectangle(static_cast<float>(x), static_cast<float>(y), static_cast<float>(width), 25, 12);
    if (dot) { g.setColour(colour); g.fillEllipse(static_cast<float>(x + 10), static_cast<float>(y + 8), 9, 9); }
    g.setColour(juce::Colour(0xffedf1f5)); g.setFont(juce::FontOptions(14.0f));
    g.drawText(caption, x + (dot ? 23 : 0), y, width - (dot ? 27 : 0), 25, dot ? juce::Justification::centredLeft : juce::Justification::centred, true);
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
TaskTree::TaskTree(arranger::WorkspaceModel& data, Execute run, std::function<bool()> flush,
    std::function<void(arranger::TaskReference)> openTask, std::function<void()> notify,
    std::function<void(juce::String)> archiveTask, std::function<void()> newTask)
    : model(data), execute(std::move(run)), before(std::move(flush)), open(std::move(openTask)),
      changed(std::move(notify)), create(std::move(newTask)), archive(std::move(archiveTask))
{
    addAndMakeVisible(list); list.setRowHeight(44); list.setMultipleSelectionEnabled(false);
    list.setColour(juce::ListBox::backgroundColourId, juce::Colour(0xff12181e));
    list.setColour(juce::ListBox::outlineColourId, juce::Colours::transparentBlack);
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
    list.repaint(); repaint();
}
void TaskTree::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff12181e));
    const Columns columns(list.getVisibleContentWidth());
    g.setColour(juce::Colour(0xff263039)); g.drawHorizontalLine(39, 0, static_cast<float>(getWidth()));
    g.setColour(juce::Colour(0xff9eabb9)); g.setFont(juce::FontOptions(14.0f));
    g.drawText("Status", 46, 0, 100, 39, juce::Justification::centredLeft);
    g.drawText(checkpointsOnly ? tr("Чек-поинт") : tr("Задача"), columns.name, 0, columns.progress - columns.name, 39, juce::Justification::centredLeft);
    g.drawText(tr("Прогресс"), columns.progress + 12, 0, 135, 39, juce::Justification::centredLeft);
    g.drawText(tr("Приоритет"), columns.priority + 5, 0, 72, 39, juce::Justification::centredLeft);
    if (!checkpointsOnly) g.drawText(tr("Страница"), columns.source + 12, 0, getWidth() - columns.source - 16, 39, juce::Justification::centredLeft);
    if (rows.empty())
    {
        g.setColour(juce::Colour(0xff8393a4));
        g.drawText(tr("Пока пусто. Правый клик — добавить задачу или чек-поинт."), 18, 55, getWidth() - 36, 50, juce::Justification::centredLeft, true);
    }
}
void TaskTree::resized() { list.setBounds(getLocalBounds().withTrimmedTop(40)); }
int TaskTree::getNumRows() { return static_cast<int>(rows.size()); }
void TaskTree::paintListBoxItem(int index, juce::Graphics& g, int width, int height, bool selected)
{
    if (index < 0 || index >= getNumRows()) return;
    const auto& row = rows[static_cast<size_t>(index)]; const Columns columns(width);
    if (selected)
    {
        g.setColour(juce::Colour(0xff20344c)); g.fillRoundedRectangle(1, 1, static_cast<float>(width - 2), static_cast<float>(height - 2), 5);
        g.setColour(juce::Colour(0xff3e699d)); g.drawRoundedRectangle(1, 1, static_cast<float>(width - 2), static_cast<float>(height - 2), 5, 1);
    }
    g.setColour(juce::Colour(0xff263039)); g.drawHorizontalLine(height - 1, 0, static_cast<float>(width));
    for (int depth = 0; depth < row.depth; ++depth) g.drawVerticalLine(18 + depth * 18, 0, static_cast<float>(height));
    const int indent = row.depth * 18;
    if (row.depth > 0) g.drawHorizontalLine(height / 2, static_cast<float>(indent), static_cast<float>(indent + 10));
    if (row.notes)
    {
        auto caption = note(model, row); if (caption.isEmpty()) caption = tr("Заметок пока нет");
        g.setColour(juce::Colour(0xffa5b4c3)); g.setFont(juce::FontOptions(13.0f));
        g.drawFittedText(caption, 48 + indent, 4, width - 64 - indent, height - 8, juce::Justification::centredLeft, 2, 1.0f);
        return;
    }
    juce::Path arrow;
    const float x = static_cast<float>(12 + indent), y = static_cast<float>(height / 2);
    if (expanded.contains(row.key())) { arrow.startNewSubPath(x, y - 3); arrow.lineTo(x + 12, y - 3); arrow.lineTo(x + 6, y + 4); }
    else { arrow.startNewSubPath(x + 3, y - 6); arrow.lineTo(x + 3, y + 6); arrow.lineTo(x + 10, y); }
    arrow.closeSubPath(); g.setColour(juce::Colour(0xffdce6ef)); g.fillPath(arrow);
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
    pill(g, arranger::label(status), statusColour(status), 40 + indent, 9, 92, true);
    g.setColour(row.checkpointId.isEmpty() ? juce::Colour(0xff7aaee9) : juce::Colour(0xff38ba7a));
    g.setFont(juce::FontOptions(16.0f));
    const int nameX = columns.name + indent;
    if (row.checkpointId.isEmpty()) g.drawText(tr("▤"), nameX, 0, 24, height, juce::Justification::centredLeft);
    else
    {
        const float iconX = static_cast<float>(nameX + 3), iconY = static_cast<float>(height / 2 - 7);
        g.setColour(status == arranger::Status::done ? juce::Colour(0xff38ba7a) : juce::Colour(0xff8495a6));
        g.drawRoundedRectangle(iconX, iconY, 14, 14, 3, 1.5f);
        if (status == arranger::Status::done)
        {
            juce::Path tick; tick.startNewSubPath(iconX + 3, iconY + 7);
            tick.lineTo(iconX + 6, iconY + 10); tick.lineTo(iconX + 11, iconY + 4);
            g.strokePath(tick, juce::PathStrokeType(1.8f));
        }
    }
    g.setColour(juce::Colour(0xffedf1f5));
    g.drawText(name, nameX + 28, 0, juce::jmax(0, columns.progress - nameX - 36), height, juce::Justification::centredLeft, true);
    const int percent = total > 0 ? done * 100 / total : 0;
    g.setFont(juce::FontOptions(12.0f)); g.setColour(juce::Colour(0xffa4afbd));
    g.drawText(juce::String(percent) + "%", columns.progress + 10, 0, 36, height, juce::Justification::centredLeft);
    g.setColour(juce::Colour(0xff252e37)); g.fillRoundedRectangle(static_cast<float>(columns.progress + 52), 19, 84, 7, 3);
    if (percent > 0) { g.setColour(juce::Colour(0xff38ba7a)); g.fillRoundedRectangle(static_cast<float>(columns.progress + 52), 19, 84.0f * static_cast<float>(percent) / 100.0f, 7, 3); }
    if (priority > 0 && row.checkpointId.isEmpty())
    {
        const juce::Colour colours[] = {juce::Colour(0xffdb6262), juce::Colour(0xffd8994c), juce::Colour(0xff429ded), juce::Colour(0xff38ba7a)};
        pill(g, "P" + juce::String(priority), colours[priority - 1], columns.priority + 12, 9, 42, false);
    }
    if (!checkpointsOnly && row.checkpointId.isEmpty())
    {
        g.setColour(juce::Colour(0xff93a2b4)); g.setFont(juce::FontOptions(12.0f));
        g.drawText(arranger::WorkspaceQueries::breadcrumb(model, row.reference.ownerPageId), columns.source + 12, 0,
            juce::jmax(0, width - columns.source - 20), height, juce::Justification::centredLeft, true);
    }
    g.setColour(juce::Colour(0xff263039));
    for (const int column : {columns.name - 8, columns.progress, columns.priority, columns.source})
        g.drawVerticalLine(column, 0, static_cast<float>(height));
}
void TaskTree::toggle(int index)
{
    if (index < 0 || index >= getNumRows()) return;
    const auto row = rows[static_cast<size_t>(index)]; if (row.notes) return;
    if (!expanded.erase(row.key())) expanded.insert(row.key());
    rebuild();
}
void TaskTree::listBoxItemClicked(int index, const juce::MouseEvent& event)
{
    if (index < 0 || index >= getNumRows()) return;
    const auto row = rows[static_cast<size_t>(index)];
    if (event.mods.isPopupMenu()) showMenu(row, event.getScreenPosition());
    else if (event.x < 36 + row.depth * 18) toggle(index);
}
void TaskTree::listBoxItemDoubleClicked(int index, const juce::MouseEvent& event)
{
    if (event.mods.isPopupMenu() || index < 0 || index >= getNumRows()) return;
    const auto row = rows[static_cast<size_t>(index)];
    if (row.checkpointId.isEmpty() && open) open(row.reference);
    else if (row.notes) editCheckpoint(row.reference, row.checkpointId);
    else toggle(index);
}
void TaskTree::returnKeyPressed(int index) { toggle(index); }
void TaskTree::backgroundClicked(const juce::MouseEvent& event) { if (event.mods.isPopupMenu()) showBackgroundMenu(event.getScreenPosition()); }
void TaskTree::mouseDown(const juce::MouseEvent& event) { if (event.mods.isPopupMenu()) showBackgroundMenu(event.getScreenPosition()); }
void TaskTree::showTaskMenu(const arranger::TaskReference& ref, juce::Point<int> position) { showMenu({ref, {}, false, 0}, position); }
void TaskTree::showBackgroundMenu(juce::Point<int> position)
{
    if (checkpointsOnly && !references.empty()) { showTaskMenu(references.front(), position); return; }
    if (!create) return;
    juce::PopupMenu menu; menu.addItem(1, tr("Создать задачу"));
    juce::Component::SafePointer<TaskTree> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withTargetScreenArea({position.x, position.y, 1, 1}),
        [safe](int item) { if (safe != nullptr && item == 1 && safe->create) safe->create(); });
}
void TaskTree::apply(const arranger::WorkspaceAction& action)
{
    if (before && !before()) return;
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
    juce::PopupMenu menu;
    if (open) menu.addItem(1, task ? tr("Открыть задачу") : tr("Открыть песню в DAW"));
    if (task)
    {
        menu.addItem(2, cp ? tr("Изменить чек-поинт / Notes") : tr("Переименовать / свойства / Notes"));
        juce::PopupMenu statusMenu;
        const auto currentStatus = cp ? cp->status : task->properties.status;
        for (int i = 0; i < 7; ++i) statusMenu.addItem(20 + i, arranger::label(statuses[i]), true, statuses[i] == currentStatus);
        menu.addSubMenu(tr("Статус"), statusMenu);
        if (!cp)
        {
            juce::PopupMenu priorities;
            for (int i = 1; i <= 4; ++i) priorities.addItem(30 + i, "P" + juce::String(i), true, task->properties.priority == i);
            menu.addSubMenu(tr("Приоритет"), priorities);
            menu.addSeparator(); menu.addItem(3, tr("Добавить чек-поинт"));
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
            if (item == 3) { safe->editCheckpoint(ref); return; }
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
