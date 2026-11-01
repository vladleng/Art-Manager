#pragma once
#include <juce_gui_extra/juce_gui_extra.h>
#include "BlockEditorSession.h"

// Document UI with inline blocks; persistence/history remain in the existing session.
class BlockEditor final : public juce::Component, private juce::Timer, private juce::KeyListener
{
public:
    using Session = arranger::BlockEditorSession;
    BlockEditor();
    ~BlockEditor() override;
    juce::Result bind(const juce::String&, Session::Read, Session::Write);
    juce::Result flush();
    juce::Result undo();
    juce::Result redo();
    void paint(juce::Graphics&) override;
    void resized() override;
private:
    struct Row;
    bool keyPressed(const juce::KeyPress&, juce::Component*) override;
    void timerCallback() override;
    void capture();
    void refresh(const juce::String& focusId = {});
    void layoutRows();
    void addAfter(const juce::String&, const juce::String& type = "text", const juce::String& content = {});
    void menu(const juce::String&, bool slash = false);
    juce::Result history(bool redo);
    void updateStatus(const juce::Result&);
    static juce::String tr(const char*);
    Session session;
    juce::String boundId;
    bool loading = false;
    juce::Viewport viewport;
    juce::Component document;
    juce::TextButton addLine;
    juce::Label feedback;
    std::vector<std::unique_ptr<Row>> rows;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BlockEditor)
};
