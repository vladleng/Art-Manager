#pragma once
#include <juce_core/juce_core.h>

namespace arranger
{
// A newly created task should be visible without navigating into the
// old full-page TaskDetails editor. Preserve the user's current task list
// when it already displays the new task; otherwise open the owner page list.
inline juce::String viewAfterTaskCreation(const juce::String& currentView,
                                          const juce::String& ownerPageId)
{
    if (currentView == "view:tasks" || currentView == "pageTasks:" + ownerPageId)
        return currentView;
    return "pageTasks:" + ownerPageId;
}
}
