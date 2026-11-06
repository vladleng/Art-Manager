#pragma once
#include "MRManagerStyle.h"
#include "TaskPlanning.h"
#include <array>

// Local civil calendar. No OS locale or UTC conversion: values retain the
// timezone semantics of WorkspaceModel::timezone.
class PlanMonthGrid final : public juce::Component
{
public:
    std::function<void(juce::String)> onChoose;

    static int daysInMonth(int year, int month)
    {
        if (month < 1 || month > 12 || year < 1) return 0;
        const bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
        const int values[] = {31, leap ? 29 : 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
        return values[month - 1];
    }
    static int mondayIndex(int year, int month, int day = 1)
    {
        // Gregorian Zeller: Monday=0, Sunday=6. Independent of system TZ.
        if (month < 3) { month += 12; --year; }
        const int k = year % 100, c = year / 100;
        const int h = (day + 13 * (month + 1) / 5 + k + k / 4 + c / 4 + 5 * c) % 7;
        return (h + 5) % 7;
    }
    static juce::String format(int y, int m, int d)
    {
        return juce::String::formatted("%04d-%02d-%02d", y, m, d);
    }

    void setReference(juce::String day)
    {
        if (!arranger::isCivilDate(day)) return;
        today = std::move(day);
        if (selected.isEmpty()) setSelection(today);
    }
    void setSelection(juce::String day)
    {
        selected = arranger::isCivilDate(day) ? day : juce::String();
        const auto display = selected.isNotEmpty() ? selected : today;
        if (arranger::isCivilDate(display))
        {
            shownYear = display.substring(0, 4).getIntValue();
            shownMonth = display.substring(5, 7).getIntValue();
        }
        repaint();
    }
    void shiftMonth(int offset)
    {
        int months = (shownYear - 1) * 12 + shownMonth - 1 + offset;
        months = juce::jlimit(0, 9998 * 12 + 11, months);
        shownYear = months / 12 + 1;
        shownMonth = months % 12 + 1;
        repaint();
    }
    void paint(juce::Graphics& g) override
    {
        auto bounds = getLocalBounds();
        g.setColour(mr::colour(mr::tokens::panel));
        g.fillRect(bounds);
        g.setFont(juce::FontOptions(14.0f, juce::Font::bold));
        g.setColour(mr::colour(mr::tokens::bright));
        const char* months[] = {"Январь", "Февраль", "Март", "Апрель", "Май", "Июнь",
            "Июль", "Август", "Сентябрь", "Октябрь", "Ноябрь", "Декабрь"};
        g.drawText(juce::String::fromUTF8(months[shownMonth - 1]) + " " + juce::String(shownYear),
            40, 0, getWidth() - 80, 34, juce::Justification::centred);
        g.setFont(juce::FontOptions(17.0f));
        g.drawText(juce::String::fromUTF8("‹"), 4, 0, 32, 34, juce::Justification::centred);
        g.drawText(juce::String::fromUTF8("›"), getWidth() - 36, 0, 32, 34, juce::Justification::centred);
        const char* days[] = {"Пн", "Вт", "Ср", "Чт", "Пт", "Сб", "Вс"};
        const float cellWidth = static_cast<float>(getWidth() - 16) / 7.0f;
        g.setFont(juce::FontOptions(11.5f));
        for (int col = 0; col < 7; ++col)
        {
            g.setColour(mr::colour(mr::tokens::muted));
            g.drawText(juce::String::fromUTF8(days[col]),
                juce::Rectangle<float>(8.0f + col * cellWidth, 37.0f, cellWidth, 24.0f),
                juce::Justification::centred);
        }
        const int first = mondayIndex(shownYear, shownMonth);
        const int daysCount = daysInMonth(shownYear, shownMonth);
        g.setFont(juce::FontOptions(13.0f));
        for (int day = 1; day <= daysCount; ++day)
        {
            const int index = first + day - 1, col = index % 7, row = index / 7;
            const auto cell = juce::Rectangle<float>(8.0f + col * cellWidth, 62.0f + row * 30.0f,
                cellWidth, 29.0f).reduced(3.0f, 1.0f);
            const auto iso = format(shownYear, shownMonth, day);
            if (iso == selected)
            {
                g.setColour(mr::colour(mr::tokens::selected));
                g.fillRoundedRectangle(cell, 5.0f);
            }
            if (iso == today)
            {
                g.setColour(mr::colour(mr::tokens::focus));
                g.drawRoundedRectangle(cell, 5.0f, 1.3f);
            }
            g.setColour(mr::colour(iso == selected ? mr::tokens::bright : mr::tokens::meta));
            g.drawText(juce::String(day), cell, juce::Justification::centred);
        }
    }
    void mouseUp(const juce::MouseEvent& event) override
    {
        const auto p = event.getPosition();
        if (p.y < 35)
        {
            if (p.x < 40) shiftMonth(-1);
            else if (p.x >= getWidth() - 40) shiftMonth(1);
            return;
        }
        if (p.y < 62 || p.y >= 242 || p.x < 8 || p.x >= getWidth() - 8) return;
        const float cellWidth = static_cast<float>(getWidth() - 16) / 7.0f;
        const int col = juce::jlimit(0, 6, static_cast<int>((p.x - 8) / cellWidth));
        const int row = (p.y - 62) / 30;
        const int day = row * 7 + col - mondayIndex(shownYear, shownMonth) + 1;
        if (day <= 0 || day > daysInMonth(shownYear, shownMonth)) return;
        const auto date = format(shownYear, shownMonth, day);
        setSelection(date);
        if (onChoose) onChoose(date);
    }
private:
    int shownYear = 2026, shownMonth = 11;
    juce::String selected, today;
};

// Notion-like task date popover: month navigation, inline civil-day picker,
// start/end/deadline tabs, optional time and explicit save. A failed save
// leaves all draft input intact.
class TaskPlanEditor final : public juce::Component
{
public:
    using Save = std::function<juce::Result(arranger::TaskPlan)>;

