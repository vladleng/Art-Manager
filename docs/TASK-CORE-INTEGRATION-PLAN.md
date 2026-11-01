# App-first: единая модель задач Arranger Manager и органайзера MR Live

**Статус:** архитектурное предложение 2026-11-01 для планирования #36/#37/#48/#49, **не реализованный API и не разрешение на миграцию данных**. Продуктовая очередность — подтверждённое решение пользователя: сначала довести самостоятельное приложение, затем интеграции. Старые версии и принятые этапы не переименовываются.

## Очередность

1. **Локальный Manager 0.2:** [#54](https://github.com/vladleng/Art-Manager/issues/54) — универсальная иерархическая TaskDomain; [#55](https://github.com/vladleng/Art-Manager/issues/55) — удобные проекты, страницы, таблицы, Notes, фильтры, поиск, шаблоны и переносимость; завершение независимого workspace [#37](https://github.com/vladleng/Art-Manager/issues/37)/0.1j.
2. **Локальные представления:** [#38](https://github.com/vladleng/Art-Manager/issues/38) план дня (0.3), [#39](https://github.com/vladleng/Art-Manager/issues/39) календарь (0.4), [#40](https://github.com/vladleng/Art-Manager/issues/40) Timeline по датам (0.5), все без внешнего приложения.
3. **После приёмки локального Manager:** [#48](https://github.com/vladleng/Art-Manager/issues/48) связать native MR Live Organizer с Manager, с read-only на первом этапе и command write-back после отдельной проверки.
4. **Затем [#49](https://github.com/vladleng/Art-Manager/issues/49)** — MR Studio на принятом source-adapter контракте. Аналитика #41 и GPT #42 — отдельные внешние интеграции, не блокирующие локальный Manager.

Конкретные номера `0.I.S` присваиваются реальным GitHub parent/sub-issues перед кандидатом согласно `AGENTS.md`, не по номерам Issues. Принятая #7 — Manager 0.2.0.

## Фактическая база (исходники main на 2026-11-01)

| Объект | Manager | Native MR Live |
| --- | --- | --- |
| Локальное хранение | Workspace JSON schema 2, `WorkspaceTask` с `TaskCheckpoint` одним уровнем | `.mrlive` show, `performance.itemTasks[itemId]`, `performance.itemWorkflow[itemId]` |
| Task узел | id, ownerPageId, name, Notes, status, priority, blocks, checkpoint IDs | id, parent, title, status, progress; до восьми уровней, без поля task Notes |
| Item workflow | Не является отдельной текущей сущностью Manager | status, priority P1–P4, progress **на item**; Notes на самом item |
| Status | POOL, TODO, WIP, DRAFT, WAIT, DONE, BLOCKED | Todo, Wip, Wait, Done |
| Progress | Число DONE/всех активных checkpoints (legacy отдельно) | Рекурсивное среднее дочерних прогрессов; leaf Done = 100%, иначе leaf progress |
| UI | WorkspaceShell, TaskTree, TaskDetails, BlockEditor | `LiveOrganizer` / `LiveItemEditor` с `editLiveInterface()` |
| Offline | Самостоятельные страницы и local legacy song tasks доступны без DAW | Задачи item доступны в открытом/сохранённом show без Arranger Manager |

Источники: Manager `src/WorkspaceModel.h`, `WorkspaceQueries.h`, `TaskTree.cpp`; MR Studio `apps/studio-juce/src/LiveShowState.h`, `LiveItemEditor.h`, `docs/MR_LIVE_NATIVE_INTEGRATION.md`. Не путать browser layout, demo show и native show; живые версии/схемы перепроверить непосредственно перед реализацией.

## Предлагаемый принцип объединения

**Не два одинаковых менеджера и не один файл, который одновременно пишут два процесса.** Остаются два **специализированных интерфейса** одной семантики задач:

- **MR Live** — компактный, автономный органайзер **конкретного item** (быстрые изменения при подготовке/на сцене); его show сохраняет исходные task IDs и owned поля. Отсутствие Manager не влияет на performance, audio callback и сохранение шоу.
- **Arranger Manager** — основное рабочее пространство: страницы/проекты, музыкальные и обычные задачи, дневной план, календарь, Timeline, архив. Для задач из Live показывает **reference/projection**, а не второй редактируемый экземпляр.
- **TaskDomain/контракт** — одинаковые понятия ID, parent, порядок, статус/приоритет/Notes/progress, команды и ограничения. В первом срезе это согласованные модели/адаптеры; выделять общую C++ библиотеку из обоих репозиториев целесообразно **лишь после** проверки стабильных API, схем и CI. Переиспользовать GUI Live в Manager не нужно.

### Владелец данных / ссылки

- `owner=workspace`: локальная Manager задача. Manager управляет полным состоянием; представления «Сегодня», календарь и Timeline — ссылки на тот же ID.
- `owner=mr-live`: native show → item → task. **MR Live — источник истины** для task title/status/progress/parent; `item.notes` и item priority остаются отдельными show полями. Ссылка Manager может содержать `sourceKind/documentId/itemId/taskId`, revision/schema/locator. Путь не используется как ID; повторное item occurrence в setlist не создаёт task.
- **Manager-owned annotations** (например, плановая дата или тег подготовки) по внешнему source reference допускаются, только если явно определены владельцы, формат и recovery; это не вторая копия данных задачи. Нельзя silently переносить priority item на task или добавлять task Notes в Live без соответствующего контракта.
- Схема `documentId` и стабильность Live task IDs **требуют подтверждения** в #48 на fixtures/Save for All/working vs browser; пример полей выше — логический контракт, не готовая сериализация.

### API / синхронизация в два этапа

1. **Read-only:** подтверждённый metadata snapshot/export или versioned reader, с document/task IDs, schema/revision/timestamp, stale/missing/offline. Manager открывает список и переходит к Live item, но не правит чужой show. Relink без дубликатов, исходные медиа и VST3 state не копируются.
2. **Write-back только после отдельной приёмки:** команды владельца в запущенный Live (или иной согласованный безопасный интерфейс): expected revision, validation, Undo, save/recovery, сообщаемые конфликты. **Никакой прямой перезаписи открытого `.mrlive` целиком из Manager.** `Save Show` и `Save for All` имеют различную область; отклонённые изменения не теряют черновик. Перед запуском/выступлением Manager не требуется.
3. В перспективе общий C++ `TaskCore` может предоставлять **модель, валидацию, сериализацию, команды** обоим приложениям; UI (Live TreeView / Manager Notion-like) остаются разными. Такой модуль не должен тянуть audio engine, ASIO и VST3 в Manager.

### Что надо согласовать перед #48

- Насколько `show/documentId` и task/parent IDs устойчивы в native сохранении, Browser saved/working versions и повторных items.
- Разрешаем ли Live задачам собственные Notes / priority / plannedAt, или оставляем часть атрибутов только как Manager annotations; что делать при отсутствии Live.
- Сопоставление дополнительных Manager статусов POOL/DRAFT/BLOCKED с четырьмя Live статусами — **не автоматическое**; неизвестное состояние сохраняется, а не переименовывается.
- Progress: ручное значение leaf Live и рекурсивное среднее vs DONE/count Manager, выбор явного `progressSource`; не менять статус от процента автоматически.
- Конфликт/Undo/revision и recovery при офлайне; отсутствие конфликта записи подтверждают тесты, не только UI.

## Ограничения и приёмка

До старта #48 не менять native show, не создавать direct `.mrlive` writer или новый Studio Pro reader/ARA bridge, не утверждать уже работающую синхронизацию. В app-first цикле проверить обычные и архивные DAW задачи, >=3 уровня вложенности, заметки/IDs/порядок, поиск/фильтры, даты, перезапуск, отказ записи, backups и независимость от MR Live. Эффективность и безопасность двухпроцессного обмена оцениваются отдельной приёмкой после локального Manager.
