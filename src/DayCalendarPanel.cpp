#include "DayCalendarPanel.h"
#include "WorkspaceQueries.h"
#include <algorithm>

namespace
{
juce::String tr(const char* value) { return juce::String::fromUTF8(value); }

class EventForm final : public juce::Component
{
public:
    using Save = std::function<juce::Result(arranger::CalendarEvent)>;
    EventForm(arranger::CalendarEvent draft, Save save)
        : initial(std::move(draft)), commit(std::move(save))
    {
        setSize(360, 335);
        for (auto* component : {static_cast<juce::Component*>(&title),
            static_cast<juce::Component*>(&notes), static_cast<juce::Component*>(&start),
            static_cast<juce::Component*>(&end), static_cast<juce::Component*>(&allDay),
            static_cast<juce::Component*>(&saveButton), static_cast<juce::Component*>(&cancelButton),
            static_cast<juce::Component*>(&error)})
            addAndMakeVisible(component);
        title.setText(initial.title, false);
        notes.setText(initial.notes, false);
        start.setText(initial.start, false);
        end.setText(initial.end, false);
        title.setTextToShowWhenEmpty(tr("Название события"), mr::colour(mr::tokens::muted));
        notes.setTextToShowWhenEmpty(tr("Заметка (необязательно)"), mr::colour(mr::tokens::muted));
        start.setTextToShowWhenEmpty("YYYY-MM-DD or YYYY-MM-DDTHH:mm", mr::colour(mr::tokens::muted));
        end.setTextToShowWhenEmpty("YYYY-MM-DD or YYYY-MM-DDTHH:mm", mr::colour(mr::tokens::muted));
        allDay.setButtonText(tr("Весь день"));
        allDay.setToggleState(initial.allDay, juce::dontSendNotification);
        allDay.onClick = [this]
        {
            if (allDay.getToggleState())
            {
                start.setText(start.getText().substring(0, 10), false);
                end.setText(end.getText().substring(0, 10), false);
            }
            else
            {
                if (start.getText().length() == 10) start.setText(start.getText() + "T10:00", false);
                if (end.getText().length() == 10) end.setText(end.getText() + "T11:00", false);
            }
        };
        saveButton.setButtonText(tr("Сохранить"));
        cancelButton.setButtonText(tr("Отмена"));
        error.setColour(juce::Label::textColourId, mr::colour(mr::tokens::blockedDot));
        error.setFont(juce::FontOptions(11.0f));
        error.setJustificationType(juce::Justification::centredLeft);
        saveButton.onClick = [this]
        {
            auto next = initial;
            next.title = title.getText().trim(); next.notes = notes.getText();
            next.start = start.getText().trim(); next.end = end.getText().trim();
            next.allDay = allDay.getToggleState();
            const auto result = commit(next);
            if (result.failed())
                error.setText(result.getErrorMessage(), juce::dontSendNotification);
            else dismiss();
        };
        cancelButton.onClick = [this] { dismiss(); };
    }
    void paint(juce::Graphics& g) override
    {
        g.fillAll(mr::colour(mr::tokens::panel));
        g.setColour(mr::colour(mr::tokens::bright));
        g.setFont(juce::FontOptions(16.0f, juce::Font::bold));
        g.drawText(initial.id.isEmpty() ? tr("Новое событие") : tr("Изменить событие"),
            12, 10, getWidth() - 24, 28, juce::Justification::centredLeft);
        g.setColour(mr::colour(mr::tokens::muted)); g.setFont(juce::FontOptions(11.0f));
        g.drawText(tr("Начало"), 14, 138, getWidth() - 28, 17, juce::Justification::centredLeft);
        g.drawText(tr("Конец"), 14, 199, getWidth() - 28, 17, juce::Justification::centredLeft);
    }
    void resized() override
    {
        auto area = getLocalBounds().reduced(12);
        title.setBounds(area.getX(), 44, area.getWidth(), 33);
        notes.setBounds(area.getX(), 87, area.getWidth(), 33);
        start.setBounds(area.getX(), 157, area.getWidth(), 31);
        end.setBounds(area.getX(), 218, area.getWidth(), 31);
        allDay.setBounds(area.getX(), 257, 150, 24);
        error.setBounds(area.getX(), 285, area.getWidth() - 7, 19);
        saveButton.setBounds(getWidth() - 124, 306, 110, 27);
        cancelButton.setBounds(getWidth() - 236, 306, 104, 27);
    }
private:
    void dismiss()
    {
        if (auto* box = findParentComponentOfClass<juce::CallOutBox>()) box->dismiss();
    }
    arranger::CalendarEvent initial;
    Save commit;
    juce::TextEditor title, notes, start, end;
    juce::ToggleButton allDay;
    juce::TextButton saveButton, cancelButton;
    juce::Label error;
};
}

