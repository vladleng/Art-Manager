#pragma once
#include <JuceHeader.h>
#include "WorkspaceCommands.h"
#include "BlockEditor.h"
#include "TaskTree.h"

class TaskDetails final : public juce::Component, private juce::Timer
{
public:
    using Execute = std::function<juce::Result(const arranger::WorkspaceAction&)>;
    TaskDetails(arranger::WorkspaceModel&, Execute, std::function<void(juce::String)> back,
        std::function<void(juce::String)> archive, std::function<void(juce::String)> changed);
    juce::Result bind(const juce::String&);
    juce::Result flush();
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
private:
    void timerCallback() override;
    void refreshCheckpoints();
    void setStatus(const juce::Result&);
    arranger::TaskProperties fields() const;
    void showProperties();

    arranger::WorkspaceModel& model;
    Execute execute;
    std::function<void(juce::String)> back, archiveTask, changed;
    juce::String taskId;
    arranger::TaskProperties baseline;
    bool loading = false;
    juce::TextEditor name, notes;
    juce::ComboBox status, priority;
    juce::TextButton save, reloadProperties, owner, archiveButton;
    juce::Label propertiesLabel, noteLabel, cpLabel, feedback;
    TaskTree checkpoints;
    BlockEditor description;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TaskDetails)
};

