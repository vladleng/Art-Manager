#pragma once
#include "MRManagerStyle.h"

class WorkspaceTemplateCreation final : public juce::Component
{
public:
    using Create = std::function<juce::Result(juce::String, juce::String)>;
    WorkspaceTemplateCreation(juce::String initialName, juce::StringArray owners,
        std::vector<juce::String> ownerIds, int selectedOwner, Create create)
        : ids(std::move(ownerIds)), perform(std::move(create))
    {
        setLookAndFeel(&theme); setSize(380, ids.empty() ? 174 : 214);
        heading.setText(juce::String::fromUTF8("Создать из шаблона"), juce::dontSendNotification);
        name.setText(initialName); name.setSelectAllWhenFocused(true);
        owner.addItemList(owners, 1); owner.setSelectedId(selectedOwner + 1, juce::dontSendNotification);
        accept.setButtonText(juce::String::fromUTF8("Создать")); cancel.setButtonText(juce::String::fromUTF8("Отмена"));
        for (auto* component : std::initializer_list<juce::Component*>{&heading, &name, &accept, &cancel, &error}) addAndMakeVisible(component);
        if (!ids.empty()) addAndMakeVisible(owner);
        accept.onClick = [this] { commit(); }; cancel.onClick = [this] { close(); };
        name.onReturnKey = [this] { commit(); }; name.onEscapeKey = [this] { close(); };
    }
    ~WorkspaceTemplateCreation() override { setLookAndFeel(nullptr); }
    void paint(juce::Graphics& g) override { g.fillAll(mr::colour(mr::tokens::panel)); }
    void resized() override
    {
        auto b = getLocalBounds().reduced(12); heading.setBounds(b.removeFromTop(24));
        name.setBounds(b.removeFromTop(30)); b.removeFromTop(6);
        if (!ids.empty()) { owner.setBounds(b.removeFromTop(30)); b.removeFromTop(10); }
        auto footer = b.removeFromBottom(30);
        accept.setBounds(footer.removeFromRight(90)); footer.removeFromRight(6); cancel.setBounds(footer.removeFromRight(90));
        error.setBounds(b);
    }
private:
    void commit()
    {
        const int selected = owner.getSelectedId() - 1;
        if (!ids.empty() && (selected < 0 || selected >= static_cast<int>(ids.size()))) return;
        const auto result = perform(name.getText(), ids.empty() ? juce::String() : ids[static_cast<size_t>(selected)]);
        if (result.failed()) error.setText(result.getErrorMessage(), juce::dontSendNotification);
        else close();
    }
    void close() { if (auto* box = findParentComponentOfClass<juce::CallOutBox>()) box->dismiss(); }
    mr::ManagerTheme theme;
    std::vector<juce::String> ids;
    Create perform;
    juce::Label heading, error;
    juce::TextEditor name;
    juce::ComboBox owner;
    juce::TextButton accept, cancel;
};