DayCalendarPanel::DayCalendarPanel(arranger::WorkspaceModel& data, Execute command,
    Before flush, std::function<void()> onChanged,
    std::function<void(arranger::TaskReference)> onOpenTask,
    std::function<void()> onClose)
    : model(data), execute(std::move(command)), before(std::move(flush)),
      changed(std::move(onChanged)), close(std::move(onClose)),
      openTask(std::move(onOpenTask))
{
    for (auto* button : {&previous, &next, &today, &newEvent, &dateButton, &dismiss})
    {
        addAndMakeVisible(button);
        button->setColour(juce::TextButton::buttonColourId, mr::colour(mr::tokens::panel));
        button->setColour(juce::TextButton::textColourOffId, mr::colour(mr::tokens::bright));
    }
    previous.setButtonText("<");
    next.setButtonText(">");
    today.setButtonText(tr("Сегодня"));
    newEvent.setButtonText("+");
    dismiss.setButtonText(tr("×"));
    previous.setTooltip(tr("Предыдущий день"));
    next.setTooltip(tr("Следующий день"));
    newEvent.setTooltip(tr("Создать календарное событие"));
    dismiss.setTooltip(tr("Скрыть календарную панель"));
    previous.onClick = [this] { setDay(arranger::addCivilDays(day, -1)); };
    next.onClick = [this] { setDay(arranger::addCivilDays(day, 1)); };
    today.onClick = [this]
    {
        const auto utc = juce::Time::getCurrentTime().toMilliseconds();
        setDay(juce::Time(utc + static_cast<juce::int64>(7 * 3600000))
            .toISO8601(true).substring(0, 10));
    };
    dateButton.onClick = [this] { datePrompt(); };
    newEvent.onClick = [this]
    {
        juce::PopupMenu menu; menu.setLookAndFeel(&getLookAndFeel());
        menu.addItem(1, tr("Событие на весь день"));
        menu.addItem(2, tr("Событие с временем"));
        juce::Component::SafePointer<DayCalendarPanel> safe(this);
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&newEvent),
            [safe](int selected)
            {
                if (!safe) return;
                if (selected == 1) safe->editEvent({}, true, -1);
                if (selected == 2) safe->editEvent({}, false, 600);
            });
    };
    dismiss.onClick = [this] { if (close) close(); };
    addAndMakeVisible(viewport);
    viewport.setViewedComponent(&timeline, false);
    viewport.setScrollBarsShown(true, false);
    viewport.setScrollBarThickness(7);
    viewport.setViewPosition(0, 8 * hourPixels);
    refresh();
}

DayCalendarPanel::~DayCalendarPanel()
{
    viewport.setViewedComponent(nullptr, false);
    juce::PopupMenu::dismissAllActiveMenus();
}

juce::String DayCalendarPanel::prettyDay(const juce::String& date)
{
    if (!arranger::isCivilDate(date)) return date;
    static const char* months[] = {"янв", "фев", "мар", "апр", "мая", "июн",
        "июл", "авг", "сен", "окт", "ноя", "дек"};
    static const char* weekdays[] = {"Вс", "Пн", "Вт", "Ср", "Чт", "Пт", "Сб"};
    const auto w = std::chrono::weekday {arranger::civilDay(date)}.c_encoding();
    const auto month = date.substring(5, 7).getIntValue();
    return date.substring(8, 10) + " " + tr(months[month - 1])
        + " · " + tr(weekdays[w]);
}

