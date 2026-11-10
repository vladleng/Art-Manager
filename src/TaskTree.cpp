#include "TaskTree.h"
#include "TaskProgressMeter.h"
#include "TaskPlanEditor.h"
#include "SongNoteKeys.h"
#include "TaskQuickProperties.h"
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
    int name = 126, progress, priority, date, notes, source, end;
    Columns(int n, int pr, int p, int d, int nt, int s)
        : progress(name + n), priority(progress + pr), date(priority + p),
          notes(date + d), source(notes + nt), end(source + s) {}
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
    std::function<void(juce::String)> archiveTask, std::function<void()> newTask,
    juce::String savedColumns, std::function<void(juce::String)> persist)
    : model(data), execute(std::move(run)), before(std::move(flushPending)), open(std::move(openTask)),
      changed(std::move(notify)), create(std::move(newTask)), archive(std::move(archiveTask)),
      persistColumns(std::move(persist))
{
    auto saved = juce::StringArray::fromTokens(savedColumns, ",", "");
    if (saved.size() == 5 || saved.size() == 6)
    {
        const bool modern = saved.size() == 6;
        const int values[] = {saved[0].getIntValue(), saved[1].getIntValue(), saved[2].getIntValue(),
            modern ? saved[3].getIntValue() : 150,
            saved[modern ? 4 : 3].getIntValue(), saved[modern ? 5 : 4].getIntValue()};
        if (values[0] >= 180 && values[0] <= 900 && values[1] >= 110 && values[1] <= 400
            && values[2] >= 80 && values[2] <= 240 && values[3] >= 100 && values[3] <= 240
            && values[4] >= 160 && values[4] <= 900 && values[5] >= 100 && values[5] <= 600)
            widths = {values[0], values[1], values[2], values[3], values[4], values[5]};
    }
    addAndMakeVisible(list); list.setRowHeight(40); list.setMultipleSelectionEnabled(false);
    resizeColumns();
    list.setColour(juce::ListBox::backgroundColourId, mr::colour(mr::tokens::canvas));
    list.setColour(juce::ListBox::outlineColourId, juce::Colours::transparentBlack);
    addChildComponent(inlineEditor); inlineEditor.setFont(juce::FontOptions(14.0f));
    inlineEditor.addKeyListener(this);
    inlineEditor.onReturnKey = [this] { flush(); };
    inlineEditor.onEscapeKey = [this] { cancelInline(); };
    inlineEditor.onTextChange = [this] { if (editing && editing->notes) positionInline(); };
    inlineEditor.onFocusLost = [this] { if (!committing && editing) flush(); };
    setWantsKeyboardFocus(true);
}
void TaskTree::setViewOptions(arranger::TaskViewOptions value)
{
    viewOptions = std::move(value);
    manualOrder = viewOptions.sort == arranger::TaskViewSort::manual;
    viewExpanded.clear();
    if (viewOptions.search.trim().isNotEmpty() || viewOptions.status.isNotEmpty()
        || viewOptions.priority > 0)
    {
        for (const auto& task : model.tasks)
        {
            if (task.parentTaskId.isEmpty() || arranger::WorkspaceQueries::taskArchived(model, task.id)
                || !arranger::taskMatchesView(model, {{}, task.ownerPageId, task.id}, viewOptions)) continue;
            auto parentId = task.parentTaskId;
            std::set<juce::String> visited;
            while (parentId.isNotEmpty() && visited.insert(parentId).second)
            {
                viewExpanded.insert("task:" + parentId);
                const auto* parent = model.findTask(parentId);
                parentId = parent != nullptr ? parent->parentTaskId : juce::String();
            }
        }
    }
}
void TaskTree::setTasks(std::vector<arranger::TaskReference> refs, bool only)
{
    references = std::move(refs); checkpointsOnly = only; rebuild();
}
void TaskTree::rebuild()
{
    const int selected = list.getSelectedRow();
    const auto key = selected >= 0 && selected < getNumRows() ? rows[static_cast<size_t>(selected)].key() : juce::String();
    auto effectiveExpanded = expanded;
    effectiveExpanded.insert(viewExpanded.begin(), viewExpanded.end());
    rows = arranger::taskTreeRows(model, references, effectiveExpanded, checkpointsOnly);
    list.updateContent(); list.setVisible(!rows.empty()); list.deselectAllRows();
    for (int i = 0; i < getNumRows(); ++i) if (rows[static_cast<size_t>(i)].key() == key) list.selectRow(i, true, false);
    list.repaint(); repaint(); positionInline();
}
void TaskTree::paint(juce::Graphics& g)
{
    g.fillAll(mr::colour(mr::tokens::canvas));
    const Columns columns(widths.name, widths.progress, widths.priority, widths.date, widths.notes, widths.source);
    g.setColour(mr::colour(mr::tokens::outline)); g.drawHorizontalLine(31, 0, static_cast<float>(getWidth()));
    g.setColour(mr::colour(mr::tokens::muted)); g.setFont(juce::FontOptions(12.0f));
    const int scroll = list.getViewport() != nullptr ? list.getViewport()->getViewPositionX() : 0;
    g.saveState(); g.addTransform(juce::AffineTransform::translation(static_cast<float>(-scroll), 0));
    g.drawText(tr("Статус"), 26, 0, 100, 31, juce::Justification::centredLeft);
    g.drawText(checkpointsOnly ? tr("Подзадача") : tr("Задача"), columns.name, 0, columns.progress - columns.name, 31, juce::Justification::centredLeft);
    g.drawText(tr("Прогресс"), columns.progress + 12, 0, 135, 31, juce::Justification::centredLeft);
    g.drawText(tr("Приоритет"), columns.priority + 5, 0, 72, 31, juce::Justification::centredLeft);
    g.drawText(tr("Дата"), columns.date + 9, 0, widths.date - 12, 31, juce::Justification::centredLeft);
    g.drawText(tr("Заметки"), columns.notes + 9, 0, widths.notes - 12, 31, juce::Justification::centredLeft);
    if (!checkpointsOnly) g.drawText(tr("Страница"), columns.source + 9, 0, widths.source - 12, 31, juce::Justification::centredLeft);
    for (auto boundary : {columns.progress, columns.priority, columns.date, columns.notes, columns.source, columns.end})
    {
        g.setColour(mr::colour(mr::tokens::outline));
        g.drawVerticalLine(boundary, 0, 31.0f);
    }
    g.restoreState();
    if (rows.empty())
    {
        g.setColour(mr::colour(mr::tokens::muted));
        g.drawText(tr("Пока пусто. Правый клик — добавить задачу или чек-поинт."), 18, 55, getWidth() - 36, 50, juce::Justification::centredLeft, true);
    }
}
void TaskTree::resized() { list.setBounds(getLocalBounds().withTrimmedTop(32)); resizeColumns(); positionInline(); }
int TaskTree::getNumRows() { return static_cast<int>(rows.size()); }
void TaskTree::paintListBoxItem(int index, juce::Graphics& g, int width, int height, bool selected)
{
    if (index < 0 || index >= getNumRows()) return;
    const auto& row = rows[static_cast<size_t>(index)];
    const Columns columns(widths.name, widths.progress, widths.priority, widths.date, widths.notes, widths.source);
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
        if (row.checkpointId.isEmpty()) { const auto progress = arranger::WorkspaceQueries::taskProgress(model, *task); done = progress.first; total = progress.second; }
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
        // A checkpoint already has a full status pill. A second checkbox
        // ambiguously duplicates its DONE/TODO state. Use a neutral leaf
        // marker and keep status changes exclusively in the status menu.
        g.setColour(mr::colour(mr::tokens::muted));
        g.fillEllipse(static_cast<float>(nameX + 7), static_cast<float>(height / 2 - 2), 4.0f, 4.0f);
    }
    g.setColour(mr::colour(mr::tokens::bright));
    g.drawText(name, nameX + 20, 0, juce::jmax(0, columns.progress - nameX - 28), height, juce::Justification::centredLeft, true);
    const int percent = total > 0 ? done * 100 / total : 0;
    g.setFont(juce::FontOptions(12.0f)); g.setColour(mr::colour(mr::tokens::meta));
    g.drawText(juce::String(percent) + "%", columns.progress + 10, 0, 36, height, juce::Justification::centredLeft);
    mr::paintTaskProgressMeter(g, static_cast<float>(columns.progress + 52), 14.0f, percent);
    if (priority > 0 && row.checkpointId.isEmpty())
    {
        pill(g, "P" + juce::String(priority), mr::priorityFill(priority), columns.priority + 12, 6, 30, false);
    }
    if (row.checkpointId.isEmpty())
        if (const auto* task = arranger::WorkspaceQueries::generalTask(model, row.reference))
        {
            juce::String dateCaption;
            if (task->plan.start.isNotEmpty())
            {
                dateCaption = task->plan.start.substring(0, 10);
                if (task->plan.end.isNotEmpty()
                    && task->plan.end.substring(0, 10) != dateCaption)
                    dateCaption += " – " + task->plan.end.substring(5, 10);
            }
            else if (task->plan.deadline.isNotEmpty())
                dateCaption = tr("до ") + task->plan.deadline.substring(0, 10);
            g.setColour(mr::colour(dateCaption.isEmpty() ? mr::tokens::muted : mr::tokens::bright));
            g.setFont(juce::FontOptions(12.0f));
            g.drawFittedText(dateCaption.isEmpty() ? tr("+ Дата") : dateCaption,
                columns.date + 8, 3, widths.date - 15, height - 6,
                juce::Justification::centredLeft, 1, 0.85f);
        }
    const auto caption = note(model, row);
    g.setColour(mr::colour(caption.isEmpty() ? mr::tokens::muted : mr::tokens::bright));
    g.setFont(juce::FontOptions(12.0f));
    g.drawFittedText(caption.isEmpty() ? tr("Заметок пока нет") : caption,
        columns.notes + 8, 3, widths.notes - 16, height - 6,
        juce::Justification::centredLeft, 2, 0.85f);
    if (!checkpointsOnly && row.checkpointId.isEmpty())
    {
        g.setColour(mr::colour(mr::tokens::muted)); g.setFont(juce::FontOptions(12.0f));
        g.drawText(arranger::WorkspaceQueries::breadcrumb(model, row.reference.ownerPageId), columns.source + 9, 0,
            widths.source - 14, height, juce::Justification::centredLeft, true);
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
    const Columns columns(widths.name, widths.progress, widths.priority, widths.date, widths.notes, widths.source);
    if (event.mods.isPopupMenu()) { if (row.addChild) beginInline(row); else showMenu(row, event.getScreenPosition()); return; }
    if (row.addChild) { beginInline(row); return; }
    if (event.x < 22 + row.depth * 14) { for (int i = 0; i < getNumRows(); ++i) if (rows[static_cast<size_t>(i)].key() == row.key()) { toggle(i); break; } return; }
    if (event.x < columns.name) { statusMenu(row,event.getScreenPosition()); return; }
    if (event.x >= columns.priority && event.x < columns.date && row.checkpointId.isEmpty())
    {
        if (const auto* task = arranger::WorkspaceQueries::generalTask(model,row.reference))
        {
            const auto expected = task->properties; juce::PopupMenu priorities; priorities.setLookAndFeel(&getLookAndFeel());
            for (int i = 1; i <= 4; ++i) priorities.addItem(i,"P" + juce::String(i),true,expected.priority == i);
            juce::Component::SafePointer<TaskTree> safe(this);
            priorities.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this)
                .withTargetScreenArea({event.getScreenPosition().x, event.getScreenPosition().y, 1, 1}),
                [safe,row,expected](int value)
            {
                if (!safe || value < 1 || value > 4) return;
                auto next = expected; next.priority = value;
                safe->apply([&](arranger::WorkspaceCommands& cmd) { return cmd.updateTask(row.reference.taskId,expected,next); });
            });
        }
        return;
    }
    if (event.x >= columns.date && event.x < columns.notes && row.checkpointId.isEmpty()
        && arranger::WorkspaceQueries::generalTask(model, row.reference))
    {
        if (event.getNumberOfClicks() == 1) showPlan(row.reference, event.getScreenPosition());
        return;
    }
    const int nameX = columns.name + row.depth * 14;
    if (event.x >= nameX && event.x < columns.progress)
    {
        if (event.getNumberOfClicks() > 1) return; // First click already opened the editor.
        if (arranger::WorkspaceQueries::generalTask(model, row.reference))
        {
            // A normal left click on the task name opens a small anchored editor,
            // never the redundant full-page TaskDetails view.
            if (row.checkpointId.isEmpty())
                editTask(row.reference, event.getScreenPosition());
            else if (event.x >= nameX + 18) // The checkpoint leaf glyph remains inert.
                editCheckpoint(row.reference, row.checkpointId, event.getScreenPosition());
        }
        else if (row.checkpointId.isEmpty() && event.x < nameX + 18 && open)
            open(row.reference); // Legacy archive remains accessible without Studio Pro.
    }
}
void TaskTree::listBoxItemDoubleClicked(int index, const juce::MouseEvent& event)
{
    if (event.mods.isPopupMenu() || index < 0 || index >= getNumRows()) return;
    auto row = rows[static_cast<size_t>(index)];
    const Columns columns(widths.name, widths.progress, widths.priority, widths.date, widths.notes, widths.source);
    if (!row.addChild && event.x >= columns.notes && event.x < columns.source)
    {
        row.notes = true;
        beginInline(row);
        return;
    }
    // Only the legacy name retains double-click inline editing. Task names
    // already open a quick properties callout with the first left click.
    if (row.addChild || (event.x >= columns.name && event.x < columns.progress
        && !arranger::WorkspaceQueries::generalTask(model, row.reference)))
        beginInline(row);
}
void TaskTree::returnKeyPressed(int index) { if (index >= 0 && index < getNumRows()) beginInline(rows[static_cast<size_t>(index)]); }
void TaskTree::backgroundClicked(const juce::MouseEvent& event) { if (event.mods.isPopupMenu()) showBackgroundMenu(event.getScreenPosition()); }
void TaskTree::mouseDown(const juce::MouseEvent& event)
{
    if (event.y < 32 && event.mods.isLeftButtonDown())
    {
        resizingColumn = headerDividerAt(event.x);
        if (resizingColumn >= 0)
        {
            resizeStartX = event.x;
            const int values[] = {widths.name, widths.progress, widths.priority, widths.date, widths.notes, widths.source};
            resizeStartWidth = values[resizingColumn];
            return;
        }
    }
    if (event.mods.isPopupMenu()) showBackgroundMenu(event.getScreenPosition());
}
void TaskTree::mouseDrag(const juce::MouseEvent& event)
{
    if (resizingColumn < 0) return;
    int* values[] = {&widths.name, &widths.progress, &widths.priority, &widths.date, &widths.notes, &widths.source};
    const int minimum[] = {180, 110, 80, 100, 160, 100};
    const int maximum[] = {900, 400, 240, 240, 900, 600};
    *values[resizingColumn] = juce::jlimit(minimum[resizingColumn], maximum[resizingColumn],
        resizeStartWidth + event.x - resizeStartX);
    resizeColumns();
    positionInline();
}
void TaskTree::mouseUp(const juce::MouseEvent&)
{
    if (resizingColumn >= 0)
    {
        resizingColumn = -1;
        if (persistColumns) persistColumns(columnSettings());
    }
}
void TaskTree::mouseMove(const juce::MouseEvent& event)
{
    setMouseCursor(event.y < 32 && headerDividerAt(event.x) >= 0
        ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
}
void TaskTree::mouseExit(const juce::MouseEvent&)
{
    if (resizingColumn < 0) setMouseCursor(juce::MouseCursor::NormalCursor);
}
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
    if (!task && open) menu.addItem(1, tr("Показать сохранённую песню"));
    if (task)
    {
        if (saveAsTemplate && row.checkpointId.isEmpty()) menu.addItem(6, tr("Сохранить как шаблон…"));
        if (exportTask && row.checkpointId.isEmpty()) menu.addItem(7, tr("Экспортировать задачу с подзадачами…"));
        if (row.checkpointId.isEmpty())
            menu.addItem(8, tr("Запланировать / изменить сроки…"));
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
        [safe, row, archivedIds, position](int item)
        {
            if (safe == nullptr || item == 0) return;
            const auto ref = row.reference;
            if (item == 1) { if (safe->open) safe->open(ref); return; }
            if (item == 3) { auto add = row; add.checkpointId.clear(); add.notes = false; add.addChild = true; safe->expanded.insert("task:" + ref.taskId); safe->rebuild(); safe->beginInline(add); return; }
            if (item == 5) { if (safe->create) safe->create(); return; }
            if (item == 6) { if (safe->saveAsTemplate) safe->saveAsTemplate(ref); return; }
            if (item == 7) { if (safe->exportTask) safe->exportTask(ref); return; }
            if (item == 8) { safe->showPlan(ref, position); return; }
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
void TaskTree::showPlan(arranger::TaskReference ref, juce::Point<int> position)
{
    if (flush().failed() || (before && !before())) return;
    const auto* task = arranger::WorkspaceQueries::generalTask(model, ref);
    if (!task) return;
    const auto expected = task->plan;
    juce::Component::SafePointer<TaskTree> safe(this);
    auto panel = std::make_unique<TaskPlanEditor>(expected, planningReferenceDay, [safe, ref, expected](arranger::TaskPlan next)
    {
        if (!safe) return juce::Result::fail("Task view was closed.");
        const auto result = safe->execute([&](arranger::WorkspaceCommands& cmd)
            { return cmd.updateTaskPlan(ref.taskId, expected, next); });
        if (result.wasOk())
        {
            safe->rebuild();
            if (safe->changed) safe->changed();
        }
        return result;
    });
    juce::CallOutBox::launchAsynchronously(std::move(panel),
        {position.x, position.y, 1, 1}, nullptr);
}

void TaskTree::editTask(arranger::TaskReference ref, juce::Point<int> position)
{
    if (flush().failed() || (before && !before())) return;
    const auto* task = arranger::WorkspaceQueries::generalTask(model, ref);
    if (!task) return;
    const auto expected = task->properties;
    juce::Component::SafePointer<TaskTree> safe(this);
    auto panel = std::make_unique<TaskQuickProperties>(false, expected.name, expected.notes,
        statusId(expected.status), expected.priority,
        [safe, ref, expected](juce::String name, juce::String notes, int status, int priority) -> juce::Result
        {
            if (!safe) return juce::Result::fail("Task list closed.");
            if (safe->flush().failed() || (safe->before && !safe->before()))
                return juce::Result::fail("Save pending edits first.");
            auto next = expected;
            next.name = name; next.notes = notes;
            next.status = statuses[juce::jlimit(1, 7, status) - 1];
            next.priority = juce::jlimit(1, 4, priority);
            const auto result = safe->execute([&](arranger::WorkspaceCommands& cmd)
                { return cmd.updateTask(ref.taskId, expected, next); });
            if (result.wasOk()) { safe->rebuild(); if (safe->changed) safe->changed(); }
            return result;
        });
    auto& callout = juce::CallOutBox::launchAsynchronously(std::move(panel),
        {position.x, position.y, 1, 1}, nullptr);
    callout.setDismissalMouseClicksAreAlwaysConsumed(true);
}
void TaskTree::editCheckpoint(arranger::TaskReference ref, juce::String cpId, juce::Point<int> position)
{
    if (flush().failed() || (before && !before())) return;
    const auto* found = checkpoint(model, {ref, cpId, false, 0});
    if (!found) return;
    const auto expected = *found;
    juce::Component::SafePointer<TaskTree> safe(this);
    auto panel = std::make_unique<TaskQuickProperties>(true, expected.name, expected.notes,
        statusId(expected.status), 3,
        [safe, ref, cpId, expected](juce::String name, juce::String notes, int status, int) -> juce::Result
        {
            if (!safe) return juce::Result::fail("Task list closed.");
            if (safe->flush().failed() || (safe->before && !safe->before()))
                return juce::Result::fail("Save pending edits first.");
            auto next = expected;
            next.name = name; next.notes = notes;
            next.status = statuses[juce::jlimit(1, 7, status) - 1];
            const auto result = safe->execute([&](arranger::WorkspaceCommands& cmd)
                { return cmd.updateCheckpoint(ref.taskId, expected, next); });
            if (result.wasOk()) { safe->rebuild(); if (safe->changed) safe->changed(); }
            return result;
        });
    auto& callout = juce::CallOutBox::launchAsynchronously(std::move(panel),
        {position.x, position.y, 1, 1}, nullptr);
    callout.setDismissalMouseClicksAreAlwaysConsumed(true);
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
    for (int i = 0; i < getNumRows(); ++i)
    {
        const auto& row = rows[static_cast<size_t>(i)];
        if (row.reference.taskId != editing->reference.taskId
            || row.reference.ownerPageId != editing->reference.ownerPageId
            || row.checkpointId != editing->checkpointId || row.addChild != editing->addChild) continue;
        const Columns columns(widths.name, widths.progress, widths.priority, widths.date, widths.notes, widths.source);
        auto bounds = list.getRowPosition(i, true).translated(list.getX(), list.getY());
        if (editing->notes)
        {
            const auto width = widths.notes - 12;
            const int chars = juce::jmax(12, width / 8);
            int lines = 0;
            for (const auto& line : juce::StringArray::fromLines(inlineEditor.getText()))
                lines += juce::jmax(1, (line.length() + chars - 1) / chars);
            bounds.setX(columns.notes + 5 - (list.getViewport() != nullptr ? list.getViewport()->getViewPositionX() : 0) + list.getX());
            bounds.setWidth(width);
            bounds.setHeight(juce::jlimit(38, 196, juce::jmax(1, lines) * 19 + 14));
        }
        else
        {
            const int nameX = columns.name + editing->depth * 14
                - (list.getViewport() != nullptr ? list.getViewport()->getViewPositionX() : 0);
            bounds.removeFromLeft(nameX);
            bounds.setWidth(juce::jmax(90, columns.progress - columns.name - editing->depth * 14 - 10));
        }
        inlineEditor.setBounds(bounds.reduced(1)); return;
    }
}
void TaskTree::listWasScrolled() { positionInline(); repaint(); }
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
        if (row.addChild)
        {
            juce::String id;
            if (arranger::WorkspaceQueries::generalTask(model, row.reference))
                return cmd.createSubtask(row.reference.taskId, value, id);
            return cmd.createTaskCheckpoint(row.reference, value, id);
        }
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


juce::String TaskTree::columnSettings() const
{
    return juce::String(widths.name) + "," + juce::String(widths.progress) + ","
        + juce::String(widths.priority) + "," + juce::String(widths.date) + ","
        + juce::String(widths.notes) + "," + juce::String(widths.source);
}
void TaskTree::resizeColumns()
{
    const Columns columns(widths.name, widths.progress, widths.priority, widths.date, widths.notes, widths.source);
    list.setMinimumContentWidth(columns.end);
    list.repaint(); repaint();
}
int TaskTree::headerDividerAt(int x) const
{
    const Columns columns(widths.name, widths.progress, widths.priority, widths.date, widths.notes, widths.source);
    const auto scroll = list.getViewport() ? list.getViewport()->getViewPositionX() : 0;
    const int boundaries[] = {columns.progress, columns.priority, columns.date, columns.notes, columns.source, columns.end};
    for (int i = 0; i < 6; ++i) if (std::abs(x + scroll - boundaries[i]) < 7) return i;
    return -1;
}
juce::var TaskTree::getDragSourceDescription(const juce::SparseSet<int>& selected)
{
    if (!manualOrder || selected.size() != 1 || editing) return {};
    const auto index = selected.getRange(0).getStart();
    if (index < 0 || index >= getNumRows()) return {};
    const auto row = rows[static_cast<size_t>(index)];
    if (row.addChild || row.notes || row.reference.songId.isNotEmpty()) return {};
    const auto* parent = arranger::WorkspaceQueries::generalTask(model, row.reference);
    if (!parent || arranger::WorkspaceQueries::taskArchived(model, parent->id)) return {};
    if (row.checkpointId.isNotEmpty())
        return "manager-checkpoint|" + parent->id + "|" + row.checkpointId;
    return "manager-task|" + parent->id + "|" + parent->ownerPageId + "|"
        + (parent->parentTaskId.isEmpty() ? juce::String("root") : parent->parentTaskId);
}
bool TaskTree::isInterestedInDragSource(const SourceDetails& source)
{
    if (!manualOrder) return false;
    return source.description.toString().startsWith("manager-task|")
        || source.description.toString().startsWith("manager-checkpoint|");
}
std::optional<TaskTree::Target> TaskTree::targetAt(juce::Point<int> point, bool cpDrag) const
{
    const auto local = list.getLocalPoint(this, point);
    if (!list.getLocalBounds().contains(local)) return {};
    const int index = list.getRowContainingPosition(local.x, local.y);
    if (index < 0 || index >= static_cast<int>(rows.size())) return {};
    const auto row = rows[static_cast<size_t>(index)];
    if (row.reference.songId.isNotEmpty()) return {};
    const auto* task = arranger::WorkspaceQueries::generalTask(model, row.reference);
    if (!task || arranger::WorkspaceQueries::taskArchived(model, task->id)) return {};
    if (cpDrag)
    {
        if (row.checkpointId.isEmpty() || row.addChild) return {};
    }
    else if (row.checkpointId.isNotEmpty()) return {};
    const auto position = list.getRowPosition(index, true);
    const int y = local.y - position.getY();
    Target result {row, false, y > position.getHeight() * 2 / 3, index};
    if (!cpDrag && (row.addChild || (y >= position.getHeight() / 3
        && y <= position.getHeight() * 2 / 3)))
        result.inside = true;
    return result;
}
void TaskTree::itemDragMove(const SourceDetails& source)
{
    const auto desc = source.description.toString();
    const auto isCheckpoint = desc.startsWith("manager-checkpoint|");
    const auto candidate = targetAt(source.localPosition, isCheckpoint);
    const auto fields = juce::StringArray::fromTokens(desc, "|", "");
    bool valid = candidate.has_value() && fields.size() >= 3;
    if (valid && isCheckpoint)
        valid = fields[1] == candidate->row.reference.taskId
            && fields[2] != candidate->row.checkpointId;
    else if (valid)
    {
        if (fields[1] == candidate->row.reference.taskId) valid = false;
        auto* parent = model.findTask(candidate->row.reference.taskId);
        std::set<juce::String> seen;
        while (valid && parent && seen.insert(parent->id).second)
        {
            if (parent->id == fields[1]) valid = false;
            parent = parent->parentTaskId.isNotEmpty() ? model.findTask(parent->parentTaskId) : nullptr;
        }
    }
    if (valid) activeDrop = candidate;
    else activeDrop.reset();
    repaint();
}
void TaskTree::itemDragExit(const SourceDetails&) { activeDrop.reset(); repaint(); }
void TaskTree::itemDropped(const SourceDetails& source)
{
    const auto target = targetAt(source.localPosition,
        source.description.toString().startsWith("manager-checkpoint|"));
    activeDrop.reset(); repaint();
    if (!target) return;
    commitDrop(source.description.toString(), *target);
}
void TaskTree::commitDrop(const juce::String& description, const Target& target)
{
    const auto args = juce::StringArray::fromTokens(description, "|", "");
    if (args.size() < 3) return;
    if (args[0] == "manager-checkpoint")
    {
        if (target.row.reference.taskId != args[1] || target.row.checkpointId.isEmpty()
            || target.row.checkpointId == args[2]) return;
        juce::String before = target.row.checkpointId;
        if (target.after)
            if (const auto* parent = model.findTask(args[1]))
            {
                before.clear(); bool found = false;
                for (const auto& cp : parent->checkpoints)
                {
                    if (cp.id == target.row.checkpointId) { found = true; continue; }
                    if (found && cp.id != args[2] && cp.archivedAt.isEmpty()) { before = cp.id; break; }
                }
            }
        apply([&](arranger::WorkspaceCommands& cmd) { return cmd.reorderCheckpoint(args[1], args[2], before); });
        return;
    }
    if (args.size() != 4 || args[0] != "manager-task") return;
    const auto* moving = model.findTask(args[1]);
    const auto* destination = model.findTask(target.row.reference.taskId);
    if (!moving || !destination || moving->id == destination->id) return;
    juce::String parent = target.inside ? destination->id : destination->parentTaskId;
    juce::String before = target.inside ? juce::String() : (target.after ? juce::String() : destination->id);
    if (target.after)
    {
        bool found = false;
        for (const auto& sibling : model.tasks)
        {
            if (sibling.id == destination->id) { found = true; continue; }
            if (found && sibling.id != moving->id && sibling.ownerPageId == destination->ownerPageId
                && sibling.parentTaskId == parent && !arranger::WorkspaceQueries::taskArchived(model, sibling.id))
            { before = sibling.id; break; }
        }
    }
    const auto owner = destination->ownerPageId;
    apply([&](arranger::WorkspaceCommands& cmd)
    { return cmd.moveTask(args[1], args[2], args[3] == "root" ? juce::String() : args[3],
        owner, parent, before); });
}
void TaskTree::paintOverChildren(juce::Graphics& g) { drawDropIndicator(g); }
void TaskTree::drawDropIndicator(juce::Graphics& g)
{
    if (!activeDrop) return;
    const auto target = *activeDrop;
    auto bounds = list.getRowPosition(target.index, true).translated(list.getX(), list.getY());
    if (bounds.getBottom() < 32 || bounds.getY() > getHeight()) return;
    g.setColour(mr::colour(mr::tokens::focus));
    if (target.inside) g.drawRoundedRectangle(bounds.reduced(3).toFloat(), 4.0f, 2.0f);
    else g.fillRect(bounds.getX() + 8, target.after ? bounds.getBottom() - 2 : bounds.getY() - 2,
        juce::jmin(bounds.getWidth() - 16, getWidth() - 20), 3);
}
