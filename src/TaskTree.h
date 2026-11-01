#pragma once
#include <JuceHeader.h>
#include "WorkspaceCommands.h"
#include "TaskTreeRows.h"
#include "MRManagerStyle.h"

class TaskTree final : public juce::Component, private juce::ListBoxModel, private juce::KeyListener
{
public:
    using Execute = std::function<juce::Result(const arranger::WorkspaceAction&)>;
    TaskTree(arranger::WorkspaceModel&, Execute, std::function<bool()> before,
        std::function<void(arranger::TaskReference)> open, std::function<void()> changed,
        std::function<void(juce::String)> archive, std::function<void()> create = {});
    juce::Result flush();
    void setTasks(std::vector<arranger::TaskReference>, bool checkpointsOnly = false);
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void showTaskMenu(const arranger::TaskReference&, juce::Point<int>);
private:
    int getNumRows() override;
    void paintListBoxItem(int, juce::Graphics&, int, int, bool) override;
    void listBoxItemClicked(int, const juce::MouseEvent&) override;
    void listBoxItemDoubleClicked(int, const juce::MouseEvent&) override;
    void backgroundClicked(const juce::MouseEvent&) override;
    void returnKeyPressed(int) override;
    void listWasScrolled() override;
    bool keyPressed(const juce::KeyPress&, juce::Component*) override;
    void beginInline(arranger::TaskTreeRow);
    void positionInline();
    void cancelInline(bool refocus = true);
    void statusMenu(arranger::TaskTreeRow, juce::Point<int>);
    void rebuild();
    void toggle(int);
    void showMenu(arranger::TaskTreeRow, juce::Point<int>);
    void showBackgroundMenu(juce::Point<int>);
    void editTask(arranger::TaskReference);
    void editCheckpoint(arranger::TaskReference, juce::String checkpointId = {});
    void apply(const arranger::WorkspaceAction&);
    arranger::WorkspaceModel& model;
    Execute execute;
    std::function<bool()> before;
    std::function<void(arranger::TaskReference)> open;
    std::function<void()> changed, create;
    std::function<void(juce::String)> archive;
    std::vector<arranger::TaskReference> references;
    std::vector<arranger::TaskTreeRow> rows;
    std::set<juce::String> expanded;
    bool checkpointsOnly = false;
    mr::Icons icons;
    juce::TextEditor inlineEditor;
    std::optional<arranger::TaskTreeRow> editing;
    juce::String expectedText;
    bool committing = false;
    juce::ListBox list {"Tasks and checkpoints", this};
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TaskTree)
};
