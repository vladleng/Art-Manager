# Arranger Manager — вход в разработку

## Решение 2026-10-28

Связь со **Studio Pro упразднена**. Manager развивается в MR-экосистеме: **сначала MR Live, затем MR Studio**. Каноническое решение и проверенные sources/assets: [MR-ECOSYSTEM](MR-ECOSYSTEM.md). [ROADMAP](ROADMAP.md), [CONCEPT](CONCEPT.md), [архитектура](WORKSPACE-ARCHITECTURE.md).

## Текущее состояние, без приписанной приёмки

- Main: приняты 0.1e–0.1g, PR #43–#45; последняя принятая версия 0.1g. Workspace schema 1 / catalog 2.
- Кандидат 0.1h: ветка `workspace-0-1h-general-tasks`, [PR #46](https://github.com/vladleng/Arranger-Manager/pull/46). Общие задачи/описания/чек-поинты, schema 2, раскрывающаяся таблица и действия по ПКМ. Последний функциональный source `9caea5891639bb6ba38ab882ebf19ed7f302ab0d`, [Windows Build #74](https://github.com/vladleng/Arranger-Manager/actions/runs/37249113209) — success, 8/8 тестовых наборов. Пользователь не принимал и не разрешал merge 0.1h.
- MR-интеграция #48 и удаление legacy reader/Hub/ARA #7 не выполнены. Кандидат визуального внедрения #47 описан ниже; прежняя docs-only актуализация сама не меняла runtime.

## Кандидат MR UDS — #47

Ветка `ui-47-mr-uds` поверх непринятой 0.1h. Внедрены закреплённые MR Manager assets и единый косметический профиль оболочки/таблиц/редакторов/ПКМ; данные/команды/геометрия остаются исходными. [Объём и проверка](MR-UDS-47.md). [PR #50](https://github.com/vladleng/Arranger-Manager/pull/50). [Windows MR Manager UI Build #4](https://github.com/vladleng/Arranger-Manager/actions/runs/40695578817), source `45aa5240d167d95ab1ec0b7cbad5b79b9627e5ed`: Desktop собран и пакет опубликован. Пользовательская визуальная приёмка ожидается; автоматические GUI preview/CTest не требуются для этого косметического среза. #48 не начинается автоматически.

## Уточнение после визуальной проверки #47

Пользователь попросил логику страниц/задач как в Notion. В том же PR #50: документный inline editor вместо формы «Блоки», компактная таблица, двойной клик по Notes/названиям, inline создание подзадачи, Enter/Esc/blur и сохранение черновиков при ошибке. [Контракт](NOTION-STYLE-EDITOR.md). Это поведенческое изменение, прежнее освобождение косметики от тестов не применяется; актуальные проверки/пакет фиксируются в #47/PR.

## Следующая работа

1. Уточнения/приёмка 0.1h — только в назначенном объёме; не считать решение сменить экосистему приёмкой PR #46.
2. [#47](https://github.com/vladleng/Arranger-Manager/issues/47) — дизайн Manager по MR Live / MR UDS, existing manager kit, без параллельной библиотеки и статических пользовательских показаний.
3. [#7](https://github.com/vladleng/Arranger-Manager/issues/7) — снять legacy Studio Pro зависимость, сохранив локальные данные и backups. Не удалять пользовательские `.song`/`.show`.
4. [#48](https://github.com/vladleng/Arranger-Manager/issues/48) — новый 0.1i: контракт IDs/владельцев/progress, затем первый MR Live adapter. Native `.mrlive`, browser demo show и layout не взаимозаменяемы. Read-only snapshot и command write-back — разные срезы.
5. 0.1j — поиск/переносимость/приёмка #37. Затем отдельная MR Studio связь [#49](https://github.com/vladleng/Arranger-Manager/issues/49); план дня/календарь/Timeline/аналитика/GPT #38–#42 сохраняются. Мобильный #35 отложен.

#19 `.show` отменена; writer `.song` и ARA/context bridge не возвращать в roadmap. Исторические PROG/Notes rules сохраняют смысл для старых данных, а не задают будущую интеграцию.

## Начало следующего сеанса

Проверить ветку/HEAD/status и назначенный issue. MR-Studio main отстаёт от Live-native/Manager-assets веток; актуальные ревизии и ссылки в MR-ECOSYSTEM. В Live уже есть собственный item organizer: не создавать независимую копию задач. Не приравнивать старый DONE-count progress к рекурсивным процентам Live. Не вводить новый audio backend или прямую конкурентную запись открытого show.

Исторический контекст прежнего направления сохранён в [старом HANDOFF](https://github.com/vladleng/Arranger-Manager/blob/17ecf79888dd4182d86682beb0b9fc11feb9cef3/docs/HANDOFF.md) и документах HYBRID/HUB/ARA. Их старые «следующий шаг»/«DAW сохраняется» не переопределяют решение 2026-10-28.
