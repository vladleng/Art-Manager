# #65 / 0.2.5 — Notion-inspired search / filters / sorts

Status: **source candidate, not accepted**. Parent #55 / #37. Code branch `feature/65-notion-task-views-0.2.5`; merge only by explicit user approval.

## Reference and visual contract

Official Notion help: https://www.notion.com/help/views-filters-and-sorts — a compact view toolbar with in-place search, distinct Filter/Sort dropdowns, scoped view settings and non-destructive projection. This implementation follows the interaction pattern in MR UDS graphite, not copyrighted Notion SVGs/brand pixels. No changes to the data backend.

- Search field at the top of task table; type to update results without a modal. Matches ordinary and legacy task names, regular Notes, nested tasks, checkpoint names/Notes, page titles/breadcrumbs and note-page blocks. When in **All Tasks**, matching pages and note pages also appear in the sidebar with full breadcrumbs. Clicking a note opens its independent block editor.
- Filter popup: **Status** (POOL/TODO/WIP/DRAFT/WAIT/DONE/BLOCKED) and **Priority** (all, P1–P4); combine with search using AND. Legacy tasks have no priority so disappear when a P filter is active; archived tasks remain hidden.
- Sort popup: original (manual) order, name A→Я / Я→A, priority P1→P4. Sort is stable, does not write to Workspace JSON; drag-reorder is disabled in non-manual sort to prevent misleading moves.
- One-click clear; a subtle dot in Filter/Sort button indicates active settings. Active settings stored per task-list view in desktop `taskViewOptions` local application preferences; `view:tasks` and each `page:<ID>` are independent. Wrong/corrupted setting values default safely. Changing view or restarting keeps the selections.
- When a nested task itself matches, its ancestor root remains included and the nested branch is expanded. Other existing children can remain visible under an expanded matching parent; only source rows (not underlying data) are filtered at this stage.
- Project/note pages with matching titles or note blocks are shown as matches in the **All Tasks** sidebar; all pages appear normally when the query clears.

## Test and safety

- `WorkspaceTaskViewsTests`: search across nested Notes/pages, case insensitive, parent visibility, combined filters, stable sort and preference round-trip. Existing `WorkspaceNavigationTests` / `TaskTreeUiTests` stay in CI.
- Workspace JSON schema, ID ownership, local Notes, legacy catalog and source order do not change. Search/filter/sort do not call `WorkspaceCommands`; only the original task edit/status paths mutate records.
- Need Windows UI acceptance after CI: verify typing, nested child matches, clearing, distinct pages, restart, sorting and manual drag after reset, old legacy rows, dark popup geometry/DPI. CI PASS alone does not confirm visual parity or real mouse behavior.

## Deliberately deferred #55 work

Reusable saved view presets beyond the current per-page settings, multi-condition compound Notion filter builder, multiple sort keys, full cross-document search result ranking, tasks/templates and export/import/backup UX remain future slices. No MR Live or MR Studio integration.
