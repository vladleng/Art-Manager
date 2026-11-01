#pragma once
#include <JuceHeader.h>
#include "ArrangementTag.h"
#include "MRManagerAssets.h"
#include <map>
#include <string>

namespace mr
{
inline juce::Colour colour(std::uint32_t value) { return juce::Colour(static_cast<juce::uint32>(value)); }
inline juce::Colour statusFill(arranger::Status value)
{
    switch (value)
    {
        case arranger::Status::done: return colour(tokens::done);
        case arranger::Status::wip: return colour(tokens::wip);
        case arranger::Status::wait: return colour(tokens::wait);
        case arranger::Status::blocked: return colour(tokens::blocked);
        case arranger::Status::pool: return colour(tokens::panel);
        case arranger::Status::draft: return colour(tokens::selected);
        default: return colour(tokens::todo);
    }
}
inline juce::Colour statusInk(arranger::Status value)
{
    switch (value)
    {
        case arranger::Status::done: return colour(tokens::doneDot);
        case arranger::Status::wip: return colour(tokens::wipDot);
        case arranger::Status::wait: return colour(tokens::waitDot);
        case arranger::Status::blocked: return colour(tokens::blockedDot);
        case arranger::Status::pool: return colour(tokens::muted);
        case arranger::Status::draft: return colour(tokens::meta);
        default: return colour(tokens::todoDot);
    }
}
inline const char* statusIcon(arranger::Status value)
{
    switch (value)
    {
        case arranger::Status::done: return "manager/status/done_glyph";
        case arranger::Status::wip: return "manager/status/wip_glyph";
        case arranger::Status::wait: return "manager/status/wait_glyph";
        case arranger::Status::blocked: return "manager/status/blocked_glyph";
        case arranger::Status::todo: return "manager/status/todo_glyph";
        default: return nullptr; // POOL/DRAFT/unmarked keep explicit native labels and outline marker.
    }
}
inline juce::Colour priorityFill(int value)
{
    const std::uint32_t colours[] = {tokens::p1, tokens::p2, tokens::p3, tokens::p4};
    return colour(colours[juce::jlimit(1, 4, value) - 1]);
}
class Icons
{
public:
    Icons()
    {
        for (const auto& source : assets::icons)
            if (auto drawable = juce::Drawable::createFromSVGString(juce::String::fromUTF8(source.xml)))
                cache.emplace(source.id, std::move(drawable));
    }
    void draw(const char* id, juce::Graphics& g, juce::Rectangle<float> bounds)
    {
        // No XML parsing, allocation, filesystem or network reads in paint.
        for (auto& entry : cache)
            if (entry.first == id) { entry.second->drawWithin(g, bounds, juce::RectanglePlacement::centred, 1.0f); return; }
    }
private:
    std::map<std::string, std::unique_ptr<juce::Drawable>> cache;
};
class ManagerTheme final : public juce::LookAndFeel_V4
{
public:
    ManagerTheme()
    {
        const auto canvas = colour(tokens::canvas), panel = colour(tokens::panel);
        const auto item = colour(tokens::item), bright = colour(tokens::bright);
        const auto outline = colour(tokens::outline), focus = colour(tokens::focus);
        setColour(juce::Label::textColourId, bright);
        setColour(juce::TextButton::buttonColourId, panel);
        setColour(juce::TextButton::buttonOnColourId, colour(tokens::selected));
        setColour(juce::TextButton::textColourOffId, bright);
        setColour(juce::TextButton::textColourOnId, bright);
        setColour(juce::TextEditor::backgroundColourId, item);
        setColour(juce::TextEditor::textColourId, bright);
        setColour(juce::TextEditor::outlineColourId, outline);
        setColour(juce::TextEditor::focusedOutlineColourId, focus);
        setColour(juce::TextEditor::shadowColourId, juce::Colours::transparentBlack);
        setColour(juce::TextEditor::highlightColourId, focus.withAlpha(0.3f));
        setColour(juce::TextEditor::highlightedTextColourId, bright);
        setColour(juce::ComboBox::backgroundColourId, panel);
        setColour(juce::ComboBox::textColourId, bright);
        setColour(juce::ComboBox::outlineColourId, outline);
        setColour(juce::ComboBox::arrowColourId, focus);
        setColour(juce::PopupMenu::backgroundColourId, panel);
        setColour(juce::PopupMenu::textColourId, bright);
        setColour(juce::PopupMenu::headerTextColourId, colour(tokens::meta));
        setColour(juce::PopupMenu::highlightedBackgroundColourId, colour(tokens::selected));
        setColour(juce::PopupMenu::highlightedTextColourId, focus);
        setColour(juce::ListBox::backgroundColourId, canvas);
        setColour(juce::ListBox::textColourId, bright);
        setColour(juce::ListBox::outlineColourId, outline);
        setColour(juce::ScrollBar::backgroundColourId, canvas);
        setColour(juce::ScrollBar::thumbColourId, outline);
        setColour(juce::ToggleButton::textColourId, bright);
        setColour(juce::ToggleButton::tickColourId, focus);
        setColour(juce::ToggleButton::tickDisabledColourId, colour(tokens::muted));
        setColour(juce::AlertWindow::backgroundColourId, panel);
        setColour(juce::AlertWindow::textColourId, bright);
        setColour(juce::AlertWindow::outlineColourId, outline);
    }
    void drawButtonBackground(juce::Graphics& g, juce::Button& button,
        const juce::Colour& background, bool over, bool down) override
    {
        if (button.getButtonText().trim().isEmpty() || static_cast<bool>(button.getProperties()["mrIconOnly"])) return; // Icon-only: no permanent plate/frame.
        auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
        const bool enabled = button.isEnabled();
        g.setColour(background.withMultipliedAlpha(enabled ? 1.0f : 0.45f));
        g.fillRoundedRectangle(bounds, 4.0f);
        g.setColour(colour(button.hasKeyboardFocus(true) ? tokens::focus : tokens::outline)
            .withMultipliedAlpha(enabled ? 1.0f : 0.45f));
        g.drawRoundedRectangle(bounds, 4.0f, 1.0f);
        if (over || down)
        {
            g.setColour(colour(tokens::focus).withAlpha(down ? 0.14f : 0.07f));
            g.fillRoundedRectangle(bounds, 4.0f);
        }
    }
    void drawPopupMenuBackground(juce::Graphics& g, int width, int height) override
    {
        g.fillAll(colour(tokens::panel));
        g.setColour(colour(tokens::outline));
        g.drawRect(0, 0, width, height);
    }
};
}
