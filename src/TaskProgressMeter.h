#pragma once
#include "MRManagerStyle.h"

// The same four-pixel progress meter is used by the task table and its UI
// regression. Clamp incoming percentage; the source of truth remains the
// domain DONE/count projection (not the manually edited parent status).
namespace mr
{
inline void paintTaskProgressMeter(juce::Graphics& g, float x, float y, int percent)
{
    constexpr float width = 76.0f, height = 4.0f, radius = 2.0f;
    percent = juce::jlimit(0, 100, percent);
    g.setColour(colour(tokens::progressBg));
    g.fillRoundedRectangle(x, y, width, height, radius);
    if (percent > 0)
    {
        g.setColour(colour(tokens::progressFill));
        g.fillRoundedRectangle(x, y, width * static_cast<float>(percent) / 100.0f,
            height, radius);
    }
}
}
