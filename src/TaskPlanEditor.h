#pragma once
#include "MRManagerStyle.h"
#include "TaskPlanning.h"

// An explicit local-date editor: failed validation/saves keep the draft visible.
class TaskPlanEditor final : public juce::Component
{
public:
    using Save = std::function<juce::Result(arranger::TaskPlan)>;

    TaskPlanEditor(const arranger::TaskPlan& original, Save callback)
        : plan(original), persist(std::move(callback))
    {
        setLookAndFeel(&theme);
        setSize(440, 300);
        heading.setText(tr("Планирование задачи"), juce::dontSendNotification);
        hint.setText(tr("Дата: ГГГГ-ММ-ДД · время: ГГГГ-ММ-ДДTЧЧ:ММ"),
            juce::dontSendNotification);
        startLabel.setText(tr("Начало"), juce::dontSendNotification);
        endLabel.setText(tr("Окончание"), juce::dontSendNotification);
        deadlineLabel.setText(tr("Дедлайн (необязательно)"), juce::dontSendNotification);
        start.setText(plan.start, false);
        end.setText(plan.end, false);
        deadline.setText(plan.deadline, false);
        save.setButtonText(tr("Сохранить"));
        clear.setButtonText(tr("Снять с плана"));
        cancel.setButtonText(tr("Отмена"));
        for (auto* control : std::initializer_list<juce::Component*>{
                &heading, &hint, &startLabel, &start, &endLabel, &end,
                &deadlineLabel, &deadline, &error, &save, &clear, &cancel})
            addAndMakeVisible(control);
        for (auto* label : {&hint, &startLabel, &endLabel, &deadlineLabel, &error})
            label->setColour(juce::Label::textColourId, mr::colour(mr::tokens::meta));
        save.onClick = [this] { commit(); };
        clear.onClick = [this]
        {
            start.clear(); end.clear();
            commit();
        };
        cancel.onClick = [this] { close(); };
        for (auto* input : {&start, &end, &deadline})
        {
            input->onReturnKey = [this] { commit(); };
            input->onEscapeKey = [this] { close(); };
        }
    }

    ~TaskPlanEditor() override { setLookAndFeel(nullptr); }
    void paint(juce::Graphics& g) override { g.fillAll(mr::colour(mr::tokens::panel)); }
    void resized() override
    {
        auto area = getLocalBounds().reduced(14);
        heading.setBounds(area.removeFromTop(25));
        hint.setBounds(area.removeFromTop(22));
        area.removeFromTop(5);
        for (auto* label : {&startLabel, &endLabel, &deadlineLabel})
        {
            auto* input = label == &startLabel ? &start
                : label == &endLabel ? &end : &deadline;
            label->setBounds(area.removeFromTop(18));
            input->setBounds(area.removeFromTop(28));
            area.removeFromTop(4);
        }
        auto buttons = area.removeFromBottom(34);
        save.setBounds(buttons.removeFromRight(106));
        buttons.removeFromRight(6);
        cancel.setBounds(buttons.removeFromRight(85));
        buttons.removeFromRight(6);
        clear.setBounds(buttons.removeFromRight(132));
        error.setBounds(area);
    }
private:
    static juce::String tr(const char* value) { return juce::String::fromUTF8(value); }
    void close()
    {
        if (auto* callout = findParentComponentOfClass<juce::CallOutBox>()) callout->dismiss();
    }
    void commit()
    {
        auto next = plan;
        next.start = start.getText().trim();
        next.end = end.getText().trim();
        next.deadline = deadline.getText().trim();
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
    mr::ManagerTheme theme;
    juce::Label heading, hint, startLabel, endLabel, deadlineLabel, error;
    juce::TextEditor start, end, deadline;
    juce::TextButton save, cancel, clear;
};
