#pragma once
#include "WorkspaceQueries.h"
#include <map>

namespace arranger
{
// Definitions are detached documents, never live references or workspace records.
struct WorkspaceTemplate
{
    bool project = true;
    WorkspaceModel content;
    juce::String id() const { return content.id; }
    juce::String title() const { return content.pages.empty() ? juce::String() : content.pages.front().title; }
    juce::Result validate() const
    {
        const auto result = content.validate();
        if (result.failed()) return result;
        if (!content.catalog.songs.empty() || !content.catalog.folders.empty()
            || content.pages.size() != 1 || content.pages.front().kind != "page"
            || content.pages.front().parentPageId.isNotEmpty() || content.pages.front().archivedAt.isNotEmpty())
            return juce::Result::fail("Invalid template page.");
        int roots = 0;
        for (const auto& task : content.tasks)
        {
            if (task.archivedAt.isNotEmpty()) return juce::Result::fail("Archived template task.");
            if (task.parentTaskId.isEmpty()) ++roots;
            for (const auto& cp : task.checkpoints)
                if (cp.archivedAt.isNotEmpty()) return juce::Result::fail("Archived template checkpoint.");
        }
        return !project && roots != 1 ? juce::Result::fail("A task template needs exactly one root task.") : juce::Result::ok();
    }
};

inline void renewTemplateIds(WorkspaceTemplate& definition)
{
    auto& content = definition.content;
    content.id = juce::Uuid().toString(); content.revision = 0;
    const auto owner = juce::Uuid().toString();
    content.pages.front().id = owner;
    for (auto& block : content.pages.front().blocks) block.id = juce::Uuid().toString();
    std::map<juce::String, juce::String> ids;
    for (const auto& task : content.tasks) ids[task.id] = juce::Uuid().toString();
    for (auto& task : content.tasks)
    {
        task.id = ids.at(task.id); task.ownerPageId = owner;
        if (task.parentTaskId.isNotEmpty()) task.parentTaskId = ids.at(task.parentTaskId);
        for (auto& block : task.blocks) block.id = juce::Uuid().toString();
        for (auto& cp : task.checkpoints) cp.id = juce::Uuid().toString();
    }
}

inline WorkspaceTemplate captureTemplate(const WorkspaceModel& source, const juce::String& pageId,
    const juce::String& taskId = {})
{
    WorkspaceTemplate result;
    result.project = taskId.isEmpty();
    const auto* page = source.findPage(pageId);
    if (!page || page->kind != "page" || WorkspaceQueries::isArchived(source, pageId)) return result;
    auto copy = *page; copy.parentPageId.clear();
    if (!result.project) copy.blocks.clear();
    result.content.pages.push_back(copy);
    for (const auto& task : source.tasks)
    {
        if (task.ownerPageId != pageId || WorkspaceQueries::taskArchived(source, task.id)) continue;
        bool include = result.project || task.id == taskId;
        auto parent = task.parentTaskId;
        while (!include && parent.isNotEmpty())
        {
            if (parent == taskId) { include = true; break; }
            const auto* ancestor = source.findTask(parent);
            if (!ancestor) break;
            parent = ancestor->parentTaskId;
        }
        if (!include) continue;
        auto value = task;
        if (task.id == taskId) { value.parentTaskId.clear(); result.content.pages.front().title = task.properties.name; }
        std::erase_if(value.checkpoints, [](const auto& cp) { return cp.archivedAt.isNotEmpty(); });
        result.content.tasks.push_back(std::move(value));
    }
    renewTemplateIds(result);
    return result;
}

inline std::vector<WorkspaceTemplate> builtinTemplates(bool project)
{
    std::vector<WorkspaceTemplate> result;
    const auto add = [&](const char* title, std::initializer_list<const char*> steps)
    {
        WorkspaceTemplate value; value.project = project;
        const auto page = juce::Uuid().toString();
        value.content.pages.push_back({page, {}, juce::String::fromUTF8(title), "page", {}, {}});
        juce::String root;
        if (!project)
        {
            WorkspaceTask task; task.id = juce::Uuid().toString(); task.ownerPageId = page;
            task.properties.name = juce::String::fromUTF8(title); root = task.id;
            value.content.tasks.push_back(task);
        }
        for (const auto* step : steps)
        {
            WorkspaceTask task; task.id = juce::Uuid().toString(); task.ownerPageId = page;
            task.parentTaskId = root; task.properties.name = juce::String::fromUTF8(step);
            value.content.tasks.push_back(task);
        }
        result.push_back(std::move(value));
    };
    if (project) add("Обычный проект", {"План", "Работа", "Проверка результата"});
    else add("Простая задача", {});
    add("Аранжировка", {"Концепция и референсы", "Гармония и форма", "Инструменты", "Запись", "Сведение", "Финальная проверка"});
    add("Работа над треком", {"Композиция", "Демо", "Аранжировка", "Запись", "Редактирование", "Сведение", "Мастеринг"});
    add("Запись", {"Подготовка сессии", "Настройка звука", "Дубли", "Отбор и монтаж", "Проверка"});
    add("Сведение", {"Подготовка", "Баланс", "Обработка", "Автоматизация", "Референсы", "Экспорт микса"});
    add("Публикация контента", {"Идея", "Материал", "Монтаж", "Обложка и описание", "Публикация", "Проверка"});
    return result;
}

// Separate versioned catalog: no workspace schema change or migration. A failed
// read/CAS/write retains definitions and draft; existing files are never reset.
class WorkspaceTemplateStore
{
public:
    explicit WorkspaceTemplateStore(juce::File destination) : file(std::move(destination)) {}
    std::vector<WorkspaceTemplate> definitions;
    juce::Result load()
    {
        if (!file.exists()) { loaded = true; lastSaved.clear(); return juce::Result::ok(); }
        const auto source = file.loadFileAsString();
        std::vector<WorkspaceTemplate> parsed;
        const auto result = decode(source, parsed);
        if (result.failed()) return result;
        definitions = std::move(parsed); lastSaved = source; loaded = true;
        return juce::Result::ok();
    }
    juce::Result put(const WorkspaceTemplate& definition)
    {
        const auto valid = definition.validate(); if (valid.failed()) return valid;
        auto candidate = definitions;
        const auto found = std::find_if(candidate.begin(), candidate.end(), [&](const auto& d) { return d.id() == definition.id(); });
        if (found == candidate.end()) candidate.push_back(definition); else *found = definition;
        return save(candidate);
    }
    juce::Result remove(const juce::String& id)
    {
        auto candidate = definitions;
        std::erase_if(candidate, [&](const auto& d) { return d.id() == id; });
        return save(candidate);
    }
    static juce::String encode(const std::vector<WorkspaceTemplate>& source)
    {
        auto root = std::make_unique<juce::DynamicObject>();
        root->setProperty("format", "ArtManagerTemplates"); root->setProperty("version", 1);
        juce::Array<juce::var> values;
        for (const auto& definition : source)
        {
            auto item = std::make_unique<juce::DynamicObject>();
            item->setProperty("project", definition.project);
            item->setProperty("content", juce::JSON::parse(definition.content.toJson()));
            values.add(juce::var(item.release()));
        }
        root->setProperty("templates", values);
        return juce::JSON::toString(juce::var(root.release()));
    }
    static juce::Result decode(const juce::String& source, std::vector<WorkspaceTemplate>& output)
    {
        const auto root = juce::JSON::parse(source);
        const auto items = root.getProperty("templates", {});
        if (!root.isObject() || root.getProperty("format", {}).toString() != "ArtManagerTemplates"
            || static_cast<int>(root.getProperty("version", 0)) != 1 || !items.isArray())
            return juce::Result::fail("Unsupported or damaged template catalog; file retained.");
        std::vector<WorkspaceTemplate> candidate;
        std::set<juce::String> ids;
        for (const auto& item : *items.getArray())
        {
            if (!item.isObject() || !item.getProperty("project", {}).isBool()) return juce::Result::fail("Invalid template type.");
            WorkspaceTemplate value; value.project = static_cast<bool>(item.getProperty("project", false));
            auto result = WorkspaceModel::fromJson(juce::JSON::toString(item.getProperty("content", {})), value.content);
            if (result.wasOk()) result = value.validate();
            if (result.failed()) return result;
            if (!ids.insert(value.id()).second) return juce::Result::fail("Duplicate template ID.");
            candidate.push_back(std::move(value));
        }
        output = std::move(candidate); return juce::Result::ok();
    }
private:
    static juce::Result write(const juce::File& destination, const juce::String& data)
    {
        if (destination.isDirectory()) return juce::Result::fail("Template destination is a directory.");
        const auto directory = destination.getParentDirectory().createDirectory(); if (directory.failed()) return directory;
        juce::TemporaryFile temporary(destination);
        if (!temporary.getFile().replaceWithText(data) || temporary.getFile().loadFileAsString() != data
            || !temporary.overwriteTargetFileWithTemporary()) return juce::Result::fail("Cannot save templates; draft retained.");
        return juce::Result::ok();
    }
    juce::Result save(const std::vector<WorkspaceTemplate>& candidate)
    {
        if (!loaded) return juce::Result::fail("Template catalog could not be loaded; saving disabled.");
        if (file.exists() ? file.loadFileAsString() != lastSaved : lastSaved.isNotEmpty())
            return juce::Result::fail("Template catalog changed externally; restart before saving.");
        const auto data = encode(candidate);
        std::vector<WorkspaceTemplate> checked;
        auto result = decode(data, checked); if (result.failed()) return result;
        result = write(file.getSiblingFile(file.getFileName() + ".backup"), lastSaved.isEmpty() ? data : lastSaved);
        if (result.wasOk()) result = write(file, data);
        if (result.wasOk()) { definitions = candidate; lastSaved = data; }
        return result;
    }
    juce::File file;
    juce::String lastSaved;
    bool loaded = false;
};
}