void DayCalendarPanel::setDay(juce::String value)
{
    if (!arranger::isCivilDate(value)) return;
    day = std::move(value);
    dateButton.setButtonText(prettyDay(day));
    refresh();
}

void DayCalendarPanel::refresh()
{
    if (day.isEmpty())
    {
        const auto utc = juce::Time::getCurrentTime().toMilliseconds();
        day = juce::Time(utc + static_cast<juce::int64>(7 * 3600000))
            .toISO8601(true).substring(0, 10);
    }
    dateButton.setButtonText(prettyDay(day));
    resized();
    repaint();
    timeline.repaint();
}

void DayCalendarPanel::resized()
{
    const int w = getWidth();
    previous.setBounds(10, 12, 26, 28);
    next.setBounds(w - 104, 12, 26, 28);
    today.setBounds(w - 75, 12, 62, 28);
    dateButton.setBounds(39, 12, juce::jmax(50, w - 150), 28);
    newEvent.setBounds(w - 49, 52, 34, 28);
    dismiss.setBounds(w - 78, 52, 26, 28);
    const int rows = static_cast<int>(entriesForDay(true).size());
    const int areaHeight = juce::jlimit(110, 213, 60 + rows * 28);
    allDayArea = {8, 84, juce::jmax(0, w - 16), areaHeight};
    viewport.setBounds(0, allDayArea.getBottom() + 8, w,
        juce::jmax(0, getHeight() - allDayArea.getBottom() - 8));
    timeline.setSize(juce::jmax(130, w - 7), 24 * hourPixels);
}

bool DayCalendarPanel::run(const arranger::WorkspaceAction& action)
{
    if (before && !before()) return false;
    const auto outcome = execute(action);
    if (outcome.failed())
    {
        juce::NativeMessageBox::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
            tr("Не удалось сохранить календарь"), outcome.getErrorMessage(), this);
        return false;
    }
    if (changed) changed();
    refresh();
    return true;
}

std::vector<DayCalendarPanel::Entry> DayCalendarPanel::entriesForDay(bool allDay) const
{
    std::vector<Entry> result;
    if (!arranger::isCivilDate(day)) return result;
    auto timeOf = [](const juce::String& text)
    {
        return text.substring(11, 13).getIntValue() * 60
            + text.substring(14, 16).getIntValue();
    };
    for (const auto& task : model.tasks)
    {
        if (arranger::WorkspaceQueries::taskArchived(model, task.id)
            || !arranger::planIncludesDay(task.plan, day)) continue;
        const bool isAllDay = task.plan.start.length() == 10;
        if (isAllDay != allDay) continue;
        Entry e;
        e.id = task.id; e.title = task.properties.name;
        e.source = arranger::WorkspaceQueries::breadcrumb(model, task.ownerPageId);
        e.allDay = isAllDay; e.status = task.properties.status;
        if (!isAllDay)
        {
            e.begin = task.plan.start.substring(0, 10) < day ? 0 : timeOf(task.plan.start);
            e.end = task.plan.end.length() == 16
                ? (task.plan.end.substring(0, 10) > day ? 1440 : timeOf(task.plan.end))
                : 1440;
            if (task.plan.end.isEmpty()) e.end = juce::jmin(1440, e.begin + 60);
            e.end = juce::jmax(e.begin + 15, e.end);
        }
        result.push_back(std::move(e));
    }
    for (const auto& event : model.events)
    {
        if (event.archivedAt.isNotEmpty() || event.allDay != allDay
            || event.start.substring(0, 10) > day
            || event.end.substring(0, 10) < day) continue;
        Entry e;
        e.id = event.id; e.title = event.title;
        e.source = event.ownerPageId.isNotEmpty()
            ? arranger::WorkspaceQueries::breadcrumb(model, event.ownerPageId)
            : tr("Событие");
        e.event = true; e.allDay = event.allDay;
        if (!allDay)
        {
            e.begin = event.start.substring(0, 10) < day ? 0 : timeOf(event.start);
            e.end = event.end.substring(0, 10) > day ? 1440 : timeOf(event.end);
            e.end = juce::jmax(e.begin + 15, e.end);
        }
        result.push_back(std::move(e));
    }
    std::stable_sort(result.begin(), result.end(), [](const Entry& a, const Entry& b)
    {
        if (a.begin != b.begin) return a.begin < b.begin;
        return a.title.compareIgnoreCase(b.title) < 0;
    });
    return result;
}

