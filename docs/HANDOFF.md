# Arranger Manager — вход в разработку

**Статус на 2026-11-01:** самостоятельный Windows/JUCE Manager **0.2.5 принят пользователем**; [PR #66](https://github.com/vladleng/Art-Manager/pull/66) ранее объединён с `main` (merge `1358d4172c63b90340f519b25e4ab7618629ebce`). [Windows Build #109](https://github.com/vladleng/Art-Manager/actions/runs/41313192769) **SUCCESS, 10/10 тестов**; работает поиск/фильтры/сортировка с сохранением вида. Принятая предыдущая база — 0.2.0–0.2.4, включая #54 и #63, офлайн legacy-каталог, заметки и вложенные задачи.

## Правило merge — отдельно от приёмки

**Прямая установка пользователя:** `main` изменять **только** при явной команде «сливай в main» (или равнозначной), **не** после фразы «принимаю». Приёмка позволяет отметить Issue завершённой, но не разрешает слияние. Ветки/PR и Actions разрешены; пользователь отдельно запретил откатывать уже совершённый merge PR #66. Перед каждым следующим merge проверять именно его явное разрешение.

## Текущие незавершённые задачи

1. [#55-B / 0.2.6](https://github.com/vladleng/Art-Manager/issues/67): шаблоны проектов/задач и Notion-like hover — **серая полоса остаётся, а яркость букв при наведении не меняется**.
2. [#55-C / 0.2.7](https://github.com/vladleng/Art-Manager/issues/68): экспорт/импорт/recovery с конфликтами ID; перенос прежних блоков страниц в отдельные заметки по явной команде и с backup.
3. Остаточная ручная QA и доступ к историческим данным [#61](https://github.com/vladleng/Art-Manager/issues/61); автономный workspace [#37](https://github.com/vladleng/Art-Manager/issues/37) и UX [#55](https://github.com/vladleng/Art-Manager/issues/55) остаются открытыми до завершения этих пунктов.
4. Затем [#38 / 0.3 «Сегодня»](https://github.com/vladleng/Art-Manager/issues/38) → [#39 / 0.4 Календарь](https://github.com/vladleng/Art-Manager/issues/39) → [#40 / 0.5 Timeline](https://github.com/vladleng/Art-Manager/issues/40). Только потом — [MR Live #48](https://github.com/vladleng/Art-Manager/issues/48), затем [MR Studio #49](https://github.com/vladleng/Art-Manager/issues/49); без новых Studio Pro readers/ARA.

## Фактическая архитектура и границы

C++20, JUCE 9, Windows Desktop, Workspace schema **2**; `WorkspaceModel` / `Store`, `Commands` / `Queries` / `Controller`, `WorkspaceShell`, `TaskTree`, `BlockEditor`. Задачи имеют стабильные ID, owner, родителя (до восьми уровней по валидатору), статусы, приоритеты и Notes. Обычные страницы сразу показывают задачи, `note` страницы — блоковый редактор; прежние `page.blocks` доступны через ПКМ. Настройки поиска/фильтров/сортировки хранятся в настройках приложения, **не** в JSON workspace. Адаптера MR Live ещё нет.

**Подробно:** [ROADMAP](ROADMAP.md), [WORKSPACE-ARCHITECTURE](WORKSPACE-ARCHITECTURE.md), [MR-ECOSYSTEM](MR-ECOSYSTEM.md), и актуальные Issues. Исторический 0.1e–0.1h и прежняя схема 0.1i/0.1j — зафиксированная история, не план новых билдов.

## Начало следующего сеанса

Проверить ветку/HEAD/status и назначенный issue. Перед адаптером MR Live заново проверить актуальные ветки, native schema, ownership и revisions; старые commit-ссылки в MR-ECOSYSTEM — историческая сверка. В Live уже есть собственный item organizer: не создавать независимую копию задач. Не приравнивать старый DONE-count progress к рекурсивным процентам Live. Не вводить новый audio backend или прямую конкурентную запись открытого show.

Исторический контекст прежнего направления сохранён в [старом HANDOFF](https://github.com/vladleng/Arranger-Manager/blob/17ecf79888dd4182d86682beb0b9fc11feb9cef3/docs/HANDOFF.md) и документах HYBRID/HUB/ARA. Их старые «следующий шаг»/«DAW сохраняется» не переопределяют решение 2026-10-28.
