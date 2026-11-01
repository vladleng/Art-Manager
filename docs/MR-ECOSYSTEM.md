# Manager в экосистеме MR

Решение пользователя: **2026-10-28**.

**Кандидат #47:** read-only Manager assets закреплены в `assets/ui/mr-uds/` этой ветки и внедрены в косметический профиль Manager. Детали и фактические проверки: [MR-UDS-47](MR-UDS-47.md). Это не интеграция #48; пользовательская визуальная приёмка ещё не получена.
 Действующая продуктовая основа; заменяет прежний план связи со Studio Pro. Этот документ задаёт архитектурное направление, не утверждает готовую интеграцию приложений. Визуальное внедрение в ветке #47 описано отдельно.

## Направление и порядок

Arranger Manager — самостоятельное рабочее пространство для музыки, проектов, контента и обычных задач. Первый музыкальный источник — **MR Live**, следующий — **MR Studio**. Название продукта пока сохраняется. Страницы, план дня, календарь, Timeline, аналитика и GPT остаются в плане; все представления используют исходные задачи или ссылки, без независимых копий.

1. Довести и проверить кандидат общих задач 0.1h / PR #46; он ещё не принят и не слит.
2. Применить общий визуальный язык MR UDS к назначенному UI-срезу Manager — #47.
3. Вместо прежнего 0.1i «DAW/.song в пространстве» выполнить контракт и первый адаптер MR Live — #48.
4. Поиск/переносимость/приёмка 0.1j учитывают source references; завершение 0.2 — #37.
5. После первого проверенного Live-контракта — отдельный адаптер MR Studio, #49. Он не блокирует первую связь с Live и не означает автоматическое начало Studio-разработки.

## Проверенные источники и их статус

Сверка 2026-10-28; перед реализацией повторить проверку актуальной ветки и назначенного issue.