void DayCalendarPanel::paint(juce::Graphics& g)
{
    g.fillAll(mr::colour(mr::tokens::panel));
    g.setColour(mr::colour(mr::tokens::outline));
    g.drawVerticalLine(0, 0.0f, static_cast<float>(getHeight()));
    g.setColour(mr::colour(mr::tokens::bright));
    g.setFont(juce::FontOptions(12.0f, juce::Font::bold));
    g.drawText(tr("КАЛЕНДАРЬ"), 13, 56, getWidth() - 100, 20,
        juce::Justification::centredLeft);
    g.setColour(mr::colour(mr::tokens::outline));
    g.drawHorizontalLine(83, 0, static_cast<float>(getWidth()));
    g.setColour(mr::colour(mr::tokens::channel));
    g.fillRoundedRectangle(allDayArea.toFloat(), 6.0f);
    if (allDayHover)
    {
        g.setColour(mr::colour(mr::tokens::focus));
        g.drawRoundedRectangle(allDayArea.toFloat().reduced(1.0f), 6.0f, 1.8f);
    }
    g.setColour(mr::colour(mr::tokens::meta));
    g.setFont(juce::FontOptions(12.0f, juce::Font::bold));
    g.drawText(tr("Весь день"), allDayArea.getX() + 10, allDayArea.getY() + 6,
        allDayArea.getWidth() - 20, 23, juce::Justification::centredLeft);
    allDayHits.clear();
    auto entries = entriesForDay(true);
    const int available = juce::jmax(0, (allDayArea.getHeight() - 36) / 27);
    for (int i = 0; i < static_cast<int>(entries.size()) && i < available; ++i)
    {
        const auto& e = entries[static_cast<size_t>(i)];
        const juce::Rectangle<int> rect {allDayArea.getX() + 9,
            allDayArea.getY() + 31 + i * 27, allDayArea.getWidth() - 18, 23};
        g.setColour(e.event ? juce::Colour(0xff355253) : mr::colour(mr::tokens::selected));
        g.fillRoundedRectangle(rect.toFloat(), 4.0f);
        g.setColour(e.event ? juce::Colour(0xff81c9b7) : mr::statusInk(e.status));
        g.fillRoundedRectangle(static_cast<float>(rect.getX()), static_cast<float>(rect.getY()),
            3.0f, static_cast<float>(rect.getHeight()), 1.5f);
        g.setColour(mr::colour(mr::tokens::bright)); g.setFont(juce::FontOptions(11.5f));
        g.drawFittedText(e.title, rect.reduced(9, 0), juce::Justification::centredLeft, 1);
        allDayHits.push_back({rect, e});
    }
    if (static_cast<int>(entries.size()) > available)
    {
        g.setColour(mr::colour(mr::tokens::muted));
        g.drawText("+" + juce::String(static_cast<int>(entries.size()) - available),
            allDayArea.getRight() - 35, allDayArea.getBottom() - 21, 27, 16,
            juce::Justification::centredRight);
    }
}

