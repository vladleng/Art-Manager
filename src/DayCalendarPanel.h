#pragma once
#include <JuceHeader.h>
#include "WorkspaceCommands.h"
#include "MRManagerStyle.h"
#include <functional>
#include <vector>

// #38/0.3.1: one shared calendar day on the right, no daily task copies.
// Both full task records and independent calendar events are resolved by ID.
class DayCalendarPanel final : public juce::Component, public juce::DragAndDropTarget
{
public:
    using Execute = std::function<juce::Result(const arranger::WorkspaceAction&)>;
    using Before = std::function<bool()>;
    DayCalendarPanel(arranger::WorkspaceModel&, Execute, Before,
        std::function<void()> onChanged,
        std::function<void(arranger::TaskReference)> onOpenTask,
        std::function<void()> onClose);
    ~DayCalendarPanel() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void refresh();
    void setDay(juce::String);
    const juce::String& selectedDay() const { return day; }

    bool isInterestedInDragSource(const SourceDetails&) override;
    void itemDragMove(const SourceDetails&) override;
    void itemDragExit(const SourceDetails&) override;
    void itemDropped(const SourceDetails&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;

private:
    struct Entry
    {
        juce::String id, title, source;
        bool event = false, allDay = false;
        int begin = 0, end = 0;
        arranger::Status status = arranger::Status::todo;
    };
    struct Hit
    {
        juce::Rectangle<int> bounds;
        Entry entry;
    };
    struct Timeline final : juce::Component, juce::DragAndDropTarget
    {
        explicit Timeline(DayCalendarPanel& p) : parent(p) {}
        void paint(juce::Graphics& g) override { parent.paintTimeline(g); }
        void mouseDown(const juce::MouseEvent& e) override { parent.timelineDown(e); }
        void mouseDrag(const juce::MouseEvent& e) override { parent.timelineDrag(e); }
        void mouseUp(const juce::MouseEvent& e) override { parent.timelineUp(e); }
        bool isInterestedInDragSource(const SourceDetails& s) override
            { return parent.isInterestedInDragSource(s); }
        void itemDragMove(const SourceDetails& s) override;
        void itemDragExit(const SourceDetails& s) override { parent.itemDragExit(s); }
        void itemDropped(const SourceDetails& s) override;
        DayCalendarPanel& parent;
    };

    bool run(const arranger::WorkspaceAction&);
    std::vector<Entry> entriesForDay(bool allDay) const;
    std::vector<Hit> allDayHits, timedHits;
    void paintTimeline(juce::Graphics&);
    void timelineDown(const juce::MouseEvent&);
    void timelineDrag(const juce::MouseEvent&);
    void timelineUp(const juce::MouseEvent&);
    void showEntryMenu(const Entry&, juce::Point<int>);
    void editEvent(const juce::String& eventId, bool allDay, int minute);
    void datePrompt();
    void applyTaskDrop(juce::String description, int minute);
    void moveEntry(const Entry&, int minute, bool resize);
    static juce::String dateTime(const juce::String&, int minute);
    static int snapMinute(int y);
    static juce::String prettyDay(const juce::String&);

    arranger::WorkspaceModel& model;
    Execute execute;
    Before before;
    std::function<void()> changed, openNothing, close;
    std::function<void(arranger::TaskReference)> openTask;
    juce::String day;
    juce::TextButton previous, next, today, newEvent, dateButton, dismiss;
    juce::Label hint;
    Timeline timeline {*this};
    juce::Viewport viewport;
    mr::Icons icons;
    int hoveredMinute = -1;
    bool allDayHover = false;
    juce::String mouseEntry;
    bool mouseEntryIsEvent = false;
    bool resizeEntry = false;
    bool mouseStartedOnTimeline = false;
    int downY = 0, downMinute = 0;
    juce::Rectangle<int> allDayArea;
    static constexpr int hourPixels = 60;
    static constexpr int gutter = 52;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DayCalendarPanel)
};
