#include "TaskDetails.h"
#include "TaskPlanEditor.h"
#include "TaskProgressMeter.h"
#include "MRManagerStyle.h"
namespace
{
juce::String tr(const char* value) { return juce::String::fromUTF8(value); }
const arranger::Status statuses[] = {arranger::Status::pool, arranger::Status::todo, arranger::Status::wip,
    arranger::Status::draft, arranger::Status::wait, arranger::Status::done, arranger::Status::blocked};
const char* const statusNames[] = {"POOL", "TODO", "WIP", "DRAFT", "WAIT", "DONE", "BLOCKED"};
int statusId(arranger::Status value)
{
    for (int i = 0; i < 7; ++i) if (statuses[i] == value) return i + 1;
    return 2;
}
int statusIndex(arranger::Status value)
{
    for (int i = 0; i < 7; ++i) if (statuses[i] == value) return i;
    return 1;
}
}
TaskDetails::TaskDetails(arranger::WorkspaceModel& data, Execute run,
    std::function<void(juce::String)> goBack, std::function<void(juce::String)> archive,
    std::function<void(juce::String)> notifyChanged)
    : model(data), execute(std::move(run)), back(std::move(goBack)), archiveTask(std::move(archive)), changed(std::move(notifyChanged)),
      checkpoints(model, execute, [this] { return flush().wasOk(); }, {},
          [this] { bind(taskId); if (changed) changed(taskId); },
          [this](juce::String id) { if (flush().wasOk()) archiveTask(id); })
{
    for (auto* field : { &name, &notes })
    {
        addAndMakeVisible(field);
        field->onTextChange = [this] { if (!loading) { feedback.setText(tr("Изменения ожидают сохранения…"), juce::dontSendNotification); startTimer(700); } };
    }
    name.setFont(juce::FontOptions(26.0f, juce::Font::bold));
    name.setColour(juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
    name.setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    name.setColour(juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
    name.setColour(juce::TextEditor::textColourId, mr::colour(mr::tokens::bright));
    name.setJustification(juce::Justification::centredLeft);
    notes.setMultiLine(true); notes.setReturnKeyStartsNewLine(true);
    notes.setFont(juce::FontOptions(13.5f));
    name.setTextToShowWhenEmpty(tr("Название задачи"), mr::colour(mr::tokens::muted));
    status.addItemList({"POOL", "TODO", "WIP", "DRAFT", "WAIT", "DONE", "BLOCKED"}, 1);
    priority.addItemList({"P1", "P2", "P3", "P4"}, 1);
    for (auto* combo : { &status, &priority })
    {
        addChildComponent(combo); combo->setVisible(false);
        combo->onChange = [this] { if (!loading) { feedback.setText(tr("Изменения ожидают сохранения…"), juce::dontSendNotification); startTimer(700); } };
    }
    const char* titles[] = {"Сохранить", "Перечитать", "К странице", "Удалить в архив"};
    int i = 0;
    for (auto* button : { &save, &reloadProperties, &owner, &archiveButton })
    {
        addChildComponent(button); button->setButtonText(tr(titles[i++]));
    }
    owner.setButtonText(juce::String::fromUTF8("←"));
    owner.setTooltip(tr("К странице"));
    owner.setVisible(true);
    for (auto* label : { &propertiesLabel, &noteLabel, &cpLabel, &feedback })
    {
        addAndMakeVisible(label); label->setColour(juce::Label::textColourId, mr::colour(mr::tokens::meta));
    }
    propertiesLabel.setText(tr("Описание"), juce::dontSendNotification);
    propertiesLabel.setInterceptsMouseClicks(false, false);
    noteLabel.setInterceptsMouseClicks(false, false);
    noteLabel.setText(tr("Текст"), juce::dontSendNotification);
    cpLabel.setText(tr("Подзадачи"), juce::dontSendNotification);
    addAndMakeVisible(checkpoints);
    cpLabel.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(description);
    save.onClick = [this] { flush(); };
    reloadProperties.onClick = [this]
    {
        stopTimer();
        juce::Component::SafePointer<TaskDetails> safe(this);
        juce::AlertWindow::showOkCancelBox(juce::MessageBoxIconType::QuestionIcon,
            tr("Перечитать свойства"), tr("Отбросить несохранённое название, статус, приоритет и Notes? Описание перечитывается отдельно."),
            tr("Перечитать"), tr("Отмена"), this, juce::ModalCallbackFunction::create([safe](int choice)
            {
                if (safe == nullptr) return;
                if (choice != 1) { safe->startTimer(700); return; }
                if (const auto* task = safe->model.findTask(safe->taskId))
                {
                    safe->baseline = task->properties; safe->planBaseline = task->plan;
                    safe->showProperties(); safe->setStatus(juce::Result::ok());
                }
            }));
    };
    owner.onClick = [this]
    {
        if (flush().failed()) return;
        if (const auto* task = model.findTask(taskId)) back(task->ownerPageId);
    };
    archiveButton.onClick = [this] { if (flush().wasOk()) archiveTask(taskId); };
}
arranger::TaskProperties TaskDetails::fields() const
{
    const auto priorityId = priority.getSelectedId();
    return { name.getText(), notes.getText(), statuses[juce::jlimit(1, 7, status.getSelectedId()) - 1],
        priorityId <= 0 ? 0 : juce::jlimit(1, 4, priorityId) };
}
void TaskDetails::showProperties()
{
    loading = true;
    name.setText(baseline.name, false); notes.setText(baseline.notes, false);
    status.setSelectedId(statusId(baseline.status), juce::dontSendNotification);
    priority.setSelectedId(baseline.priority, juce::dontSendNotification);
    loading = false;
    repaint();
}
void TaskDetails::setStatus(const juce::Result& result)
{
    feedback.setText(result.failed() ? tr("Не сохранено: ") + result.getErrorMessage() : tr("Свойства сохранены"), juce::dontSendNotification);
}
juce::String TaskDetails::statusText() const
{
    const auto* task = model.findTask(taskId);
    return task == nullptr ? juce::String() : tr(statusNames[statusIndex(task->properties.status)]);
}
juce::String TaskDetails::priorityText() const
{
    const auto* task = model.findTask(taskId);
    if (task == nullptr || task->properties.priority <= 0) return tr("Нет");
    return "P" + juce::String(task->properties.priority);
}
juce::String TaskDetails::planText() const
{
    const auto* task = model.findTask(taskId);
    if (task == nullptr) return {};
    const auto& plan = task->plan;
    if (plan.start.isEmpty())
        return plan.deadline.isNotEmpty() ? tr("Дедлайн ") + plan.deadline.substring(0, 10) : tr("Пусто");
    auto stamp = [](const juce::String& value)
    {
        return value.length() > 10 ? value.substring(0, 10) + " " + value.substring(11, 16)
                                   : value.substring(0, 10);
    };
    auto text = stamp(plan.start);
    if (plan.end.isNotEmpty() && plan.end != plan.start)
        text += juce::String::fromUTF8(" – ") + stamp(plan.end);
    return text;
}
juce::String TaskDetails::progressText() const
{
    const auto* task = model.findTask(taskId);
    if (task == nullptr) return {};
    const auto progress = arranger::WorkspaceQueries::taskProgress(*task);
    return juce::String(progress.first) + "/" + juce::String(progress.second);
}
void TaskDetails::showStatusMenu()
{
    if (const auto* task = model.findTask(taskId))
    {
        juce::PopupMenu menu; menu.setLookAndFeel(&getLookAndFeel());
        for (int index = 0; index < 7; ++index)
            menu.addItem(index + 1, tr(statusNames[index]), true,
                task->properties.status == statuses[index]);
        juce::Component::SafePointer<TaskDetails> safe(this);
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this),
            [safe](int selected)
            {
                if (safe == nullptr || selected <= 0) return;
                safe->status.setSelectedId(selected, juce::sendNotification);
                safe->flush();
            });
    }
}
void TaskDetails::showPriorityMenu()
{
    if (const auto* task = model.findTask(taskId))
    {
        juce::PopupMenu menu; menu.setLookAndFeel(&getLookAndFeel());
        for (int value = 1; value <= 4; ++value)
            menu.addItem(value, "P" + juce::String(value), true, task->properties.priority == value);
        menu.addSeparator();
        menu.addItem(9, tr("Без приоритета"), true, task->properties.priority <= 0);
        juce::Component::SafePointer<TaskDetails> safe(this);
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this),
            [safe](int selected)
            {
                if (safe == nullptr || selected <= 0) return;
                safe->priority.setSelectedId(selected <= 4 ? selected : 0, juce::sendNotification);
                safe->flush();
            });
    }
}
void TaskDetails::openDateEditor()
{
    const auto* task = model.findTask(taskId);
    if (task == nullptr) return;
    const auto plan = task->plan;
    planBaseline = plan;
    const auto today = arranger::workspaceToday(juce::Time::getCurrentTime().toMilliseconds());
    juce::Component::SafePointer<TaskDetails> safe(this);
    auto editor = std::make_unique<TaskPlanEditor>(plan, today,
        [safe, plan](arranger::TaskPlan next) -> juce::Result
        {
            auto* self = safe.getComponent();
            if (self == nullptr) return juce::Result::fail("Task view closed.");
            const auto result = self->execute([&](arranger::WorkspaceCommands& cmd)
                { return cmd.updateTaskPlan(self->taskId, plan, next); });
            if (result.wasOk())
            {
                self->planBaseline = next;
                if (self->changed) self->changed(self->taskId);
                self->repaint();
            }
            return result;
        });
    juce::CallOutBox::launchAsynchronously(std::move(editor), dateRowBounds(), this);
}