void DayCalendarPanel::paintTimeline(juce::Graphics& g)
{
    const int w = timeline.getWidth();
    g.fillAll(mr::colour(mr::tokens::canvas));
    g.setColour(mr::colour(mr::tokens::outline));
    g.drawVerticalLine(gutter, 0.0f, static_cast<float>(timeline.getHeight()));
    for (int hour = 0; hour <= 24; ++hour)
    {
        const int y = hour * hourPixels;
        g.setColour(mr::colour(mr::tokens::outline).withAlpha(0.62f));
        g.drawHorizontalLine(y, gutter, static_cast<float>(w));
        if (hour < 24)
        {
            g.setColour(mr::colour(mr::tokens::muted));
            g.setFont(juce::FontOptions(11.0f));
            g.drawText(juce::String(hour).paddedLeft('0', 2) + ":00",
                5, y + 2, gutter - 9, 19, juce::Justification::centredLeft);
        }
    }
    if (hoveredMinute >= 0)
    {
        const int y = hoveredMinute;
        g.setColour(mr::colour(mr::tokens::focus).withAlpha(0.14f));
        g.fillRect(gutter + 2, y, w - gutter - 3, 15);
        g.setColour(mr::colour(mr::tokens::focus));
        g.drawHorizontalLine(y, gutter, static_cast<float>(w));
    }
    timedHits.clear();
    const auto entries = entriesForDay(false);
    for (int i = 0; i < static_cast<int>(entries.size()); ++i)
    {
        const auto& e = entries[static_cast<size_t>(i)];
        bool overlaps = false;
        int earlier = 0;
        for (int j = 0; j < static_cast<int>(entries.size()); ++j)
        {
            if (j == i) continue;
            const auto& other = entries[static_cast<size_t>(j)];
            if (e.begin < other.end && other.begin < e.end)
            {
                overlaps = true;
                if (j < i) ++earlier;
            }
        }
        const int available = juce::jmax(40, w - gutter - 8);
        const int width = overlaps ? available / 2 : available;
        const int x = gutter + 3 + (overlaps && earlier % 2 ? width : 0);
        const int y = e.begin + 2;
        const int h = juce::jmax(18, juce::jmin(e.end, 1440) - e.begin - 3);
        const juce::Rectangle<int> rect {x, y, juce::jmax(25, width - 3), h};
        g.setColour(e.event ? juce::Colour(0xff365054) : mr::colour(mr::tokens::selected));
        g.fillRoundedRectangle(rect.toFloat(), 4.0f);
        g.setColour(e.event ? juce::Colour(0xff80c1b1) : mr::statusInk(e.status));
        g.fillRoundedRectangle(static_cast<float>(rect.getX()), static_cast<float>(rect.getY()),
            3.0f, static_cast<float>(rect.getHeight()), 1.5f);
        g.setColour(mr::colour(mr::tokens::bright));
        g.setFont(juce::FontOptions(11.5f, juce::Font::bold));
        g.drawFittedText(e.title, rect.getX() + 9, rect.getY() + 3,
            rect.getWidth() - 13, juce::jmin(20, rect.getHeight() - 3),
            juce::Justification::centredLeft, 1);
        if (h > 42)
        {
            g.setColour(mr::colour(mr::tokens::meta));
            g.setFont(juce::FontOptions(10.0f));
            g.drawFittedText(e.source, rect.getX() + 9, rect.getY() + 22,
                rect.getWidth() - 13, 16, juce::Justification::centredLeft, 1);
        }
        timedHits.push_back({rect, e});
    }
}

int DayCalendarPanel::snapMinute(int y)
{
    return juce::jlimit(0, 1425, (juce::jmax(0, y) + 7) / 15 * 15);
}

juce::String DayCalendarPanel::dateTime(const juce::String& date, int minute)
{
    const auto next = arranger::addCivilDays(date, minute / 1440);
    if (next.isEmpty()) return {};
    const int remainder = minute % 1440;
    return next + "T" + juce::String(remainder / 60).paddedLeft('0', 2)
        + ":" + juce::String(remainder % 60).paddedLeft('0', 2);
}

bool DayCalendarPanel::isInterestedInDragSource(const SourceDetails& s)
{
    return s.description.toString().startsWith("manager-task|");
}

void DayCalendarPanel::itemDragMove(const SourceDetails& s)
{
    allDayHover = allDayArea.contains(s.localPosition);
    hoveredMinute = -1;
    repaint(); timeline.repaint();
}
void DayCalendarPanel::itemDragExit(const SourceDetails&)
{
    allDayHover = false; hoveredMinute = -1;
    repaint(); timeline.repaint();
}

