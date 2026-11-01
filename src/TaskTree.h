#pragma once
#include <JuceHeader.h>
#include "WorkspaceCommands.h"
#include "TaskTreeRows.h"
#include "MRManagerStyle.h"

class TaskTree final : public juce::Component, private juce::ListBoxModel, private juce::KeyListener, public juce::DragAndDropTarget
{
public:
    using Execute = std::function<juce::Result(const arranger::WorkspaceAction&)>;
    TaskTree(arranger::WorkspaceModel&, Execute, std::function<bool()> before,
        std::function<void(arranger::TaskReference)> open, std::function<void()> changed,
        std::function<void(juce::String)> archive, std::function<void()> create = {},
        juce::String savedColumns = {}, std::function<void(juce::String)> persistColumns = {});
    juce::Result flush();
    void setTasks(std::vector<arranger::TaskReference>, bool checkpointsOnly = false);
    void setManualOrder(bool enabled) { manualOrder = enabled; }
    void paint(juce::Graphics&) override;
    void paintOverChildren(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    bool isInterestedInDragSource(const SourceDetails&) override;
    void itemDragMove(const SourceDetails&) override;
    void itemDragExit(const SourceDetails&) override;
    void itemDropped(const SourceDetails&) override;
    juce::String columnSettings() const;
    void showTaskMenu(const arranger::TaskReference&, juce::Point<int>);
private:
    int getNumRows() override;
    void paintListBoxItem(int, juce::Graphics&, int, int, bool) override;
    void listBoxItemClicked(int, const juce::MouseEvent&) override;
    void listBoxItemDoubleClicked(int, const juce::MouseEvent&) override;
    void backgroundClicked(const juce::MouseEvent&) override;
    void returnKeyPressed(int) override;
    void listWasScrolled() override;
    juce::var getDragSourceDescription(const juce::SparseSet<int>&) override;
    bool mayDragToExternalWindows() const override { return false; }
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
    struct Widths { int name = 350, progress = 145, priority = 100, notes = 320, source = 190; } widths;
    struct Target { arranger::TaskTreeRow row; bool inside = false; bool after = false; int index = -1; };
    std::optional<Target> targetAt(juce::Point<int> point, bool isCheckpoint) const;
    void commitDrop(const juce::String& description, const Target& target);
    void drawDropIndicator(juce::Graphics&);
    void resizeColumns();
    int headerDividerAt(int x) const;
    std::optional<Target> activeDrop;
    int resizingColumn = -1, resizeStartX = 0, resizeStartWidth = 0;
    std::function<void(juce::String)> persistColumns;
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
    bool manualOrder = true;
    mr::Icons icons;
    juce::TextEditor inlineEditor;
    std::optional<arranger::TaskTreeRow> editing;
    juce::String expectedText;
    bool committing = false;
    juce::ListBox list {"Tasks and checkpoints", this};
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TaskTree)
};
