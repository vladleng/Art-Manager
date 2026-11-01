#include "TaskDetails.h"
namespace
{
juce::String tr(const char* value) { return juce::String::fromUTF8(value); }
const arranger::Status statuses[] = {arranger::Status::pool, arranger::Status::todo, arranger::Status::wip,
    arranger::Status::draft, arranger::Status::wait, arranger::Status::done, arranger::Status::blocked};
int statusId(arranger::Status value)
{
    for (int i = 0; i < 7; ++i) if (statuses[i] == value) return i + 1;
    return 2;
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
        field->setFont(juce::FontOptions(16.0f));
        field->onTextChange = [this] { if (!loading) { feedback.setText(tr("Изменения ожидают сохранения…"), juce::dontSendNotification); startTimer(700); } };
    }
    notes.setMultiLine(true); notes.setReturnKeyStartsNewLine(true);
    name.setTextToShowWhenEmpty(tr("Название задачи"), juce::Colours::grey);
    status.addItemList({"POOL", "TODO", "WIP", "DRAFT", "WAIT", "DONE", "BLOCKED"}, 1);
    priority.addItemList({"P1", "P2", "P3", "P4"}, 1);
    for (auto* combo : { &status, &priority })
    {
        addAndMakeVisible(combo); combo->onChange = [this] { if (!loading) { feedback.setText(tr("Изменения ожидают сохранения…"), juce::dontSendNotification); startTimer(700); } };
    }
    const char* titles[] = {"Сохранить", "Перечитать", "К странице", "Удалить в архив"};
    int i = 0;
    for (auto* button : { &save, &reloadProperties, &owner, &archiveButton })
    {
        addChildComponent(button); button->setButtonText(tr(titles[i++]));
    }
    for (auto* label : { &propertiesLabel, &noteLabel, &cpLabel, &feedback })
    {
        addAndMakeVisible(label); label->setColour(juce::Label::textColourId, juce::Colour(0xffb8c5d5));
    }
    propertiesLabel.setText(tr("Свойства задачи · правый клик — сохранить, перечитать, к странице, архив"), juce::dontSendNotification);
    propertiesLabel.setInterceptsMouseClicks(false, false);
    noteLabel.setInterceptsMouseClicks(false, false);
    noteLabel.setText("Notes", juce::dontSendNotification);
    cpLabel.setText(tr("Чек-поинты"), juce::dontSendNotification);
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
                    safe->baseline = task->properties; safe->showProperties(); safe->setStatus(juce::Result::ok());
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
    return { name.getText(), notes.getText(), statuses[juce::jlimit(1, 7, status.getSelectedId()) - 1],
        juce::jlimit(1, 4, priority.getSelectedId()) };
}
void TaskDetails::showProperties()
{
    loading = true;
    name.setText(baseline.name, false); notes.setText(baseline.notes, false);
    status.setSelectedId(statusId(baseline.status), juce::dontSendNotification);
    priority.setSelectedId(baseline.priority, juce::dontSendNotification);
    loading = false;
}
void TaskDetails::setStatus(const juce::Result& result)
{
    feedback.setText(result.failed() ? tr("Не сохранено: ") + result.getErrorMessage() : tr("Свойства сохранены"), juce::dontSendNotification);
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
    const auto saved = description.flush(); setStatus(saved); return saved;
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
        baseline = task->properties; showProperties();
    }
    else if (id.isNotEmpty())
    {
        const auto* task = model.findTask(id);
        if (!task) return juce::Result::fail("Task was not found.");
        if (fields() == baseline) { baseline = task->properties; showProperties(); }
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
void TaskDetails::resized()
{
    auto area = getLocalBounds();
    propertiesLabel.setBounds(area.removeFromTop(28));
    auto top = area.removeFromTop(34);
    priority.setBounds(top.removeFromRight(72).reduced(2));
    status.setBounds(top.removeFromRight(110).reduced(2));
    name.setBounds(top.reduced(2));
    auto noteRow = area.removeFromTop(62);
    noteLabel.setBounds(noteRow.removeFromLeft(48)); notes.setBounds(noteRow.reduced(2));
    feedback.setBounds(area.removeFromBottom(26));
    area.removeFromTop(8);
    cpLabel.setBounds(area.removeFromTop(28));
    checkpoints.setBounds(area.removeFromTop(juce::jmin(220, area.getHeight() / 2)));
    area.removeFromTop(12); description.setBounds(area);
}
void TaskDetails::refreshCheckpoints()
{
    std::vector<arranger::TaskReference> refs;
    if (const auto* task = model.findTask(taskId))
    {
        const auto progress = arranger::WorkspaceQueries::taskProgress(*task);
        cpLabel.setText(tr("Чек-поинты: ") + juce::String(progress.first) + "/" + juce::String(progress.second)
            + tr(" · правый клик — действия задачи и добавление чек-поинта"), juce::dontSendNotification);
        refs.push_back({{}, task->ownerPageId, taskId});
    }
    checkpoints.setTasks(std::move(refs), true);
}
void TaskDetails::mouseDown(const juce::MouseEvent& event)
{
    if (!event.mods.isPopupMenu()) return;
    if (event.y >= cpLabel.getY())
    {
        if (const auto* task = model.findTask(taskId))
            checkpoints.showTaskMenu({{}, task->ownerPageId, taskId}, event.getScreenPosition());
        return;
    }
    juce::PopupMenu menu;
    menu.addItem(1, tr("Сохранить")); menu.addItem(2, tr("Перечитать свойства"));
    menu.addItem(3, tr("К странице")); menu.addSeparator(); menu.addItem(4, tr("Архивировать задачу"));
    juce::Component::SafePointer<TaskDetails> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this), [safe](int item)
    {
        if (safe == nullptr) return;
        if (item == 1) safe->save.onClick(); else if (item == 2) safe->reloadProperties.onClick();
        else if (item == 3) safe->owner.onClick(); else if (item == 4) safe->archiveButton.onClick();
    });
}
