# #7 — вывод Studio Pro: карта зависимостей и безопасный первый срез

Статус: **в разработке**, ветка `issue-7-legacy-isolation` от принятого `main` после PR #50. Этот документ описывает фактические правки в ветке; **не** является отчётом о приёмке или подтверждением Windows-сборки.

## Карта coupling до изменения

| Компонент | Прежняя роль | Решение в первом срезе |
| --- | --- | --- |
| `src/HubApplication.cpp` | Создавал `HubEditor`, сохранял `lastProjectPath`, открывал legacy DAW view | Desktop создаёт только `WorkspaceShell`; `lastProjectPath` читается один раз как **исторический источник первичной миграции**, но музыкальный проект не открывается |
| `src/WorkspaceShell.h/.cpp` | Владел HubEditor, отображал вкладку DAW; переход из legacy задачи открывал `.song` | Вкладка **«Архив песен»** и offline список локальных legacy задач с Notes, без Hub/editor |
| `src/TaskTree.cpp` | Команда «Открыть песню в DAW» | Переход к сохранённой песне/задаче в архиве |
| `src/WorkspaceQueries.h` | Общие страницы, задачи, ссылки на CatalogSong | Read-only summary из локального каталога: заголовок, статус, путь только **как текст**, число задач, все локальные Notes с ключами |
| `src/WorkspaceModel.h`, `src/WorkspaceStore.h` | Schema 2 JSON с `catalog.songs[]`, `tasks[]`, IDs, backups/recovery | **Не менять** формат/владельцев/поля; исторические `song/dawFolder` pages сохранять, отсутствие `.song` не удаляет данные |
| `src/WorkspaceCommands.h` | Общие команды в JSON, включая локальные edits legacy task/checkpoint по source ID/expected field | **Сохранить**; выполняет изменения локальных Notes/статусов без обращения к Studio Pro |
| `src/HubEditor.*`, `src/SongSnapshot.*`, `src/HubProcessor.*` | Reader `.song`, DAW hub, VST3 bridge | Desktop их **не собирает и не вызывает**; исходники оставлены для истории, часть parser tests остаётся в тестовом профиле |
| `src/PluginProcessor.*`, `src/PluginEditor.*`, `src/InspectorDocumentController.*` | ARA Inspector | CMake target и загрузка ARA SDK **удалены из конфигурации сборки**, исходные файлы пока не удаляются |
| `scripts/ArrangerManagerContextProbe/` | Старый Studio Pro context probe | Оставлен только как исторический исходник; не пакетируется и не подключается к Desktop |
| `CMakeLists.txt` и `.github/workflows/windows-build.yml` | Сборка Desktop+Hub VST3, optional ARA Inspector | Target Hub/ARA убран, Windows workflow собирает только Desktop и тесты; **версию нового кандидата перед запуском/публикацией CI ещё нужно назначить по `AGENTS.md`** |

## Граница первого среза

- Локальный SongCatalog остается в `WorkspaceModel` **без schema migration**. Ни `.song` ни `.show` не открываются и не удаляются; `.mrlive` не создается.
- **Сохранить DAW-модель задач для будущего объединения:** задача → checkpoint → Notes, ручные статусы и progress, приоритеты, локальные ID и порядок. В #7 данные legacy `catalog.songs[].tasks[]` остаются в исходном владельце и отображаются по ссылкам в общем списке. В последующем отдельном этапе привести legacy tasks и `WorkspaceTask` к единому каноническому task contract/командам с source references `MR Live`/`MR Studio`, **без второго редактируемого экземпляра и без потери ID**, отдельно согласовать семантику progress. Не смешивать эту миграцию с отключением Studio Pro.

- `Все задачи` показывает существующие задачи из каталога и обычных страниц по одним ID, `Архив песен` показывает сохранённые records и legacy задачи. Inline Notes/названия, status и checkpoints работают через `WorkspaceCommands`.
- Если в пользовательских настройках сохранена прежняя вкладка `view:daw`, GUI переводит её в `view:legacy`. Новый запуск по умолчанию открывает `Все задачи`.
- Текущий `WorkspaceStore` создаёт точную резервную копию schema 1 при миграции и сохраняет previous workspace backup. Любая будущая **деструктивная** миграция legacy records потребует отдельного read-only preview, backup и проверок; здесь миграции **нет**.
- Song/track/clip статистика Studio Pro не подменяется формулой MR Live. Данные статуса треков/клипов из внешнего файла не заявляются доступными в offline archive. Архив отображает только то, что уже есть в локальном JSON.

## Проверки и ограничения

- В `GeneralTaskTests` добавлен fixture с **отсутствующим файлом `.song`**: schema 1 → 2, исходные song/page/task IDs, status/type, Song/Task Notes, редактирование Notes командой, запись и перезапуск без файла.
- **NOT TESTED:** Windows compile, CTest, native GUI, recovery на пользовательских данных, реальный DPI, артефакт; CI/PR не запускаются отдельной командой до согласованной сборки.
- Старые исходники parser/Hub/ARA физически остаются в Git и не должны удаляться до проверки возможных зависимостей; их отсутствие в Desktop/runtime — проверяемая цель этого среза.
- **Номер первого кандидата по новой схеме закреплён: `0.2.0`** для среза #7 в текущем этапе 0.2. CMake/версия Desktop, имя Windows Actions run, название артефакта и generated `version.json` согласованы. Пока нет успешной сборки, это только заявленная конфигурация, не проверенный пакет. Исторические принятые `0.1h` не переименовывались.
- Не запускать full CTest/Windows package только ради документации. При прямой команде сборки сначала завершить версионирование и проверить таргеты.

## Оставшиеся задачи #7

1. Проверить билд и профильные тесты нового Desktop без Hub/ARA; исправить регрессии.
2. Верифицировать offline archive, редактирование legacy Notes/чекпоинтов, missing `.song`, backup/restore на Windows.
3. Определить, нужен ли отдельный режим просмотра глубоких сохранённых song/track/clip notes или экспорт/архив из JSON; нельзя удалять такие ключи без принятого решения/backup.
4. Пройти пользовательскую приёмку scope и лишь затем merge/закрытие #7; родительский #37 остаётся открыт.