    TaskPlanEditor(const arranger::TaskPlan& original, juce::String day, Save callback)
        : plan(original), persist(std::move(callback)), referenceDay(std::move(day)),
          draft {original.start, original.end, original.deadline}
    {
        setLookAndFeel(&theme);
        setSize(414, 535);
        heading.setText(tr("Дата задачи"), juce::dontSendNotification);
        hint.setText(tr("Выберите начало, конец или дедлайн"), juce::dontSendNotification);
        startTab.setButtonText(tr("Начало"));
        endTab.setButtonText(tr("Конец"));
        deadlineTab.setButtonText(tr("Дедлайн"));
        chooseToday.setButtonText(tr("Сегодня"));
        clearField.setButtonText(tr("Очистить дату"));
        includeTime.setButtonText(tr("Со временем"));
        save.setButtonText(tr("Сохранить"));
        unschedule.setButtonText(tr("Снять с плана"));
        cancel.setButtonText(tr("Отмена"));
        dateInput.setTextToShowWhenEmpty("YYYY-MM-DD", mr::colour(mr::tokens::muted));
        timeInput.setTextToShowWhenEmpty("HH:mm", mr::colour(mr::tokens::muted));
        for (auto* item : std::initializer_list<juce::Component*>{
                &heading, &hint, &startTab, &endTab, &deadlineTab,
                &dateInput, &timeInput, &includeTime, &calendar,
                &chooseToday, &clearField, &save, &unschedule, &cancel, &error})
            addAndMakeVisible(item);
        for (auto* label : {&hint, &error})
            label->setColour(juce::Label::textColourId, mr::colour(mr::tokens::meta));
        for (auto* button : {&startTab, &endTab, &deadlineTab, &chooseToday, &clearField, &save,
                             &unschedule, &cancel})
            button->setColour(juce::TextButton::buttonColourId, mr::colour(mr::tokens::selected));
        calendar.setReference(referenceDay);
        calendar.onChoose = [this](juce::String value)
        {
            dateInput.setText(value, false);
            acceptActiveDate();
        };
        startTab.onClick = [this] { changeField(0); };
        endTab.onClick = [this] { changeField(1); };
        deadlineTab.onClick = [this] { changeField(2); };
        includeTime.onClick = [this]
        {
            timeInput.setVisible(includeTime.getToggleState());
            resized();
        };
        chooseToday.onClick = [this]
        {
            if (!arranger::isCivilDate(referenceDay)) return;
            dateInput.setText(referenceDay, false);
            acceptActiveDate();
        };
        clearField.onClick = [this]
        {
            dateInput.clear();
            acceptActiveDate();
        };
        save.onClick = [this] { commit(); };
        unschedule.onClick = [this]
        {
            storeActive();
            draft[0].clear(); draft[1].clear();
            commitWithDraft();
        };
        cancel.onClick = [this] { close(); };
        dateInput.onReturnKey = [this] { acceptActiveDate(); };
        dateInput.onEscapeKey = [this] { close(); };
        timeInput.onReturnKey = [this] { acceptActiveDate(); };
        timeInput.onEscapeKey = [this] { close(); };
        changeField(0);
    }