juce::Rectangle<int> TaskDetails::dateRowBounds() const
{
    for (const auto& row : rows)
        if (row.kind == RowKind::date) return row.bounds;
    return getLocalBounds();
}
juce::Result TaskDetails::flush()
{
    stopTimer();
    if (taskId.isNotEmpty())
    {
        auto values = fields(); values.name = values.name.trim();
        if (values != baseline)
        {
            const auto id = taskId;
            const auto result = execute([&](arranger::WorkspaceCommands& cmd) { return cmd.updateTask(id, baseline, values); });
            setStatus(result);
            if (result.failed()) return result;
            baseline = values;
            if (changed) changed(id);
        }
    }
    auto saved = description.flush(); if (saved.wasOk()) saved = checkpoints.flush(); setStatus(saved); return saved;
}
juce::Result TaskDetails::bind(const juce::String& id)
{
    if (id != taskId)
    {
        const auto saved = flush(); if (saved.failed()) return saved;
        if (id.isEmpty()) { taskId.clear(); checkpoints.setTasks({}, true); return description.bind({}, {}, {}); }
        const auto* task = model.findTask(id);
        if (!task || arranger::WorkspaceQueries::taskArchived(model, id)) return juce::Result::fail("Task is not editable.");
        taskId = id;
        baseline = task->properties; planBaseline = task->plan; showProperties();
    }
    else if (id.isNotEmpty())
    {
        const auto* task = model.findTask(id);
        if (!task) return juce::Result::fail("Task was not found.");
        if (fields() == baseline) { baseline = task->properties; planBaseline = task->plan; showProperties(); }
        else if (task->properties != baseline) return juce::Result::fail("Task properties changed while edits were pending.");
    }
    if (id.isEmpty()) return juce::Result::ok();
    const auto result = description.bind("task:" + id, [this, id]() -> std::optional<arranger::BlockEditorSession::Blocks>
    {
        const auto* task = model.findTask(id);
        if (!task || arranger::WorkspaceQueries::taskArchived(model, id)) return {};
        return task->blocks;
    }, [this, id](const auto& expected, const auto& blocks)
    {
        return execute([&](arranger::WorkspaceCommands& cmd) { return cmd.replaceTaskBlocks(id, expected, blocks); });
    });
    refreshCheckpoints(); return result;
}
void TaskDetails::timerCallback() { flush(); }
void TaskDetails::paint(juce::Graphics& g)
{
    g.fillAll(mr::colour(mr::tokens::canvas));
    // A quiet rule under the title keeps the page readable without a card frame.
    g.setColour(mr::colour(mr::tokens::outline).withAlpha(0.55f));
    g.drawHorizontalLine(titleBounds.getBottom() + 4, 24.0f, static_cast<float>(getWidth() - 24));
    auto label = [&](const Row& row, const juce::String& text)
    {
        g.setColour(mr::colour(mr::tokens::muted));
        g.setFont(juce::FontOptions(12.5f));
        g.drawText(text, row.bounds.getX() + 30, row.bounds.getY(), 150, row.bounds.getHeight(),
            juce::Justification::centredLeft);
    };
    auto icon = [&](const Row& row, const juce::String& glyph, juce::Colour colour)
    {
        g.setColour(colour);
        g.setFont(juce::FontOptions(13.0f, juce::Font::bold));
        g.drawText(glyph, row.bounds.getX() + 2, row.bounds.getY(), 22, row.bounds.getHeight(),
            juce::Justification::centred);
    };
    for (const auto& row : rows)
    {
        switch (row.kind)
        {
            case RowKind::status:
            {
                const auto* task = model.findTask(taskId);
                const auto status = task == nullptr ? arranger::Status::todo : task->properties.status;
                icon(row, juce::String::fromUTF8("●"), mr::statusInk(status));
                label(row, tr("Статус"));
                const auto value = statusText();
                const auto chip = juce::Rectangle<int>(row.bounds.getX() + 170, row.bounds.getCentreY() - 11,
                    130, 22);
                g.setColour(mr::statusFill(status));
                g.fillRoundedRectangle(chip.toFloat(), 11.0f);
                g.setColour(mr::statusInk(status));
                g.setFont(juce::FontOptions(11.5f, juce::Font::bold));
                g.drawText(value, chip, juce::Justification::centred);
                break;
            }
            case RowKind::date:
                icon(row, juce::String::fromUTF8("▦"), mr::colour(mr::tokens::meta));
                label(row, tr("Дата"));
                g.setColour(mr::colour(mr::tokens::bright));
                g.setFont(juce::FontOptions(12.5f));
                g.drawText(planText(), row.bounds.getX() + 170, row.bounds.getY(),
                    row.bounds.getWidth() - 175, row.bounds.getHeight(), juce::Justification::centredLeft);
                break;
            case RowKind::priority:
                icon(row, juce::String::fromUTF8("!"), mr::colour(mr::tokens::meta));
                label(row, tr("Приоритет"));
                g.setColour(mr::colour(mr::tokens::bright));
                g.setFont(juce::FontOptions(12.5f));
                g.drawText(priorityText(), row.bounds.getX() + 170, row.bounds.getY(),
                    160, row.bounds.getHeight(), juce::Justification::centredLeft);
                break;
            case RowKind::page:
            {
                icon(row, juce::String::fromUTF8("▤"), mr::colour(mr::tokens::meta));
                label(row, tr("Страница"));
                const auto* task = model.findTask(taskId);
                const auto page = task == nullptr ? juce::String()
                    : arranger::WorkspaceQueries::breadcrumb(model, task->ownerPageId);
                g.setColour(mr::colour(mr::tokens::meta));
                g.setFont(juce::FontOptions(12.5f));
                g.drawText(page, row.bounds.getX() + 170, row.bounds.getY(),
                    row.bounds.getWidth() - 175, row.bounds.getHeight(), juce::Justification::centredLeft);
                break;
            }
            case RowKind::progress:
                icon(row, juce::String::fromUTF8("▰"), mr::colour(mr::tokens::meta));
                label(row, tr("Прогресс"));
                g.setColour(mr::colour(mr::tokens::meta));
                g.setFont(juce::FontOptions(12.5f));
                g.drawText(progressText(), row.bounds.getX() + 170, row.bounds.getY(),
                    120, row.bounds.getHeight(), juce::Justification::centredLeft);
                if (const auto* task = model.findTask(taskId))
                {
                    const auto progress = arranger::WorkspaceQueries::taskProgress(*task);
                    mr::paintTaskProgressMeter(g, static_cast<float>(row.bounds.getX() + 240),
                        static_cast<float>(row.bounds.getCentreY() - 5),
                        progress.second > 0 ? progress.first * 100 / progress.second : 0);
                }
                break;
        }
    }
}
void TaskDetails::resized()
{
    auto area = getLocalBounds().reduced(24);
    owner.setBounds(area.removeFromTop(30).removeFromLeft(30).reduced(1));
    area.removeFromTop(2);
    titleBounds = area.removeFromTop(48);
    name.setBounds(titleBounds);
    area.removeFromTop(10);
    rows.clear();
    for (const auto kind : { RowKind::status, RowKind::date, RowKind::priority,
                             RowKind::page, RowKind::progress })
        rows.push_back({ kind, area.removeFromTop(32) });
    area.removeFromTop(10);
    notesHeader = area.removeFromTop(22);
    noteLabel.setBounds(notesHeader);
    notes.setBounds(area.removeFromTop(74));
    area.removeFromTop(12);
    cpHeader = area.removeFromTop(22);
    cpLabel.setBounds(cpHeader);
    checkpoints.setBounds(area.removeFromTop(juce::jmin(190, area.getHeight() / 2)));
    area.removeFromTop(12);
    descHeader = area.removeFromTop(22);
    propertiesLabel.setBounds(descHeader);
    description.setBounds(area.removeFromTop(juce::jmax(120, area.getHeight() - 26)));
    feedback.setBounds(getLocalBounds().removeFromBottom(22).reduced(24, 0));
}
void TaskDetails::refreshCheckpoints()
{
    std::vector<arranger::TaskReference> refs;
    if (const auto* task = model.findTask(taskId))
    {
        const auto progress = arranger::WorkspaceQueries::taskProgress(*task);
        cpLabel.setText(tr("Подзадачи  ") + juce::String(progress.first) + "/" + juce::String(progress.second),
            juce::dontSendNotification);
        refs.push_back({{}, task->ownerPageId, taskId});
    }
    checkpoints.setTasks(std::move(refs), true);
}
void TaskDetails::mouseDown(const juce::MouseEvent& event)
{
    if (event.mods.isPopupMenu())
    {
        juce::PopupMenu menu; menu.setLookAndFeel(&getLookAndFeel());
        menu.addItem(1, tr("Сохранить")); menu.addItem(2, tr("Перечитать свойства"));
        menu.addItem(3, tr("К странице")); menu.addSeparator(); menu.addItem(4, tr("Архивировать задачу"));
        juce::Component::SafePointer<TaskDetails> safe(this);
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this), [safe](int item)
        {
            if (safe == nullptr) return;
            if (item == 1) safe->save.onClick(); else if (item == 2) safe->reloadProperties.onClick();
            else if (item == 3) safe->owner.onClick(); else if (item == 4) safe->archiveButton.onClick();
        });
        return;
    }
    for (const auto& row : rows)
    {
        if (!row.bounds.contains(event.getPosition())) continue;
        switch (row.kind)
        {
            case RowKind::status: showStatusMenu(); break;
            case RowKind::date: openDateEditor(); break;
            case RowKind::priority: showPriorityMenu(); break;
            case RowKind::page:
                if (flush().wasOk())
                    if (const auto* task = model.findTask(taskId)) back(task->ownerPageId);
                break;
            case RowKind::progress: break;
        }
        return;
    }
}
