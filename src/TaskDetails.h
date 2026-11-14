#pragma once
#include <JuceHeader.h>
#include "WorkspaceCommands.h"
#include "BlockEditor.h"
#include "TaskTree.h"
#include <vector>

// #39/0.4.2: Notion-like task page. Properties are rendered as clickable rows
// (status, date, priority, page, progress) instead of a form of combo boxes.
class TaskDetails final : public juce::Component, private juce::Timer
{
public:
    using Execute = std::function<juce::Result(const arranger::WorkspaceAction&)>;
    TaskDetails(arranger::WorkspaceModel&, Execute, std::function<void()> goBack,
        std::function<void(juce::String)> openPage, std::function<void(juce::String)> archive,
        std::function<void(juce::String)> changed);
    juce::Result bind(const juce::String&);
    juce::Result flush();
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
private:
    enum class RowKind { status, date, priority, page, progress };
    struct Row
    {
        RowKind kind = RowKind::status;
        juce::Rectangle<int> bounds;
    };

    void timerCallback() override;
    void refreshCheckpoints();
    void setStatus(const juce::Result&);
    arranger::TaskProperties fields() const;
    void showProperties();
    void showStatusMenu(juce::Rectangle<int> anchor);
    void showPriorityMenu(juce::Rectangle<int> anchor);
    void openDateEditor();
    juce::Rectangle<int> valueBounds(const Row&) const;
    juce::String statusText() const;
    juce::String priorityText() const;
    juce::String planText() const;
    juce::String progressText() const;
    juce::Rectangle<int> dateRowBounds() const;

    arranger::WorkspaceModel& model;
    Execute execute;
    std::function<void()> back;
    std::function<void(juce::String)> openPage, archiveTask, changed;
    juce::String taskId;
    arranger::TaskProperties baseline;
    arranger::TaskPlan planBaseline;
    bool loading = false;
    std::vector<Row> rows;
    juce::Rectangle<int> titleBounds, notesHeader, cpHeader, descHeader;
    juce::TextEditor name, notes;
    juce::ComboBox status, priority;
    juce::TextButton save, reloadProperties, owner, archiveButton;
    juce::Label propertiesLabel, noteLabel, cpLabel, feedback;
    TaskTree checkpoints;
    BlockEditor description;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TaskDetails)
};