    ~TaskPlanEditor() override { setLookAndFeel(nullptr); }
    void paint(juce::Graphics& g) override { g.fillAll(mr::colour(mr::tokens::panel)); }
    void resized() override
    {
        auto b = getLocalBounds().reduced(14);
        heading.setBounds(b.removeFromTop(23));
        hint.setBounds(b.removeFromTop(20));
        b.removeFromTop(7);
        auto tabs = b.removeFromTop(31);
        const int width = tabs.getWidth() / 3;
        startTab.setBounds(tabs.removeFromLeft(width).reduced(2, 1));
        endTab.setBounds(tabs.removeFromLeft(width).reduced(2, 1));
        deadlineTab.setBounds(tabs.reduced(2, 1));
        b.removeFromTop(6);
        auto dates = b.removeFromTop(33);
        dateInput.setBounds(dates.removeFromLeft(220).reduced(2));
        includeTime.setBounds(dates.reduced(2));
        b.removeFromTop(3);
        auto timeRow = b.removeFromTop(30);
        timeInput.setBounds(timeRow.removeFromLeft(100).reduced(2));
        b.removeFromTop(5);
        calendar.setBounds(b.removeFromTop(246));
        b.removeFromTop(7);
        auto quick = b.removeFromTop(29);
        chooseToday.setBounds(quick.removeFromLeft(110).reduced(2));
        clearField.setBounds(quick.removeFromLeft(140).reduced(2));
        b.removeFromTop(4);
        error.setBounds(b.removeFromTop(29));
        auto buttons = b.removeFromBottom(34);
        save.setBounds(buttons.removeFromRight(98).reduced(2));
        cancel.setBounds(buttons.removeFromRight(92).reduced(2));
        unschedule.setBounds(buttons.removeFromRight(144).reduced(2));
    }
    const std::array<juce::String, 3>& currentDraft() { storeActive(); return draft; }
    void selectField(int index) { changeField(index); }
private:
    static juce::String tr(const char* value) { return juce::String::fromUTF8(value); }
    void close()
    {
        if (auto* callout = findParentComponentOfClass<juce::CallOutBox>()) callout->dismiss();
    }
    void acceptActiveDate()
    {
        storeActive();
        const auto day = dateInput.getText().trim();
        calendar.setSelection(arranger::isCivilDate(day) ? day : juce::String());
    }
    void storeActive()
    {
        const auto date = dateInput.getText().trim();
        if (includeTime.getToggleState() && date.isNotEmpty())
            draft[static_cast<size_t>(active)] = date + "T" + timeInput.getText().trim();
        else draft[static_cast<size_t>(active)] = date;
    }
    void changeField(int next)
    {
        if (next < 0 || next > 2) return;
        if (initialised) storeActive();
        active = next; initialised = true;
        const auto value = draft[static_cast<size_t>(active)];
        dateInput.setText(value.substring(0, juce::jmin(10, value.length())), false);
        const bool time = value.containsChar('T');
        includeTime.setToggleState(time, juce::dontSendNotification);
        timeInput.setText(time ? value.fromFirstOccurrenceOf("T", false, false) : "09:00", false);
        timeInput.setVisible(time);
        calendar.setSelection(arranger::isCivilDate(dateInput.getText())
            ? dateInput.getText() : referenceDay);
        startTab.setToggleState(active == 0, juce::dontSendNotification);
        endTab.setToggleState(active == 1, juce::dontSendNotification);
        deadlineTab.setToggleState(active == 2, juce::dontSendNotification);
        resized();
    }
    void commit()
    {
        storeActive();
        commitWithDraft();
    }
    void commitWithDraft()
    {
        auto next = plan;
        next.start = draft[0]; next.end = draft[1]; next.deadline = draft[2];
        const auto valid = arranger::validateTaskPlan(next);
        const auto result = valid.failed() ? valid : persist(next);
        if (result.failed())
        {
            error.setText(result.getErrorMessage(), juce::dontSendNotification);
            return;
        }
        close();
    }
    arranger::TaskPlan plan;
    Save persist;
    juce::String referenceDay;
    std::array<juce::String, 3> draft;
    int active = 0;
    bool initialised = false;
    mr::ManagerTheme theme;
    juce::Label heading, hint, error;
    juce::TextButton startTab, endTab, deadlineTab, chooseToday, clearField, save, unschedule, cancel;
    juce::TextEditor dateInput, timeInput;
    juce::ToggleButton includeTime;
    PlanMonthGrid calendar;
};
