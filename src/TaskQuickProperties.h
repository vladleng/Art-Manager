#pragma once
#include "MRManagerStyle.h"

// Compact Notion-like properties panel, anchored to the clicked task row.
// Save is explicit; validation and write failures preserve all draft fields.
class TaskQuickProperties final : public juce::Component
{
public:
    using Save = std::function<juce::Result(juce::String, juce::String, int, int)>;

    TaskQuickProperties(bool checkpoint, juce::String taskName, juce::String taskNotes,
        int statusId, int priorityId, Save save)
        : isCheckpoint(checkpoint), persist(std::move(save))
    {
        setLookAndFeel(&theme);
        setSize(440, 340);
        heading.setText(checkpoint ? tr("Свойства подзадачи") : tr("Свойства задачи"),
            juce::dontSendNotification);
        heading.setFont(juce::FontOptions(15.0f, juce::Font::bold));
        nameLabel.setText(tr("Название"), juce::dontSendNotification);
        statusLabel.setText(tr("Статус"), juce::dontSendNotification);
        priorityLabel.setText(tr("Приоритет"), juce::dontSendNotification);
        notesLabel.setText("Notes", juce::dontSendNotification);
        for (auto* label : {&nameLabel, &statusLabel, &priorityLabel, &notesLabel, &error})
            label->setColour(juce::Label::textColourId, mr::colour(mr::tokens::meta));

        name.setText(taskName, false); name.setSelectAllWhenFocused(true);
        notes.setMultiLine(true); notes.setReturnKeyStartsNewLine(true);
        notes.setScrollbarsShown(true); notes.setText(taskNotes, false);
        status.addItemList({"POOL", "TODO", "WIP", "DRAFT", "WAIT", "DONE", "BLOCKED"}, 1);
        status.setSelectedId(juce::jlimit(1, 7, statusId), juce::dontSendNotification);
        priority.addItemList({"P1", "P2", "P3", "P4"}, 1);
        priority.setSelectedId(juce::jlimit(1, 4, priorityId), juce::dontSendNotification);
        accept.setButtonText(tr("Сохранить"));
        cancel.setButtonText(tr("Отмена"));
        for (auto* child : std::initializer_list<juce::Component*>{&heading, &nameLabel, &name,
                            &statusLabel, &status, &notesLabel, &notes, &accept, &cancel, &error})
            addAndMakeVisible(child);
        if (!isCheckpoint)
        {
            addAndMakeVisible(priorityLabel);
            addAndMakeVisible(priority);
        }
        accept.onClick = [this] { commit(); };
        cancel.onClick = [this] { close(); };
        name.onReturnKey = [this] { commit(); };
        name.onEscapeKey = [this] { close(); };
        notes.onEscapeKey = [this] { close(); };
    }

    ~TaskQuickProperties() override { setLookAndFeel(nullptr); }
    void paint(juce::Graphics& g) override { g.fillAll(mr::colour(mr::tokens::panel)); }
    void resized() override
    {
        auto b = getLocalBounds().reduced(14);
        heading.setBounds(b.removeFromTop(24)); b.removeFromTop(6);
        nameLabel.setBounds(b.removeFromTop(18));
        name.setBounds(b.removeFromTop(30)); b.removeFromTop(8);
        auto labels = b.removeFromTop(18);
        auto controls = b.removeFromTop(30); b.removeFromTop(8);
        if (isCheckpoint)
        {
            statusLabel.setBounds(labels);
            status.setBounds(controls);
        }
        else
        {
            statusLabel.setBounds(labels.removeFromLeft(labels.getWidth() / 2));
            priorityLabel.setBounds(labels.reduced(4, 0));
            status.setBounds(controls.removeFromLeft(controls.getWidth() / 2).withTrimmedRight(6));
            priority.setBounds(controls.withTrimmedLeft(4));
        }
        notesLabel.setBounds(b.removeFromTop(18));
        auto footer = b.removeFromBottom(32);
        accept.setBounds(footer.removeFromRight(108));
        footer.removeFromRight(6);
        cancel.setBounds(footer.removeFromRight(94));
        error.setBounds(b.removeFromBottom(24));
        notes.setBounds(b.reduced(0, 4));
    }

private:
    static juce::String tr(const char* value) { return juce::String::fromUTF8(value); }
    void commit()
    {
        const auto result = persist(name.getText().trim(), notes.getText(),
            status.getSelectedId(), priority.getSelectedId());
        if (result.failed()) { error.setText(result.getErrorMessage(), juce::dontSendNotification); return; }
        close();
    }
    void close()
    {
        if (auto* callout = findParentComponentOfClass<juce::CallOutBox>()) callout->dismiss();
    }
    bool isCheckpoint;
    Save persist;
    mr::ManagerTheme theme;
    juce::Label heading, nameLabel, statusLabel, priorityLabel, notesLabel, error;
    juce::TextEditor name, notes;
    juce::ComboBox status, priority;
    juce::TextButton accept, cancel;
};