void DayCalendarPanel::applyTaskDrop(juce::String description, int minute)
{
    if (before && !before()) return;
    const auto fields = juce::StringArray::fromTokens(description, "|", "");
    if (fields.size() != 4 || fields[0] != "manager-task") return;
    const auto* task = model.findTask(fields[1]);
    if (!task || arranger::WorkspaceQueries::taskArchived(model, fields[1])
        || task->ownerPageId != fields[2]
        || (task->parentTaskId.isEmpty() ? juce::String("root") : task->parentTaskId) != fields[3])
        return;
    const auto expected = task->plan;
    const auto targetDay = day;
    run([=](arranger::WorkspaceCommands& cmd)
        { return cmd.scheduleTaskAtSlot(fields[1], expected, targetDay, minute); });
}

void DayCalendarPanel::itemDropped(const SourceDetails& s)
{
    const bool valid = allDayArea.contains(s.localPosition);
    itemDragExit(s);
    if (valid) applyTaskDrop(s.description.toString(), -1);
}
void DayCalendarPanel::Timeline::itemDragMove(const SourceDetails& s)
{
    parent.hoveredMinute = DayCalendarPanel::snapMinute(s.localPosition.y);
    parent.allDayHover = false;
    parent.timeline.repaint(); parent.repaint();
}
void DayCalendarPanel::Timeline::itemDropped(const SourceDetails& s)
{
    const auto slot = DayCalendarPanel::snapMinute(s.localPosition.y);
    parent.itemDragExit(s);
    parent.applyTaskDrop(s.description.toString(), slot);
}

void DayCalendarPanel::mouseDown(const juce::MouseEvent& e)
{
    mouseStartedOnTimeline = false;
    mouseEntry.clear();
    if (!allDayArea.contains(e.getPosition())) return;
    for (auto it = allDayHits.rbegin(); it != allDayHits.rend(); ++it)
        if (it->bounds.contains(e.getPosition()))
        {
            mouseEntry = it->entry.id;
            mouseEntryIsEvent = it->entry.event;
            break;
        }
}

void DayCalendarPanel::mouseUp(const juce::MouseEvent& e)
{
    if (mouseEntry.isEmpty() || !allDayArea.contains(e.getPosition())) return;
    for (const auto& hit : allDayHits)
        if (hit.entry.id == mouseEntry && hit.entry.event == mouseEntryIsEvent)
        {
            if (e.mods.isPopupMenu()) showEntryMenu(hit.entry, e.getScreenPosition());
            else if (hit.entry.event) editEvent(hit.entry.id, true, -1);
            else if (const auto* task = model.findTask(hit.entry.id))
                if (openTask) openTask({{}, task->ownerPageId, task->id});
            break;
        }
    mouseEntry.clear();
}

void DayCalendarPanel::timelineDown(const juce::MouseEvent& e)
{
    mouseStartedOnTimeline = true;
    downY = e.y;
    downMinute = snapMinute(e.y);
    mouseEntry.clear(); resizeEntry = false;
    for (auto it = timedHits.rbegin(); it != timedHits.rend(); ++it)
        if (it->bounds.contains(e.getPosition()))
        {
            mouseEntry = it->entry.id;
            mouseEntryIsEvent = it->entry.event;
            resizeEntry = it->bounds.getBottom() - e.y <= 8;
            break;
        }
}

void DayCalendarPanel::timelineDrag(const juce::MouseEvent& e)
{
    if (!mouseStartedOnTimeline || mouseEntry.isEmpty() || e.mods.isPopupMenu()) return;
    hoveredMinute = snapMinute(e.y);
    timeline.repaint();
}

