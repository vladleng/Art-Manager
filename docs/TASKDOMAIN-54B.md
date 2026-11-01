# #54-B / #59: Task table and drag/drop (0.2.2)

Implementation branch `feature/54-task-table-0.2.2` is **based on unmerged** `feature/54-task-domain-0.2.1` (PR #58). Do not merge either branch to main before explicit user approval. Both stages are work for #54; only the final Windows candidate with UI is suitable for visual user acceptance.

## Intended behavior

- An ordinary task row has editable Notes in a column **to the right of Priority**, followed by the Page column. The old additional Notes row beneath a task is gone; Task Details and document blocks remain independent.
- Double-click Notes edits the actual task/checkpoint/legacy local note through guarded `WorkspaceController`. Editor height starts at one line and grows with lines/wrapped text, capped at 196 px; Ctrl+Enter or blur saves, Esc cancels, failed saves keep the draft.
- The inline `+ Новая подзадача` creates a **real nested WorkspaceTask** for ordinary tasks. Legacy song records still create their own existing checkpoint records; no conversion.
- Drag an ordinary task/subtask within the task table to the top/bottom of a sibling (reorder), to the middle of another task (reparent), or over its `+ Новая подзадача` row (nest). Old ordinary checkpoints can be reordered within the same parent. Illegal drops are refused by the domain command.
- Drag an ordinary task over a page in the left navigation to move its entire subtree to that page. Legacy DAW tasks are read-only for moving between pages. A single visible drop marker shows the destination.
- Columns are resizable at header dividers, bounded by min/max and stored in application local settings `taskColumnWidths`, not in task data. The list's horizontal scrollbar handles narrow windows.
- Existing dark graphite MR UDS theme remains default, with popup fill alpha 0.9 (10% transparent). Native OS menus may retain system-level chrome.

## Test and acceptance boundary

- CMake/Windows workflow version and names set to 0.2.2, with TaskDomainTests, TaskTreeUiTests and full existing checks.
- CI / actual input / DPI must be verified **after** opening PR. Until then: NOT TESTED.
- Known pending: persisted undo for drag moves is not in this GUI slice. Don't mark Undo checkboxes complete before implementation. No two-way MR Live/Studio connectivity.
