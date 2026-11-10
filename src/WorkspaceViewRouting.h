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
// Without in-memory navigation history (e.g. restored task detail after
// restart), use the task's owner page instead of trapping the user.
inline juce::String viewAfterTaskBack(const juce::String& previousView,
                                      const juce::String& ownerPageId)
{
    if (previousView == "view:tasks" || previousView == "view:archive"
        || previousView == "view:legacy"
        || previousView.startsWith("page:") || previousView.startsWith("pageTasks:")
        || previousView.startsWith("pageBlocks:"))
        return previousView;
    return ownerPageId.isNotEmpty() ? "pageTasks:" + ownerPageId : "view:tasks";
}
}
}