void DayCalendarPanel::timelineUp(const juce::MouseEvent& e)
{
    hoveredMinute = -1;
    timeline.repaint();
    if (!mouseStartedOnTimeline) return;
    mouseStartedOnTimeline = false;
    const auto id = mouseEntry; mouseEntry.clear();
    if (id.isEmpty()) return;
    for (const auto& hit : timedHits)
        if (hit.entry.id == id && hit.entry.event == mouseEntryIsEvent)
        {
            if (e.mods.isPopupMenu()) showEntryMenu(hit.entry, e.getScreenPosition());
            else if (std::abs(e.y - downY) >= 7)
                moveEntry(hit.entry, snapMinute(e.y), resizeEntry);
            else if (hit.entry.event) editEvent(hit.entry.id, false, hit.entry.begin);
            else if (const auto* task = model.findTask(hit.entry.id))
                if (openTask) openTask({{}, task->ownerPageId, task->id});
            break;
        }
}

void DayCalendarPanel::moveEntry(const Entry& entry, int minute, bool resize)
{
    if (entry.event)
    {
        const auto* event = model.findEvent(entry.id);
        if (!event || event->archivedAt.isNotEmpty()) return;
        const auto expected = *event;
        auto updated = expected;
        if (resize)
            updated.end = dateTime(day, juce::jmax(entry.begin + 15, minute + 15));
        else
        {
            const int span = juce::jmax(15, static_cast<int>(
                (arranger::civilDay(expected.end.substring(0, 10))
                - arranger::civilDay(expected.start.substring(0, 10))).count()) * 1440
                + expected.end.substring(11, 13).getIntValue() * 60
                + expected.end.substring(14, 16).getIntValue()
                - expected.start.substring(11, 13).getIntValue() * 60
                - expected.start.substring(14, 16).getIntValue());
            updated.start = dateTime(day, minute);
            updated.end = dateTime(day, minute + span);
        }
        run([=](arranger::WorkspaceCommands& cmd)
            { return cmd.updateCalendarEvent(expected, updated); });
    }
    else if (const auto* task = model.findTask(entry.id))
    {
        const auto expected = task->plan;
        const auto targetDay = day;
        if (resize)
        {
            auto next = expected;
            next.end = dateTime(targetDay, juce::jmax(entry.begin + 15, minute + 15));
            run([=](arranger::WorkspaceCommands& cmd)
                { return cmd.updateTaskPlan(entry.id, expected, next); });
        }
        else
            run([=](arranger::WorkspaceCommands& cmd)
                { return cmd.scheduleTaskAtSlot(entry.id, expected, targetDay, minute); });
    }
}

void DayCalendarPanel::showEntryMenu(const Entry& entry, juce::Point<int> position)
{
    juce::PopupMenu menu; menu.setLookAndFeel(&getLookAndFeel());
    if (entry.event)
    {
        menu.addItem(1, tr("Изменить событие"));
        menu.addItem(2, entry.allDay ? tr("Назначить время 10:00") : tr("На весь день"));
        menu.addSeparator();
        menu.addItem(3, tr("Удалить событие"));
    }
    else
    {
        menu.addItem(1, tr("Перейти к задаче"));
        menu.addItem(2, entry.allDay ? tr("Назначить время 10:00") : tr("На весь день"));
        menu.addItem(3, tr("Снять с плана"));
    }
    juce::Component::SafePointer<DayCalendarPanel> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this)
        .withTargetScreenArea({position.x, position.y, 1, 1}),
        [safe, entry](int action)
        {
            if (!safe || action == 0) return;
            if (entry.event)
            {
                const auto* event = safe->model.findEvent(entry.id);
                if (!event || event->archivedAt.isNotEmpty()) return;
                if (action == 1) { safe->editEvent(entry.id, entry.allDay, entry.begin); return; }
                const auto original = *event;
                if (action == 2)
                {
                    auto next = original;
                    next.allDay = !entry.allDay;
                    if (next.allDay)
                    {
                        next.start = safe->day;
                        next.end = safe->day;
                    }
                    else
                    {
                        next.start = dateTime(safe->day, 600);
                        next.end = dateTime(safe->day, 660);
                    }
                    safe->run([=](arranger::WorkspaceCommands& cmd)
                        { return cmd.updateCalendarEvent(original, next); });
                }
                else if (action == 3)
                    safe->run([=](arranger::WorkspaceCommands& cmd)
                        { return cmd.archiveCalendarEvent(original); });
            }
            else
            {
                const auto* task = safe->model.findTask(entry.id);
                if (!task || arranger::WorkspaceQueries::taskArchived(safe->model, entry.id)) return;
                if (action == 1)
                {
                    if (safe->openTask) safe->openTask({{}, task->ownerPageId, task->id});
                    return;
                }
                const auto expected = task->plan;
                if (action == 2)
                    safe->run([=](arranger::WorkspaceCommands& cmd)
                        { return cmd.scheduleTaskAtSlot(entry.id, expected, safe->day,
                            entry.allDay ? 600 : -1); });
                else if (action == 3)
                    safe->run([=](arranger::WorkspaceCommands& cmd)
                        { return cmd.removeTaskFromPlan(entry.id, expected); });
            }
        });
}