| Источник MR-Studio | Проверенная ревизия / значение |
| --- | --- |
| [main](https://github.com/vladleng/MR-Studio/tree/3fc8859e66a75821cb402990897c96a60d5ecb9a) | Общие MR UDS, библиотека и правила; main может отставать от принятого Live runtime |
| [Опубликованный Live](https://vladleng.github.io/MR-Studio/tools/design-lab/live/) | Исходники публикации проверены в [gh-pages 5275a98](https://github.com/vladleng/MR-Studio/tree/5275a9853e4bab9fa6c23b3a81155985bff63b85/tools/design-lab/live). Это браузерный макет/демо, не доказательство native API/DSP |
| [Manager SVG Kit](https://github.com/vladleng/MR-Studio/tree/f0c3a67d2c67d1d9ff863bccc01eb11fb9e6cdb5/assets/ui/mr-uds) | feature/manager-svg-kit-v1, PR [MR-Studio#276](https://github.com/vladleng/MR-Studio/pull/276); ещё не в main, native adoption/приёмка отдельно |
| [Native Live](https://github.com/vladleng/MR-Studio/tree/1210be632b9750b3a07d0f0e08d9cdc4b228c4f8) | codex/mr-live-native-entry, PR [MR-Studio#275](https://github.com/vladleng/MR-Studio/pull/275); docs/MR_LIVE_NATIVE_INTEGRATION.md и MR_LIVE_INTERFACE_AUDIT.md |

Native Live уже существует: отдельный **MR Live.exe**, прямой запуск и `.mrlive`, собственный workflow и общий MR Core. Историческая формулировка main о Live только как встроенном режиме Studio уступает более новому решению 2026-10-25. Принятый runtime и оставшиеся области различаются в native audit/issues; наличие макета не завершает запись, patches, hardware/MIDI или концертную надёжность.

## Дизайн и ассеты

Каноническая библиотека MR: `assets/ui/mr-uds/`. Использовать существующие `manifest.json`, `catalogs/manager-v1.json`, `tokens/manager.json`, `svg/manager/`, а также `docs/MR_UNIFIED_DESIGN_SYSTEM.md` и `.codex/skills/mr-unified-gui/SKILL.md` в MR-Studio. Не создавать параллельную библиотеку или расходящийся Theme.

Manager Kit содержит **46 SVG: 25 reference, 10 template, 11 icon**. Он включает status pills/glyphs, P1–P4, progress, ready/inactive, item/set/search/panel, M/S/R и действия organizer. `reference` содержит вымышленные подписи и значения; в runtime применять шаблоны/иконки, а реальные тексты, статус, проценты и показатели рисовать из модели. Ready, M/S/R и метры не добавляются в органайзер как декоративные работающие функции.

| Роль Manager | Цвет |
| --- | --- |
| Canvas / panel | `#191A1C` / `#242629` |
| Item / selected | `#292D33` / `#2B3037` |
| Outline / text | `#415167` / `#DAE5F4` |
| Focus и metadata | `#91BBEE` |
| Progress fill | `#59AD87` |

Это целевой scoped-профиль Manager от 2026-10-26, более конкретный, чем прежняя общая navy-палитра. Компактные почти плоские графитовые поверхности, тонкие холодные контуры, 4px rhythm; semantic state + текст/форма. Примерные размеры status 66×20, priority 30×18, progress 82×6, item 334×52 — reference, не обязательная ширина всего рабочего пространства. Не копировать фиксированную DAW-раскладку в таблицу задач. Иконки без постоянной рамки, текстовые кнопки могут иметь рамку; реальные hit areas/focus сохраняются. Действия задач/подзадач доступны по ПКМ, раскрытие — в строках.

В #47 закрепить версию и доставку нужных ресурсов (включение/пакетирование с provenance), без runtime download из GitHub и без независимо редактируемого форка kit. Старый `build_kit.py` переписывает manifest; не запускать его автоматически. Наличие ассета не означает его внедрение в EXE или пользовательскую приёмку.

## Контракт интеграции: что определить в #48

- Browser layout `mr-live-layout` schema 1 (v5 — ревизия макета), demo show v3 и native `.mrlive` (`mr-live-show` v2 в изученном контракте) — разные документы. Layout не содержит задач, содержимого show или audio/plugin state. Не использовать его как интеграционный snapshot.
- В Live уже есть item organizer, Notes, status/priority, рабочий item и сохранённая Browser version / Save for All. Перепроверить native model/commands до выбора обмена; опубликованный JS не является native API.
- Предлагаемая логическая ссылка: источник → документ → item/project → task/checkpoint. Это требования к идентичности, не утверждённые имена полей wire format. Проверить существующие постоянные IDs. Имя, путь и номер setlist occurrence не заменяют ID. Повторные occurrences одного item не создают копий задач.
- Обычной задачей владеет Workspace. Исходная Live-задача остаётся у своего владельца; Manager предоставляет ссылку/проекцию через adapter. До подтверждённого command bridge первый snapshot помечается read-only, с revision/time и stale/missing.
- Не терять POOL/DRAFT/BLOCKED при сопоставлении с четырьмя browser Live статусами Todo/Wip/Wait/Done. Не менять сохранённые P3 Manager на default P4 Live.
- Старый Manager считает DONE/active checkpoints (legacy PROG ещё и tracks/tasks); browser Live `taskProgress` рекурсивно усредняет дочерние проценты. Нужна явная политика, а не молчаливое приравнивание. Ручной статус не равен прогрессу.
- Выбрать versioned metadata export/snapshot или IPC/commands, права на поля, правила revision conflict, relink, archive/delete, повторов и offline. Сохранение show и Save for All имеют разную область действия.
- Write-back — отдельный проверяемый срез через команды владельца с Undo/save/recovery. Не заменять открытый `.mrlive` сторонней записью целого JSON. Никаких file/network/serialization операций в audio callback; доступность Manager не блокирует Live.

MR Studio использует тот же слой источников после Live, но проверяется отдельно. [MR-Studio#291](https://github.com/vladleng/MR-Studio/issues/291) — экспорт Studio → Live как Playback; это не готовая синхронизация задач Manager.

## Завершение Studio Pro направления

- Новая функциональная связь со Studio Pro, чтение новых вариантов `.song`, исследование `.show`, writer и ARA/context bridge исключены из будущего scope.
- #19 закрывается как not planned. #7 переопределён на вывод legacy-зависимости и сохранность данных; закрытые исследования не переоткрываются.
- Существующий reader/Hub/Inspector остаётся фактом старого кода, пока отдельная работа #7 не удалит/отключит его. Это решение документации не удаляет пользовательские файлы и не делает runtime уже независимым от Studio Pro во всех экранах.
- При выводе legacy сохранять workspace, tasks/checkpoints IDs, Notes keys, порядок, статусы/приоритеты, архивы и backups. Отсутствие старого `.song` не должно удалять локальные задачи. Не конвертировать `.song` в `.mrlive` по предположению; старые проекты остаются пользовательскими файлами.
- Исторические HYBRID/HUB/ARA документы описывают прежние сборки, не текущий roadmap.

## Проверка этого решения

Проверены ветки, docs, каталог/manifest references, SVG и CSS/JS опубликованного редактора, открытые issues/PR. Интерактивная браузерная сессия, native UI, сборки и тесты в этом docs-only изменении не выполнялись. Новая интеграция и удаление legacy кода не заявляются реализованными. В ветке #47 перенос закреплённых SVG и косметическое внедрение описаны отдельно в MR-UDS-47.
