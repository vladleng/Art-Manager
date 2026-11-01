#pragma once
#include "TaskTree.h"
#include "WorkspaceTemplates.h"

// The same TaskTree edits a detached definition. Only Save writes the catalog;
// cancel/outside click never creates project/task records in the live workspace.
class WorkspaceTemplateEditor final : public juce::Component, public juce::DragAndDropContainer
{
public:
    using Save = std::function<juce::Result(const arranger::WorkspaceTemplate&)>;
    WorkspaceTemplateEditor(arranger::WorkspaceTemplate definition, Save save)
        : draft(std::move(definition)), persist(std::move(save)),
          tree(draft.content, [this](const auto& action)
          {
              auto candidate = draft.content; arranger::WorkspaceCommands cmd(candidate);
              auto result = action(cmd); if (result.wasOk()) result = candidate.validate();
              if (result.wasOk()) draft.content = std::move(candidate);
              return result;
          }, [] { return true; }, {}, [this] { refresh(); },
          [this](juce::String id)
          {
              if (!draft.project)
                  if (const auto* task = draft.content.findTask(id); task && task->parentTaskId.isEmpty()) return;
              arranger::WorkspaceCommands cmd(draft.content); cmd.archiveTask(id); refresh();
          }, [this] { addTask(); }, "220,110,80,180,100")
    {
        setLookAndFeel(&theme);
        setSize(780, 440);
        heading.setText(juce::String::fromUTF8("Шаблон · ") + (draft.project ? juce::String::fromUTF8("проект") : juce::String::fromUTF8("задача")), juce::dontSendNotification);
        name.setText(draft.title()); name.setSelectAllWhenFocused(true);
        name.setTextToShowWhenEmpty(juce::String::fromUTF8("Название шаблона"), mr::colour(mr::tokens::muted));
        hint.setText(juce::String::fromUTF8("Двойной клик — название / Notes · ПКМ — свойства и подзадачи"), juce::dontSendNotification);
        hint.setColour(juce::Label::textColourId, mr::colour(mr::tokens::meta));
        add.setButtonText(juce::String::fromUTF8("+ Задача"));
        accept.setButtonText(juce::String::fromUTF8("Сохранить шаблон"));
        cancel.setButtonText(juce::String::fromUTF8("Отмена"));
        for (auto* component : std::initializer_list<juce::Component*>{&heading, &name, &hint, &tree, &add, &accept, &cancel, &error}) addAndMakeVisible(component);
        add.onClick = [this] { addTask(); };
        accept.onClick = [this] { commit(); };
        cancel.onClick = [this] { close(); };
        name.onEscapeKey = [this] { close(); };
        refresh();
    }
    ~WorkspaceTemplateEditor() override { juce::PopupMenu::dismissAllActiveMenus(); setLookAndFeel(nullptr); }
    void paint(juce::Graphics& g) override { g.fillAll(mr::colour(mr::tokens::panel)); }
    void resized() override
    {
        auto bounds = getLocalBounds().reduced(12);
        heading.setBounds(bounds.removeFromTop(24));
        name.setBounds(bounds.removeFromTop(30)); bounds.removeFromTop(4);
        hint.setBounds(bounds.removeFromTop(24));
        auto footer = bounds.removeFromBottom(32);
        add.setBounds(footer.removeFromLeft(96).reduced(2));
        accept.setBounds(footer.removeFromRight(172).reduced(2));
        cancel.setBounds(footer.removeFromRight(90).reduced(2));
        error.setBounds(bounds.removeFromBottom(32));
        tree.setBounds(bounds);
    }
private:
    void refresh() { tree.setTasks(arranger::WorkspaceQueries::allTasks(draft.content)); }
    void addTask()
    {
        const auto flushed = tree.flush(); if (flushed.failed()) return;
        arranger::WorkspaceCommands cmd(draft.content); juce::String created;
        if (draft.project) cmd.createTask(draft.content.pages.front().id, juce::String::fromUTF8("Новая задача"), created);
        else
            for (const auto& task : draft.content.tasks) if (task.parentTaskId.isEmpty())
            { const auto id = task.id; cmd.createSubtask(id, juce::String::fromUTF8("Новая подзадача"), created); break; }
        refresh();
    }
    void commit()
    {
        auto result = tree.flush();
        if (result.wasOk())
        {
            auto saved = arranger::captureTemplate(draft.content, draft.content.pages.front().id);
            saved.project = draft.project; saved.content.id = draft.id();
            saved.content.pages.front().title = name.getText().trim();
            result = saved.validate();
            if (result.wasOk()) result = persist(saved);
        }
        if (result.failed()) { error.setText(result.getErrorMessage(), juce::dontSendNotification); return; }
        close();
    }
    void close() { if (auto* callout = findParentComponentOfClass<juce::CallOutBox>()) callout->dismiss(); }
    mr::ManagerTheme theme;
    arranger::WorkspaceTemplate draft;
    Save persist;
    TaskTree tree;
    juce::Label heading, hint, error;
    juce::TextEditor name;
    juce::TextButton add, accept, cancel;
};
