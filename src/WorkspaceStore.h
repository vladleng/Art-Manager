#pragma once
#include "WorkspaceModel.h"

namespace arranger
{
// Single-writer, whole-document transactions. User data lives outside settings.
// Windows replacement uses JUCE TemporaryFile (same directory).
class WorkspaceStore
{
public:
    explicit WorkspaceStore(juce::File destination) : file(std::move(destination)) {}
    const juce::File& getFile() const { return file; }
    juce::File backupFile() const { return file.getSiblingFile(file.getFileName() + ".backup"); }
    juce::File schemaOneFile() const { return file.getSiblingFile(file.getFileName() + ".schema-1.json"); }
    juce::File legacyFile() const { return file.getSiblingFile(file.getFileName() + ".legacy-v1.json"); }

    juce::Result load(const juce::String& legacy, const juce::String& lastPath, WorkspaceModel& output)
    {
        if (file.exists())
        {
            const auto text = file.loadFileAsString();
            WorkspaceModel candidate;
            const auto result = WorkspaceModel::fromJson(text, candidate);
            if (result.failed()) return result;
            const int version = static_cast<int>(juce::JSON::parse(text).getProperty("schemaVersion", 0));
            if (version == 1)
            {
                auto backup = schemaOneFile();
                if (backup.existsAsFile() && !backup.hasIdenticalContentTo(file))
                    backup = file.getSiblingFile(file.getFileName() + ".schema-1-" + juce::Uuid().toString() + ".json");
                if (!backup.existsAsFile())
                {
                    const auto preserved = atomicCopy(file, backup);
                    if (preserved.failed()) return preserved;
                }
            }
            lastSaved = text;
            loaded = true;
            if (version == 1)
            {
                const auto migrated = save(candidate);
                if (migrated.failed()) { loaded = false; return migrated; }
            }
            output = std::move(candidate);
            return juce::Result::ok();
        }
        WorkspaceModel candidate;
        auto result = WorkspaceModel::migrateLegacy(legacy, candidate);
        if (result.failed()) return result;
        if (legacy.isEmpty() && lastPath.isNotEmpty())
        {
            candidate.catalog.addSong(lastPath);
            candidate.reconcileCatalogPages();
        }
        if (legacy.isNotEmpty())
        {
            // Never overwrite the original backup on retry/relaunch.
            if (legacyFile().existsAsFile())
            {
                if (legacyFile().loadFileAsString() != legacy)
                    return juce::Result::fail("Legacy backup differs from settings; migration cancelled.");
            }
            else
            {
                result = atomicWrite(legacyFile(), legacy);
                if (result.failed()) return result;
            }
        }
        lastSaved.clear();
        loaded = true;
        result = save(candidate);
        if (result.wasOk()) output = std::move(candidate);
        else loaded = false;
        return result;
    }

    juce::Result save(WorkspaceModel& model)
    {
        if (!loaded) return juce::Result::fail("Workspace was not loaded; saving is disabled.");
        if (file.exists() ? file.loadFileAsString() != lastSaved : lastSaved.isNotEmpty())
            return juce::Result::fail("Workspace file changed externally; restart before saving.");
        auto candidate = model;
        candidate.reconcileCatalogPages();
        auto result = candidate.validate();
        if (result.failed()) return result;
        ++candidate.revision;
        const auto text = candidate.toJson();
        WorkspaceModel checked;
        result = WorkspaceModel::fromJson(text, checked);
        if (result.failed()) return result;
        // Backup is also replaced transactionally before touching the main file.
        // On first creation it contains the initial valid workspace.
        result = atomicWrite(backupFile(), lastSaved.isEmpty() ? text : lastSaved);
        if (result.failed()) return result;
        result = atomicWrite(file, text);
        if (result.wasOk())
        {
            lastSaved = text;
            model = std::move(candidate);
        }
        return result;
    }

    juce::Result restoreBackup()
    {
        if (!backupFile().existsAsFile()) return juce::Result::fail("Workspace backup is missing.");
        const auto text = backupFile().loadFileAsString();
        WorkspaceModel checked;
        auto result = WorkspaceModel::fromJson(text, checked);
        if (result.failed()) return result;
        if (file.existsAsFile())
        {
            const auto preserved = file.getSiblingFile(file.getFileName() + ".before-restore-"
                + juce::Uuid().toString() + ".json");
            if (!file.copyFileTo(preserved))
                return juce::Result::fail("Cannot preserve current file before recovery.");
        }
        result = atomicWrite(file, text);
        if (result.wasOk()) { loaded = false; lastSaved.clear(); }
        return result;
    }

private:
    static juce::Result atomicCopy(const juce::File& source, const juce::File& target)
    {
        if (target.isDirectory()) return juce::Result::fail("Workspace destination is a directory.");
        const auto directory = target.getParentDirectory().createDirectory();
        if (directory.failed()) return directory;
        juce::TemporaryFile temporary(target);
        if (!source.copyFileTo(temporary.getFile()) || !source.hasIdenticalContentTo(temporary.getFile()))
            return juce::Result::fail("Cannot preserve the exact schema 1 file.");
        if (!temporary.overwriteTargetFileWithTemporary())
            return juce::Result::fail("Cannot replace the schema 1 backup.");
        return juce::Result::ok();
    }

    static juce::Result atomicWrite(const juce::File& target, const juce::String& text)
    {
        if (target.isDirectory()) return juce::Result::fail("Workspace destination is a directory.");
        const auto directory = target.getParentDirectory().createDirectory();
        if (directory.failed()) return directory;
        juce::TemporaryFile temporary(target);
        {
            auto stream = temporary.getFile().createOutputStream();
            if (stream == nullptr) return juce::Result::fail("Cannot open workspace temporary file.");
            const auto utf8 = text.toUTF8();
            if (!stream->write(utf8.getAddress(), static_cast<size_t>(text.getNumBytesAsUTF8())))
                return juce::Result::fail("Cannot write workspace temporary file.");
            stream->flush();
            if (stream->getStatus().failed()) return stream->getStatus();
        }
        if (temporary.getFile().loadFileAsString() != text)
            return juce::Result::fail("Workspace write verification failed.");
        if (!temporary.overwriteTargetFileWithTemporary())
            return juce::Result::fail("Cannot replace workspace file; previous file retained.");
        return juce::Result::ok();
    }

    juce::File file;
    juce::String lastSaved;
    bool loaded = false;
};
}

