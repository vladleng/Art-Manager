#pragma once
#include <JuceHeader.h>
#include "MRManagerStyle.h"
#include "WorkspaceCommands.h"
#include "BlockEditor.h"
#include "TaskDetails.h"
#include "WorkspaceTaskViews.h"
#include "WorkspaceTemplateEditor.h"
#include <map>

class WorkspaceShell final : public juce::Component, public juce::DragAndDropContainer,
    public juce::DragAndDropTarget
{
public:
    using Execute = std::function<juce::Result(const arranger::WorkspaceAction&)>;
    WorkspaceShell(arranger::WorkspaceModel&, Execute,
        juce::String selectedView, std::function<void(juce::String)> saveView,
        juce::String savedColumns = {}, std::function<void(juce::String)> saveColumns = {},
        juce::String savedTaskViews = {}, std::function<void(juce::String)> saveTaskViews = {},
        juce::File templateFile = {});
    ~WorkspaceShell() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    void refresh();
    bool flushEdits();
    bool isInterestedInDragSource(const SourceDetails&) override;
    void itemDragMove(const SourceDetails&) override;
    void itemDragExit(const SourceDetails&) override;
    void itemDropped(const SourceDetails&) override;

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
    void promptPage(const juce::String& parentId, const juce::String& renameId = {}, bool asNote = false);
    void showMoveMenu(const juce::String&, juce::Point<int>);
    void archivePage(const juce::String&);
    void restoreArchived(const juce::String&);
    bool execute(const arranger::WorkspaceAction&);
    juce::String selectedPage() const;
    void openTask(arranger::TaskReference);
    void promptTask();
    void showTemplateMenu(bool project, juce::String parentId = {});
    void createFromTemplate(arranger::WorkspaceTemplate, juce::String parentId);
    void editTemplate(arranger::WorkspaceTemplate);
    void captureAsTemplate(juce::String pageId, juce::String taskId = {});
    void archiveTask(const juce::String&);
    void report(const juce::Result&);
    juce::String taskViewKey() const;
    void loadTaskViewSettings(const juce::String&);
    void saveTaskViewSettings();
    void updateTaskProjection();
    void showFilterMenu();
    void showSortMenu();

    mr::ManagerTheme theme;
    arranger::WorkspaceModel& model;
    arranger::WorkspaceTemplateStore templateStore;
    juce::Result templateLoad = juce::Result::ok();
    Execute runCommand;
    std::function<void(juce::String)> persistView;
    juce::String viewKey;
    std::map<juce::String, arranger::TaskViewOptions> taskViews;
    std::function<void(juce::String)> persistTaskViews;
    bool applyingView = false;
    juce::TextEditor searchBox;
    juce::TextButton filterButton, sortButton, clearButton;
    bool rebuilding = false;
    juce::String dropPageId;
    juce::String dropPageAt(juce::Point<int>) const;
    std::vector<NavRow> navigation;
    std::vector<arranger::TaskReference> taskReferences;
    std::vector<juce::String> archived;
    NavModel navModel { *this };
    ArchiveModel archiveModel { *this };
    juce::ListBox nav { "Pages", &navModel }, archive { "Archive", &archiveModel };
    TaskTree tasks;
    juce::Label brand, title, breadcrumb, description, empty;
    juce::TextEditor legacySummary; // Offline, read-only catalog archive; no DAW reader.
    juce::TextButton addPage, addNote, addChild, rename, move, archiveButton;
    BlockEditor blockEditor;
    std::unique_ptr<TaskDetails> taskDetails;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WorkspaceShell)
};

