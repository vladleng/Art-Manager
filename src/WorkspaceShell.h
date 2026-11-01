#pragma once
#include <JuceHeader.h>
#include "HubEditor.h"
#include "WorkspaceCommands.h"
#include "BlockEditor.h"
#include "TaskDetails.h"

class WorkspaceShell final : public juce::Component
{
public:
    using Execute = std::function<juce::Result(const arranger::WorkspaceAction&)>;
    WorkspaceShell(arranger::WorkspaceModel&, std::unique_ptr<HubEditor>, Execute,
        juce::String selectedView, std::function<void(juce::String)> saveView);
    void paint(juce::Graphics&) override;
    void resized() override;
    void refresh();
    bool flushEdits();

private:
    struct NavRow { juce::String key, title; int depth = 0; };
    struct NavModel final : juce::ListBoxModel
    {
        explicit NavModel(WorkspaceShell& value) : owner(value) {}
        int getNumRows() override;
        void paintListBoxItem(int, juce::Graphics&, int, int, bool) override;
        void selectedRowsChanged(int) override;
        void listBoxItemClicked(int, const juce::MouseEvent&) override;
        void listBoxItemDoubleClicked(int, const juce::MouseEvent&) override;
        WorkspaceShell& owner;
    };
    struct ArchiveModel final : juce::ListBoxModel
    {
        explicit ArchiveModel(WorkspaceShell& value) : owner(value) {}
        int getNumRows() override;
        void paintListBoxItem(int, juce::Graphics&, int, int, bool) override;
        void listBoxItemClicked(int, const juce::MouseEvent&) override;
        void listBoxItemDoubleClicked(int, const juce::MouseEvent&) override;
        WorkspaceShell& owner;
    };

    void select(const juce::String& key);
    void showDetails();
    void showPageMenu(const juce::String&, juce::Point<int>);
    void promptPage(const juce::String& parentId, const juce::String& renameId = {});
    void showMoveMenu(const juce::String&, juce::Point<int>);
    void archivePage(const juce::String&);
    void restoreArchived(const juce::String&);
    bool execute(const arranger::WorkspaceAction&);
    juce::String selectedPage() const;
    void openTask(arranger::TaskReference);
    void promptTask();
    void archiveTask(const juce::String&);
    void report(const juce::Result&);

    arranger::WorkspaceModel& model;
    std::unique_ptr<HubEditor> hub;
    Execute runCommand;
    std::function<void(juce::String)> persistView;
    juce::String viewKey;
    bool rebuilding = false;
    std::vector<NavRow> navigation;
    std::vector<arranger::TaskReference> taskReferences;
    std::vector<juce::String> archived;
    NavModel navModel { *this };
    ArchiveModel archiveModel { *this };
    juce::ListBox nav { "Pages", &navModel }, archive { "Archive", &archiveModel };
    TaskTree tasks;
    juce::Label brand, title, breadcrumb, description, empty;
    juce::TextButton addPage, addChild, rename, move, archiveButton, blocksTab, tasksTab;
    BlockEditor blockEditor;
    std::unique_ptr<TaskDetails> taskDetails;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WorkspaceShell)
};

