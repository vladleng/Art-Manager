#include <JuceHeader.h>
#include "HubEditor.h"
#include "WorkspaceStore.h"
#include "WorkspaceShell.h"
#include "WorkspaceController.h"
#include <utility>

namespace
{
class HubWindow final : public juce::DocumentWindow, private juce::MenuBarModel
{
public:
    HubWindow(std::function<juce::String()> getPath,
        std::function<void(juce::String)> setPath, arranger::WorkspaceModel& workspace,
        std::function<void()> saveCatalog, std::function<void(int)> action,
        WorkspaceShell::Execute execute, juce::String selectedView,
        std::function<void(juce::String)> persistView)
        : juce::DocumentWindow("Arranger Manager 0.1h", juce::Colour(0xff1d232b),
              juce::DocumentWindow::allButtons), workspaceAction(std::move(action))
    {
        setUsingNativeTitleBar(true);
        setMenuBar(this);
        auto editor = std::make_unique<HubEditor>(std::move(getPath), std::move(setPath),
            &workspace.catalog, std::move(saveCatalog));
        setContentOwned(new WorkspaceShell(workspace, std::move(editor), std::move(execute),
            std::move(selectedView), std::move(persistView)), true);
        setResizable(true, false);
        setResizeLimits(1240, 760, 2300, 1600);
        centreWithSize(1480, 900);
        setVisible(true);
    }
    ~HubWindow() override { setMenuBar(nullptr); }
    void refreshWorkspace()
    {
        if (auto* shell = dynamic_cast<WorkspaceShell*>(getContentComponent())) shell->refresh();
    }
    bool flushEdits()
    {
        if (auto* shell = dynamic_cast<WorkspaceShell*>(getContentComponent())) return shell->flushEdits();
        return true;
    }
    void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
private:
    juce::StringArray getMenuBarNames() override { return {"Workspace"}; }
    juce::PopupMenu getMenuForIndex(int, const juce::String&) override
    {
        juce::PopupMenu menu;
        menu.addItem(1, "Storage information");
        menu.addSeparator();
        menu.addItem(3, "Restore previous workspace backup...");
        return menu;
    }
    void menuItemSelected(int item, int) override { workspaceAction(item); }
    std::function<void(int)> workspaceAction;
};

class HubApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "Arranger Manager"; }
    const juce::String getApplicationVersion() override { return "0.1h"; }
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise(const juce::String& commandLine) override
    {
        juce::PropertiesFile::Options options;
        options.applicationName = "Arranger Manager";
        options.folderName = "Moon River Studio";
        options.filenameSuffix = ".settings";
        settings.setStorageParameters(options);
        auto* user = settings.getUserSettings();
        if (user == nullptr) { failStartup("Cannot open application settings."); return; }
        projectPath = user->getValue("lastProjectPath");
        store = std::make_unique<arranger::WorkspaceStore>(
            user->getFile().getSiblingFile("Arranger Manager.workspace.json"));
        if (commandLine.contains("--restore-workspace-backup"))
        {
            const auto restored = store->restoreBackup();
            if (restored.failed()) { failStartup(restored.getErrorMessage()); return; }
        }
        const auto result = store->load(user->getValue("songCatalog"), projectPath, workspace);
        if (result.failed()) { failStartup(result.getErrorMessage()); return; }
        openMainWindow();
    }

    void systemRequestedQuit() override
    {
        if (window != nullptr && !window->flushEdits()) return;
        quit();
    }

    void shutdown() override
    {
        window.reset();
        controller.reset();
        store.reset();
        settings.saveIfNeeded();
    }

private:
    void failStartup(const juce::String& error)
    {
        juce::NativeMessageBox::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
            "Workspace could not be opened", error
                + "\nExisting data was not replaced. To restore the previous backup, start:\n"
                  "Arranger Manager.exe --restore-workspace-backup",
            nullptr, juce::ModalCallbackFunction::create([](int) { juce::JUCEApplication::quit(); }));
    }

    void saveWorkspace()
    {
        const auto result = store->save(workspace);
        if (result.failed())
            juce::NativeMessageBox::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                "Workspace was not saved", result.getErrorMessage()
                    + "\nChanges are still in memory. Existing saved data is retained.", window.get());
        else if (window != nullptr) window->refreshWorkspace();
    }

    void openMainWindow()
    {
        if (controller == nullptr) controller = std::make_unique<arranger::WorkspaceController>(workspace, *store);
        window = std::make_unique<HubWindow>(
            [this] { return projectPath; },
            [this](juce::String path)
            {
                projectPath = std::move(path);
                if (auto* user = settings.getUserSettings())
                {
                    user->setValue("lastProjectPath", projectPath);
                    user->saveIfNeeded();
                }
            }, workspace, [this] { saveWorkspace(); },
            [this](int action)
            {
                if (action == 1)
                {
                    juce::NativeMessageBox::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon,
                        "Workspace storage", "Version: 0.1h\nWorkspace ID: " + workspace.id
                            + "\nSchema: 2\nRevision: " + juce::String(workspace.revision)
                            + "\nSongs: " + juce::String(static_cast<int>(workspace.catalog.songs.size()))
                            + "\nPages: " + juce::String(static_cast<int>(workspace.pages.size()))
                            + "\nFile: " + store->getFile().getFullPathName()
                            + "\nBackup: " + store->backupFile().getFullPathName()
                            + "\nLegacy catalog backup: " + store->legacyFile().getFullPathName(), window.get());
                }
                else if (action == 3)
                {
                    if (!window->flushEdits()) return;
                    juce::AlertWindow::showOkCancelBox(juce::MessageBoxIconType::WarningIcon,
                        "Restore previous workspace", "Restore the previous saved version? The current file "
                        "will be preserved separately. Recent edits will no longer appear.",
                        "Restore", "Cancel", window.get(),
                        juce::ModalCallbackFunction::create([this](int choice)
                        {
                            if (choice != 1) return;
                            const auto result = store->restoreBackup();
                            if (result.failed())
                            {
                                juce::NativeMessageBox::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                                    "Restore failed", result.getErrorMessage(), window.get());
                                return;
                            }
                            window.reset();
                            const auto loaded = store->load({}, {}, workspace);
                            if (loaded.failed()) failStartup(loaded.getErrorMessage());
                            else openMainWindow();
                        }));
                }
            },
            [this](const arranger::WorkspaceAction& action)
            {
                auto result = controller->execute(action);
                return result;
            },
            settings.getUserSettings()->getValue("workspaceView", "view:daw"),
            [this](juce::String key)
            {
                if (auto* user = settings.getUserSettings())
                {
                    user->setValue("workspaceView", key);
                    user->saveIfNeeded();
                }
            });
    }

    juce::ApplicationProperties settings;
    juce::String projectPath;
    arranger::WorkspaceModel workspace;
    std::unique_ptr<arranger::WorkspaceStore> store;
    std::unique_ptr<arranger::WorkspaceController> controller;
    std::unique_ptr<HubWindow> window;
};
}
START_JUCE_APPLICATION(HubApplication)