void DayCalendarPanel::editEvent(const juce::String& eventId, bool allDay, int minute)
{
    if (before && !before()) return;
    arranger::CalendarEvent draft;
    if (eventId.isNotEmpty())
    {
        const auto* existing = model.findEvent(eventId);
        if (!existing || existing->archivedAt.isNotEmpty()) return;
        draft = *existing;
    }
    else
    {
        draft.allDay = allDay;
        draft.start = allDay ? day : dateTime(day, minute < 0 ? 600 : minute);
        draft.end = allDay ? day : dateTime(day, (minute < 0 ? 600 : minute) + 60);
    }
    juce::Component::SafePointer<DayCalendarPanel> safe(this);
    auto form = std::make_unique<EventForm>(draft, [safe, draft](arranger::CalendarEvent next)
    {
        if (!safe) return juce::Result::fail("Calendar panel has closed.");
        if (safe->before && !safe->before())
            return juce::Result::fail("Finish pending edits before saving.");
        const auto valid = arranger::validateCalendarEvent(
            arranger::CalendarEvent {next.id.isEmpty() ? "preview" : next.id, next.title,
                next.notes, next.start, next.end, next.ownerPageId, next.archivedAt, next.allDay});
        if (valid.failed()) return valid;
        juce::String created;
        const auto result = safe->execute([&](arranger::WorkspaceCommands& cmd)
        {
            return draft.id.isEmpty()
                ? cmd.createCalendarEvent(next, created)
                : cmd.updateCalendarEvent(draft, next);
        });
        if (result.wasOk())
        {
            if (safe->changed) safe->changed();
            safe->refresh();
        }
        return result;
    });
    const auto anchor = newEvent.getScreenBounds();
    auto& callout = juce::CallOutBox::launchAsynchronously(std::move(form), anchor, nullptr);
    callout.setDismissalMouseClicksAreAlwaysConsumed(true);
}

void DayCalendarPanel::datePrompt()
{
    auto* dialog = new juce::AlertWindow(tr("Дата календаря"),
        tr("Введите дату в формате YYYY-MM-DD"),
        juce::MessageBoxIconType::NoIcon, this);
    dialog->addTextEditor("day", day, tr("Дата"));
    dialog->addButton(tr("Открыть"), 1, juce::KeyPress(juce::KeyPress::returnKey));
    dialog->addButton(tr("Отмена"), 0, juce::KeyPress(juce::KeyPress::escapeKey));
    juce::Component::SafePointer<DayCalendarPanel> safe(this);
    dialog->enterModalState(true, juce::ModalCallbackFunction::create([safe, dialog](int choice)
    {
        if (!safe || choice != 1) return;
        const auto next = dialog->getTextEditorContents("day").trim();
        if (arranger::isCivilDate(next)) safe->setDay(next);
        else juce::NativeMessageBox::showMessageBoxAsync(
            juce::MessageBoxIconType::WarningIcon, tr("Неверная дата"),
            tr("Введите существующую дату YYYY-MM-DD."), safe.getComponent());
    }), true);
}
