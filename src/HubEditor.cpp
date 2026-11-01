#include "HubEditor.h"
#include "SongProgress.h"
#include "StudioProColour.h"
#include <algorithm>
#include <iterator>
#include <numeric>
#include <tuple>
#include <utility>

namespace
{
juce::Colour statusColor(arranger::Status s)
{
    switch (s)
    {
        case arranger::Status::pool: return juce::Colour(0xff8b78b8);
        case arranger::Status::todo: return juce::Colour(0xff818a96);
        case arranger::Status::wip: return juce::Colour(0xff3b82f6);
        case arranger::Status::draft: return juce::Colour(0xff35b8d6);
        case arranger::Status::wait: return juce::Colour(0xffd3a438);
        case arranger::Status::done: return juce::Colour(0xff34a878);
        case arranger::Status::blocked: return juce::Colour(0xffdd5b61);
        default: return juce::Colour(0xff56606a);
    }
}

void badge(juce::Graphics& g, juce::Rectangle<int> bounds, juce::Colour colour, const char* label)
{
    g.setColour(colour);
    g.fillRoundedRectangle(bounds.toFloat(), 5.0f);
    g.setColour(juce::Colours::white);
    g.setFont(juce::FontOptions(12.0f, juce::Font::bold));
    g.drawText(label, bounds, juce::Justification::centred);
}

struct Columns { int type, parts, partsWidth, status, prog, notes, notesWidth, nameWidth, statusWidth, progWidth; };
juce::PopupMenu::Options menuAt(juce::Point<int> screen, juce::Component& target)
{
    return juce::PopupMenu::Options().withTargetComponent(target)
        .withTargetScreenArea({screen.x, screen.y, 1, 1});
}
Columns columnsFor(int width)
{
    const bool wide = width >= 700;
    const int nameWidth = std::max(145, static_cast<int>(width * (wide ? 0.38f : 0.35f)));
    const int type = nameWidth + 6;
    const int parts = type + 32;
    const int partsWidth = wide ? 86 : 62;
    const int status = parts + partsWidth + 6;
    const int statusWidth = wide ? 90 : 76;
    const int prog = status + statusWidth + 7;
    const int progWidth = wide ? 86 : 64;
    const int notes = prog + progWidth + 10;
    return { type, parts, partsWidth, status, prog, notes, std::max(1, width - notes - 10),
        nameWidth, statusWidth, progWidth };
}

void mediaIcon(juce::Graphics& g, int x, int height, bool midi)
{
    const float left = static_cast<float>(x + 7);
    const float top = (static_cast<float>(height) - 18.0f) * 0.5f;
    g.setColour(midi ? juce::Colour(0xffed9844) : juce::Colour(0xff3b82f6));
    g.fillRoundedRectangle(left, top, 18.0f, 18.0f, 4.0f);
    g.setColour(juce::Colours::white);
    if (midi)
    {
        g.fillRect(left + 3.0f, top + 6.0f, 12.0f, 7.0f);
        g.setColour(juce::Colour(0xffed9844));
        for (int key = 1; key < 4; ++key)
            g.fillRect(left + 3.0f + 3.0f * static_cast<float>(key), top + 9.0f, 1.0f, 4.0f);
        g.fillRect(left + 5.0f, top + 6.0f, 2.0f, 4.0f);
        g.fillRect(left + 11.0f, top + 6.0f, 2.0f, 4.0f);
    }
    else
    {
        const float bars[] { 4.0f, 9.0f, 13.0f, 7.0f, 4.0f };
        for (int i = 0; i < 5; ++i)
            g.fillRoundedRectangle(left + 3.0f + 2.5f * static_cast<float>(i),
                top + (18.0f - bars[i]) * 0.5f, 1.5f, bars[i], 0.7f);
    }
}

void instrumentIcon(juce::Graphics& g, int x, int height)
{
    const float left = static_cast<float>(x + 7);
    const float top = (static_cast<float>(height) - 14.0f) * 0.5f;
    g.setColour(juce::Colour(0xffdce4eb));
    g.fillRoundedRectangle(left, top, 18.0f, 14.0f, 1.5f);
    g.setColour(juce::Colour(0xff26313b));
    for (int key = 1; key < 5; ++key)
        g.fillRect(left + 3.6f * static_cast<float>(key), top + 7.0f, 1.0f, 6.0f);
    for (const float key : { 3.0f, 6.6f, 13.8f })
        g.fillRect(left + key, top, 2.5f, 8.0f);
}

void taskIcon(juce::Graphics& g, int x, int height, bool checkpoint)
{
    const auto top = (static_cast<float>(height) - 18.0f) * 0.5f;
    g.setColour(juce::Colour(0xff8795a3));
    if (checkpoint)
    {
        g.drawEllipse(static_cast<float>(x + 8), top + 2.0f, 14.0f, 14.0f, 1.5f);
        juce::Path tick;
        tick.startNewSubPath(static_cast<float>(x + 11), top + 9.0f);
        tick.lineTo(static_cast<float>(x + 14), top + 12.0f);
        tick.lineTo(static_cast<float>(x + 19), top + 6.0f);
        g.strokePath(tick, juce::PathStrokeType(1.7f));
        return;
    }
    g.drawRoundedRectangle(static_cast<float>(x + 7), top, 18.0f, 18.0f, 2.5f, 1.5f);
    for (int i = 0; i < 3; ++i)
    {
        const auto y = top + 4.5f + 5.0f * static_cast<float>(i);
        g.fillRect(static_cast<float>(x + 10), y, 2.5f, 2.5f);
        g.fillRect(static_cast<float>(x + 15), y + 0.7f, 7.0f, 1.3f);
    }
}

void songTypeIcon(juce::Graphics& g, int x, int height, arranger::SongType type)
{
    const float left = static_cast<float>(x + 8);
    const float top = (static_cast<float>(height) - 18.0f) * 0.5f;
    switch (type)
    {
        case arranger::SongType::beginning:
            g.setColour(juce::Colour(0xffe15a5c));
            g.fillEllipse(left, top, 18.0f, 18.0f);
            break;
        case arranger::SongType::rough:
        {
            g.setColour(juce::Colour(0xfff1c84b));
            g.fillEllipse(left + 3.0f, top + 4.5f, 12.0f, 9.0f);
            juce::Path tail;
            tail.addTriangle(left + 3.0f, top + 9.0f, left - 1.0f, top + 4.0f,
                left - 1.0f, top + 14.0f);
            g.fillPath(tail);
            g.setColour(juce::Colour(0xff29333d));
            g.fillEllipse(left + 12.0f, top + 7.5f, 1.8f, 1.8f);
            break;
        }
        case arranger::SongType::mixing:
        {
            g.setColour(juce::Colour(0xff438ce5));
            g.fillRoundedRectangle(left, top, 18.0f, 18.0f, 3.0f);
            g.setColour(juce::Colours::white);
            for (int i = 0; i < 3; ++i)
            {
                const float lineX = left + 4.5f + static_cast<float>(i) * 4.5f;
                g.fillRect(lineX, top + 3.0f, 1.0f, 12.0f);
                g.fillRoundedRectangle(lineX - 1.3f, top + 5.0f + static_cast<float>((i * 4) % 7),
                    3.6f, 2.5f, 1.0f);
            }
            break;
        }
        case arranger::SongType::finalMix:
        {
            g.setColour(juce::Colour(0xff34a878));
            g.fillEllipse(left, top, 18.0f, 18.0f);
            juce::Path play;
            play.addTriangle(left + 7.0f, top + 4.5f, left + 7.0f, top + 13.5f,
                left + 13.0f, top + 9.0f);
            g.setColour(juce::Colours::white);
            g.fillPath(play);
            break;
        }
        default:
            g.setColour(juce::Colour(0xff8795a3));
            g.drawEllipse(left + 1.0f, top + 1.0f, 16.0f, 16.0f, 1.3f);
            break;
    }
}

juce::Colour savedColour(const juce::String& saved)
{
    if (const auto argb = arranger::studioProColour(saved.toStdString()))
        return juce::Colour(static_cast<juce::uint32>(*argb));
    return juce::Colour(0xff586d83);
}

void progressBar(juce::Graphics& g, int x, int height, int width, int done, int total)
{
    g.setColour(juce::Colour(0xffaebdca));
    g.setFont(juce::FontOptions(11.0f));
    if (total == 0)
    {
        g.drawText("-", juce::Rectangle<int> {x, 0, width, height}, juce::Justification::centredLeft);
        return;
    }
    g.drawText(juce::String(done) + "/" + juce::String(total), juce::Rectangle<int> {x, 1, width, 17},
        juce::Justification::centredLeft);
    const auto bar = juce::Rectangle<float> {static_cast<float>(x), static_cast<float>(height - 12),
        static_cast<float>(width), 5.0f};
    g.setColour(juce::Colour(0xff49545e));
    g.fillRoundedRectangle(bar, 2.5f);
    if (done > 0)
    {
        g.setColour(juce::Colour(0xff34a878));
        g.fillRoundedRectangle(bar.withWidth(bar.getWidth() * static_cast<float>(done) / static_cast<float>(total)), 2.5f);
    }
}

template <typename Items>
std::vector<size_t> orderByStart(const Items& items)
{
    std::vector<size_t> order(items.size());
    std::iota(order.begin(), order.end(), size_t{0});
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b)
    {
        return items[a].start.getDoubleValue() < items[b].start.getDoubleValue();
    });
    return order;
}
}

HubEditor::HubEditor(std::function<juce::String()> getPath,
    std::function<void(juce::String)> setPath, arranger::SongCatalog* songCatalog,
    std::function<void()> onSaveCatalog)
    : getProjectPath(std::move(getPath)), setProjectPath(std::move(setPath)),
      catalog(songCatalog), saveCatalog(std::move(onSaveCatalog))
{
    heading.setText("Arranger Manager 0.1h", juce::dontSendNotification);
    heading.setFont(juce::FontOptions(21.0f, juce::Font::bold));
    addAndMakeVisible(heading);
    for (auto* label : { &path, &summary, &info, &notes, &help })
    {
        label->setFont(juce::FontOptions(14.0f));
        label->setColour(juce::Label::textColourId, juce::Colour(0xffdce4eb));
        addAndMakeVisible(*label);
    }
    help.setText(catalog != nullptr ? "Click Notes/Type to edit; double-click task name; drag tasks to reorder."
        : "Saved project snapshot - Save in Studio Pro to update", juce::dontSendNotification);
    help.setColour(juce::Label::textColourId, juce::Colour(0xffaebdca));
    path.setText("No project selected", juce::dontSendNotification);
    summary.setText("Open the .song file of your current project", juce::dontSendNotification);
    if (catalog != nullptr) choose.setButtonText("Add .song");
    choose.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser>("Select a Studio Pro song", juce::File(), "*.song");
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
            | (catalog != nullptr ? juce::FileBrowserComponent::canSelectMultipleItems : 0),
            [safe = juce::Component::SafePointer<HubEditor>(this)](const juce::FileChooser& selected)
            {
                if (safe == nullptr) return;
                if (safe->catalog != nullptr)
                {
                    juce::String lastAdded;
                    for (const auto& file : selected.getResults())
                        if (safe->catalog->addSong(file.getFullPathName(), safe->selectedFolderId))
                            lastAdded = file.getFullPathName();
                    if (lastAdded.isNotEmpty())
                    {
                        safe->setProjectPath(lastAdded);
                        safe->expandedSongs.insert("project:" + lastAdded.toStdString());
                        safe->catalogChanged();
                    }
                }
                else
                {
                    const auto file = selected.getResult();
                    if (file == juce::File()) return;
                    safe->setProjectPath(file.getFullPathName());
                }
                safe->refreshSnapshot();
            });
    };
    newFolder.onClick = [this] { promptNewFolder(); };
    if (catalog != nullptr) addAndMakeVisible(newFolder);
    refresh.onClick = [this] { refreshSnapshot(); };
    addAndMakeVisible(choose);
    addAndMakeVisible(refresh);
    list.setRowHeight(36);
    list.setColour(juce::ListBox::backgroundColourId, juce::Colour(0xff222b33));
    addAndMakeVisible(list);
    inlineEditor.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff354554));
    inlineEditor.setColour(juce::TextEditor::textColourId, juce::Colours::white);
    inlineEditor.onFocusLost = [this] { finishInlineEdit(true); };
    inlineEditor.onEscapeKey = [this] { finishInlineEdit(false); };
    inlineEditor.onReturnKey = [this]
    {
        if (editKind == EditKind::name) finishInlineEdit(true);
    };
    setSize(980, 620);
    timerCallback();
    startTimerHz(1);
}

HubEditor::~HubEditor() { stopTimer(); }

void HubEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff1d232b));
    if (list.getWidth() == 0) return;
    const auto header = juce::Rectangle<int> {list.getX(), list.getY() - 27, list.getWidth(), 26};
    g.setColour(juce::Colour(0xff2a343e));
    g.fillRect(header);
    g.setColour(juce::Colour(0xffaebdca));
    g.setFont(juce::FontOptions(12.0f, juce::Font::bold));
    const auto layout = columnsFor(list.getWidth());
    g.drawText("NAME", juce::Rectangle<int> {header.getX() + 12, header.getY(), layout.nameWidth - 12, header.getHeight()},
        juce::Justification::centredLeft);
    g.drawText("TYPE", juce::Rectangle<int> {header.getX() + layout.type, header.getY(), 40, header.getHeight()},
        juce::Justification::centredLeft);
    g.drawText("PARTS", juce::Rectangle<int> {header.getX() + layout.parts, header.getY(), layout.partsWidth, header.getHeight()},
        juce::Justification::centredLeft);
    g.drawText("STATUS", juce::Rectangle<int> {header.getX() + layout.status, header.getY(), layout.statusWidth, header.getHeight()},
        juce::Justification::centredLeft);
    g.drawText("PROG", juce::Rectangle<int> {header.getX() + layout.prog, header.getY(), layout.progWidth, header.getHeight()},
        juce::Justification::centredLeft);
    g.drawText("NOTES", juce::Rectangle<int> {header.getX() + layout.notes, header.getY(), layout.notesWidth, header.getHeight()},
        juce::Justification::centredLeft);
}

void HubEditor::resized()
{
    if (getWidth() < 400 || getHeight() < 250) return;
    auto area = getLocalBounds().reduced(16);
    const bool compact = catalog != nullptr && getWidth() < 760;
    auto header = area.removeFromTop(compact ? 80 : 44);
    if (compact)
    {
        heading.setBounds(header.removeFromTop(36));
        refresh.setBounds(header.removeFromRight(90).reduced(3));
        choose.setBounds(header.removeFromRight(130).reduced(3));
        newFolder.setBounds(header.removeFromRight(110).reduced(3));
    }
    else
    {
        refresh.setBounds(header.removeFromRight(90).reduced(3));
        choose.setBounds(header.removeFromRight(130).reduced(3));
        if (catalog != nullptr) newFolder.setBounds(header.removeFromRight(110).reduced(3));
        heading.setBounds(header);
    }
    path.setBounds(area.removeFromTop(25));
    summary.setBounds(area.removeFromTop(27));
    info.setBounds(area.removeFromTop(25));
    notes.setBounds(area.removeFromTop(25));
    help.setBounds(area.removeFromTop(23));
    area.removeFromTop(32);
    list.setBounds(area);
    positionInlineEditor();
    repaint();
}

void HubEditor::timerCallback()
{
    if (catalog != nullptr)
    {
        if (lastPath != getProjectPath() || songCache.size() != catalog->songs.size())
        {
            refreshSnapshot();
            return;
        }
        for (const auto& song : catalog->songs)
        {
            const juce::File file(song.path);
            const auto key = song.path.toStdString();
            const auto found = songCache.find(key);
            if (found == songCache.end() || found->second.modified != (file.existsAsFile() ? file.getLastModificationTime().toMilliseconds() : -1)
                || found->second.size != (file.existsAsFile() ? file.getSize() : -1))
            {
                refreshSnapshot();
                return;
            }
        }
        return;
    }
    const auto current = getProjectPath();
    if (current.isEmpty()) return;
    const juce::File file(current);
    const auto modified = file.existsAsFile() ? file.getLastModificationTime().toMilliseconds() : -1;
    const auto size = file.existsAsFile() ? file.getSize() : -1;
    if (current != lastPath || modified != lastModified || size != lastSize) refreshSnapshot();
}

void HubEditor::refreshSnapshot()
{
    finishInlineEdit(true);
    if (catalog != nullptr) { refreshCatalog(); return; }
    const auto current = getProjectPath();
    if (current != lastPath) collapsed.clear();
    lastPath = current;
    snapshot = {};
    rows.clear();
    if (lastPath.isEmpty())
    {
        path.setText("No project selected", juce::dontSendNotification);
        summary.setText("Open the .song file of your current project", juce::dontSendNotification);
        info.setText({}, juce::dontSendNotification);
        notes.setText({}, juce::dontSendNotification);
        list.updateContent();
        return;
    }
    const juce::File file(lastPath);
    lastModified = file.existsAsFile() ? file.getLastModificationTime().toMilliseconds() : -1;
    lastSize = file.existsAsFile() ? file.getSize() : -1;
    path.setText(file.getFullPathName(), juce::dontSendNotification);
    snapshot = arranger::readSongSnapshot(file);
    if (!snapshot.ok())
    {
        summary.setText("Cannot read project: " + snapshot.error, juce::dontSendNotification);
        info.setText({}, juce::dontSendNotification);
        notes.setText({}, juce::dontSendNotification);
    }
    else
    {
        int audio = 0, midi = 0;
        for (const auto& track : snapshot.tracks)
            if (arranger::managedTrack(track))
                for (const auto& event : track.events)
                {
                    if (event.type == "AudioEvent") ++audio;
                    if (event.type == "MusicPart") ++midi;
                }
        summary.setText(snapshot.clipRangeWarning.isNotEmpty() ? snapshot.clipRangeWarning
            : snapshot.documentTitle + "  |  " + juce::String(arranger::managedTrackCount(snapshot)) + " tracks  |  "
                + juce::String(audio) + " audio  |  " + juce::String(midi) + " MIDI", juce::dontSendNotification);
        info.setText("Info: " + snapshot.mediaTitle + (snapshot.artist.isNotEmpty() ? "  -  " + snapshot.artist : ""),
            juce::dontSendNotification);
        notes.setText("Notes: " + snapshot.notes.replaceCharacters("\r\n", "  "), juce::dontSendNotification);
        rebuildRows();
    }
    list.updateContent();
    list.repaint();
}

void HubEditor::refreshCatalog()
{
    finishInlineEdit(true);
    lastPath = getProjectPath();
    auto targetName = juce::String("Songs");
    for (const auto& folder : catalog->folders)
        if (folder.id == selectedFolderId) { targetName = folder.name; break; }
    help.setText("Add .song to: " + targetName + "  |  Right-click song/task for tasks and checkpoints",
        juce::dontSendNotification);
    std::set<std::string> paths;
    for (const auto& song : catalog->songs)
    {
        const auto key = song.path.toStdString();
        paths.insert(key);
        const juce::File file(song.path);
        const auto modified = file.existsAsFile() ? file.getLastModificationTime().toMilliseconds() : -1;
        const auto size = file.existsAsFile() ? file.getSize() : -1;
        auto found = songCache.find(key);
        if (found == songCache.end() || found->second.modified != modified || found->second.size != size)
            songCache[key] = {arranger::readSongSnapshot(file), modified, size};
    }
    for (auto it = songCache.begin(); it != songCache.end();)
        it = paths.contains(it->first) ? std::next(it) : songCache.erase(it);

    snapshot = {};
    if (const auto found = songCache.find(lastPath.toStdString()); found != songCache.end())
        snapshot = found->second.snapshot;
    updateHeader(lastPath);
    rebuildRows();
    list.updateContent();
    list.repaint();
}

void HubEditor::updateHeader(const juce::String& selectedPath)
{
    if (selectedPath.isEmpty() || !songCache.contains(selectedPath.toStdString()))
    {
        path.setText("Song catalog", juce::dontSendNotification);
        summary.setText(juce::String(static_cast<int>(catalog->songs.size())) + " songs  |  "
            + juce::String(static_cast<int>(catalog->folders.size())) + " folders", juce::dontSendNotification);
        info.setText("Add a saved .song file to begin", juce::dontSendNotification);
        notes.setText({}, juce::dontSendNotification);
        return;
    }
    path.setText(selectedPath, juce::dontSendNotification);
    if (!snapshot.ok())
    {
        summary.setText("Cannot read project: " + snapshot.error, juce::dontSendNotification);
        info.setText({}, juce::dontSendNotification);
        notes.setText({}, juce::dontSendNotification);
        return;
    }
    int audio = 0, midi = 0;
    for (const auto& track : snapshot.tracks)
        if (arranger::managedTrack(track))
            for (const auto& event : track.events)
            {
                if (event.type == "AudioEvent") ++audio;
                if (event.type == "MusicPart") ++midi;
            }
    summary.setText(snapshot.clipRangeWarning.isNotEmpty() ? snapshot.clipRangeWarning
        : snapshot.documentTitle + "  |  " + juce::String(arranger::managedTrackCount(snapshot)) + " tracks  |  "
            + juce::String(audio) + " audio  |  " + juce::String(midi) + " MIDI", juce::dontSendNotification);
    info.setText("Info: " + snapshot.mediaTitle + (snapshot.artist.isNotEmpty() ? "  -  " + snapshot.artist : ""),
        juce::dontSendNotification);
    notes.setText("Notes: " + snapshot.notes.replaceCharacters("\r\n", "  "), juce::dontSendNotification);
}

void HubEditor::catalogChanged()
{
    if (saveCatalog) saveCatalog();
    refreshCatalog();
}

void HubEditor::rebuildRows()
{
    rows.clear();
    if (catalog == nullptr)
    {
        if (snapshot.ok()) appendSongRows(snapshot, 0, "single", lastPath, false);
        return;
    }

    auto addFolder = [this](const juce::String& id, const juce::String& name)
    {
        const auto key = "folder:" + id.toStdString();
        int count = 0;
        for (const auto& song : catalog->songs)
            if (song.folderId == id)
                ++count;
        const auto [done, total] = catalog->folderProgress(id);
        Row folder {RowKind::folder, name, juce::String(count) + " songs", {}, {}, {}, key,
            0, true, !collapsed.contains(key)};
        folder.folderId = id;
        folder.done = done;
        folder.total = total;
        rows.push_back(std::move(folder));
        if (collapsed.contains(key)) return;
        for (const auto& song : catalog->songs)
            if (song.folderId == id)
            {
                const auto found = songCache.find(song.path.toStdString());
                if (found != songCache.end())
                    appendSongRows(found->second.snapshot, 1, song.path.toStdString(), song.path, true);
            }
    };
    addFolder({}, "Songs");
    for (const auto& folder : catalog->folders) addFolder(folder.id, folder.name);
}

void HubEditor::appendSongRows(const arranger::SongSnapshot& song, int depth,
    const std::string& key, const juce::String& songPath, bool catalogProject)
{
    const auto projectKey = catalogProject ? "project:" + key : std::string("project");
    const bool expanded = catalogProject ? expandedSongs.contains(projectKey) : !collapsed.contains(projectKey);
    const auto title = song.documentTitle.isNotEmpty() ? song.documentTitle
        : songPath.isNotEmpty() ? juce::File(songPath).getFileNameWithoutExtension() : juce::String("Project");
    int managedClips = 0;
    for (const auto& track : song.tracks)
        if (arranger::managedTrack(track)) managedClips += static_cast<int>(track.events.size());
    Row project {RowKind::project, title,
        song.ok() ? juce::String(arranger::managedTrackCount(song)) + " tracks / "
            + juce::String(managedClips) + " clips" : song.error,
        {}, {}, {}, projectKey, depth, song.ok(), expanded};
    const auto* catalogSong = catalogProject ? catalog->findSong(songPath) : nullptr;
    if (catalogSong != nullptr && !catalogSong->tasks.empty()) project.expandable = true;
    const auto [projectDone, projectTotal] = arranger::songProgress(song, catalogSong);
    project.done = projectDone;
    project.total = projectTotal;
    project.songPath = songPath;
    if (catalogProject) project.manualStatus = catalog->songStatus(songPath);
    if (catalogSong != nullptr)
    {
        project.songType = catalogSong->type;
        project.noteKey = arranger::songNoteKey();
        project.notes = catalog->localNote(songPath, project.noteKey);
    }
    rows.push_back(std::move(project));
    if (!expanded) return;

    if (catalogSong != nullptr)
        for (const auto& task : catalogSong->tasks)
        {
            const auto taskKey = projectKey + ":task:" + task.id.toStdString();
            const bool open = !collapsed.contains(taskKey);
            Row item {RowKind::task, task.name, {}, {}, {}, {}, taskKey,
                depth + 1, !task.checkpoints.empty(), open};
            item.songPath = songPath;
            item.taskId = task.id;
            item.noteKey = arranger::taskNoteKey(task.id);
            item.notes = catalog->localNote(songPath, item.noteKey);
            item.manualStatus = task.status;
            std::tie(item.done, item.total) = task.progress();
            rows.push_back(std::move(item));
            if (open)
                for (const auto& checkpoint : task.checkpoints)
                {
                    Row child {RowKind::checkpoint, checkpoint.name, {}, {}, {}, {},
                        taskKey + ":checkpoint:" + checkpoint.id.toStdString(), depth + 2};
                    child.songPath = songPath;
                    child.taskId = task.id;
                    child.checkpointId = checkpoint.id;
                    child.noteKey = arranger::checkpointNoteKey(checkpoint.id);
                    child.notes = catalog->localNote(songPath, child.noteKey);
                    child.manualStatus = checkpoint.status;
                    rows.push_back(std::move(child));
                }
        }
    if (!song.ok()) return;

    auto addClip = [this, &song, &songPath](const arranger::SongEvent& event,
        int clipDepth, const juce::String& noteKey)
    {
        const auto tag = arranger::parse(event.name.toStdString());
        const auto fallback = event.type == "MusicPart" ? "MIDI" : "Audio";
        Row row { RowKind::clip, juce::String::fromUTF8(
                arranger::displayClipName(event.name.toStdString(), fallback).c_str()),
            {}, {},
            {}, tag, {}, clipDepth };
        row.midi = event.type == "MusicPart";
        row.songPath = songPath;
        for (auto index : arranger::matchingSectionIndices(song, event))
        {
            const auto& section = song.sections[index];
            row.sections.push_back({section.name, savedColour(section.color)});
        }
        row.noteKey = noteKey;
        if (catalog != nullptr) row.notes = catalog->localNote(songPath, noteKey);
        rows.push_back(std::move(row));
    };

    auto appendTrack = [&](size_t i, int trackDepth)
    {
        const auto& track = song.tracks[i];
        const auto trackName = arranger::parseTrackName(track.name.toStdString());
        if (trackName.status == arranger::Status::unmarked) return;
        const auto trackNoteKey = arranger::trackNoteKey(track, i);
        const std::string trackKey = (catalogProject ? key + ":" : std::string()) + "track:"
            + (track.id.isNotEmpty() ? track.id.toStdString() : std::to_string(i));
        const auto trackOpen = catalogProject ? expandedChildren.contains(trackKey) : !collapsed.contains(trackKey);
        const auto displayName = trackName.name.empty() ? juce::String("Track")
            : juce::String::fromUTF8(trackName.name.c_str());
        rows.push_back({RowKind::track, displayName,
            juce::String(static_cast<int>(track.events.size())) + " clips", {}, {}, {},
            trackKey, trackDepth, !track.events.empty(), trackOpen});
        rows.back().noteKey = trackNoteKey;
        if (catalog != nullptr) rows.back().notes = catalog->localNote(songPath, trackNoteKey);
        rows.back().trackColour = savedColour(track.color);
        rows.back().mediaType = track.mediaType;
        rows.back().songPath = songPath;
        rows.back().manualStatus = trackName.status;
        const auto [done, total] = arranger::clipProgress(track.events);
        rows.back().done = done;
        rows.back().total = total;
        if (!trackOpen) return;
        for (auto index : orderByStart(track.events))
        {
            const auto& event = track.events[index];
            const auto base = arranger::clipNoteBaseKey(trackNoteKey, event);
            size_t occurrence = 0;
            for (size_t previous = 0; previous < index; ++previous)
                if (arranger::clipNoteBaseKey(trackNoteKey, track.events[previous]) == base)
                    ++occurrence;
            addClip(event, trackDepth + 1, arranger::clipNoteKey(trackNoteKey, event, occurrence));
        }
    };

    std::map<juce::String, size_t> folderIds;
    for (size_t i = 0; i < song.trackFolders.size(); ++i)
        if (song.trackFolders[i].id.isNotEmpty())
            folderIds.try_emplace(song.trackFolders[i].id, i);
    auto parentOf = [&](const juce::String& id) -> juce::String
    {
        return folderIds.contains(id) ? id : juce::String();
    };
    std::set<size_t> visitedFolders;
    std::function<void(const juce::String&, int)> appendChildren;
    appendChildren = [&](const juce::String& parent, int childDepth)
    {
        for (const auto& entry : song.trackOrder)
        {
            if (entry.folder)
            {
                if (entry.index >= song.trackFolders.size()) continue;
                const auto& folder = song.trackFolders[entry.index];
                if (parentOf(folder.parentFolder) != parent
                    || !arranger::folderContainsManagedTrack(song, folder.id)
                    || !visitedFolders.insert(entry.index).second) continue;
                const auto folderKey = key + ":trackfolder:"
                    + (folder.id.isNotEmpty() ? folder.id.toStdString() : std::to_string(entry.index));
                const bool open = !collapsed.contains(folderKey);
                Row item {RowKind::trackFolder, folder.name.isNotEmpty() ? folder.name : juce::String("Folder"),
                    {}, {}, {}, {}, folderKey, childDepth, true, open};
                item.songPath = songPath;
                item.trackColour = savedColour(folder.color);
                rows.push_back(std::move(item));
                if (open && folder.id.isNotEmpty()) appendChildren(folder.id, childDepth + 1);
            }
            else
            {
                if (entry.index >= song.tracks.size()) continue;
                const auto& track = song.tracks[entry.index];
                if (parentOf(track.parentFolder) == parent)
                    appendTrack(entry.index, childDepth);
            }
        }
    };
    appendChildren({}, depth + 1);
}

void HubEditor::promptNewFolder()
{
    if (catalog == nullptr) return;
    auto* dialog = new juce::AlertWindow("New folder", "Name for the song group",
        juce::MessageBoxIconType::QuestionIcon);
    dialog->addTextEditor("name", {}, "Folder name");
    dialog->addButton("Create", 1, juce::KeyPress(juce::KeyPress::returnKey));
    dialog->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    dialog->enterModalState(true, juce::ModalCallbackFunction::create(
        [safe = juce::Component::SafePointer<HubEditor>(this), dialog](int result)
        {
            if (safe == nullptr || result != 1) return;
            const auto id = safe->catalog->addFolder(dialog->getTextEditorContents("name"));
            if (id.isNotEmpty())
            {
                safe->selectedFolderId = id;
                safe->catalogChanged();
            }
        }), true);
}

void HubEditor::showSongMenu(const juce::String& songPath, juce::Point<int> screen)
{
    juce::PopupMenu menu;
    menu.addItem(2000, "New task...");
    menu.addItem(2001, "Edit local note...");
    menu.addSeparator();
    menu.addItem(1, "Move to Songs (root)");
    for (size_t i = 0; i < catalog->folders.size(); ++i)
        menu.addItem(static_cast<int>(i) + 10, "Move to " + catalog->folders[i].name);
    menu.addSeparator();
    menu.addItem(1000, "Remove from catalog");
    menu.showMenuAsync(menuAt(screen, list),
        [safe = juce::Component::SafePointer<HubEditor>(this), songPath](int result)
        {
            if (safe == nullptr || result == 0) return;
            if (result == 2000) { safe->promptLocalName(songPath, {}); return; }
            if (result == 2001)
            {
                const auto found = std::find_if(safe->rows.begin(), safe->rows.end(), [&](const Row& row)
                { return row.kind == RowKind::project && row.songPath.equalsIgnoreCase(songPath); });
                if (found != safe->rows.end()) safe->promptLocalNote(*found);
                return;
            }
            if (result == 1000)
            {
                safe->catalog->removeSong(songPath);
                if (safe->getProjectPath().equalsIgnoreCase(songPath))
                    safe->setProjectPath(safe->catalog->songs.empty() ? juce::String() : safe->catalog->songs.front().path);
            }
            else
            {
                const auto folderId = result == 1 ? juce::String()
                    : (result >= 10 && static_cast<size_t>(result - 10) < safe->catalog->folders.size()
                        ? safe->catalog->folders[static_cast<size_t>(result - 10)].id : juce::String());
                if (result != 1 && folderId.isEmpty()) return;
                safe->catalog->moveSong(songPath, folderId);
                safe->selectedFolderId = folderId;
                safe->collapsed.erase("folder:" + folderId.toStdString());
            }
            safe->catalogChanged();
        });
}

void HubEditor::showFolderMenu(const juce::String& folderId, juce::Point<int> screen)
{
    if (folderId.isEmpty()) return;
    juce::PopupMenu menu;
    menu.addItem(1, "Remove folder (keep songs)");
    menu.showMenuAsync(menuAt(screen, list),
        [safe = juce::Component::SafePointer<HubEditor>(this), folderId](int result)
        {
            if (safe == nullptr || result != 1) return;
            safe->catalog->removeFolder(folderId);
            if (safe->selectedFolderId == folderId) safe->selectedFolderId.clear();
            safe->catalogChanged();
        });
}

void HubEditor::showStatusMenu(const juce::String& songPath, juce::Point<int> screen)
{
    if (catalog == nullptr || songPath.isEmpty()) return;
    const auto current = catalog->songStatus(songPath);
    juce::PopupMenu menu;
    const arranger::Status options[] {arranger::Status::unmarked, arranger::Status::pool,
        arranger::Status::todo, arranger::Status::wip, arranger::Status::draft,
        arranger::Status::wait, arranger::Status::done, arranger::Status::blocked};
    for (int i = 0; i < static_cast<int>(std::size(options)); ++i)
        menu.addItem(i + 1, options[i] == arranger::Status::unmarked ? "Clear status" : arranger::label(options[i]),
            true, options[i] == current);
    menu.showMenuAsync(menuAt(screen, list),
        [safe = juce::Component::SafePointer<HubEditor>(this), songPath](int result)
        {
            if (safe == nullptr || result < 1 || result > 8) return;
            const arranger::Status options[] {arranger::Status::unmarked, arranger::Status::pool,
                arranger::Status::todo, arranger::Status::wip, arranger::Status::draft,
                arranger::Status::wait, arranger::Status::done, arranger::Status::blocked};
            safe->catalog->setSongStatus(songPath, options[result - 1]);
            safe->catalogChanged();
        });
}

void HubEditor::showSongTypeMenu(const Row& row, juce::Point<int> screen)
{
    if (catalog == nullptr || row.kind != RowKind::project) return;
    juce::PopupMenu menu;
    const arranger::SongType types[] {arranger::SongType::unspecified, arranger::SongType::beginning,
        arranger::SongType::rough, arranger::SongType::mixing, arranger::SongType::finalMix};
    const char* names[] {"Clear type", "Beginning", "Rough sketch", "Mixing", "Final mix"};
    for (int i = 0; i < 5; ++i)
        menu.addItem(i + 1, names[i], true, row.songType == types[i]);
    menu.showMenuAsync(menuAt(screen, list),
        [safe = juce::Component::SafePointer<HubEditor>(this), row](int result)
        {
            if (safe == nullptr || result < 1 || result > 5) return;
            const arranger::SongType options[] {arranger::SongType::unspecified,
                arranger::SongType::beginning, arranger::SongType::rough,
                arranger::SongType::mixing, arranger::SongType::finalMix};
            if (safe->catalog->setSongType(row.songPath, options[result - 1])) safe->catalogChanged();
        });
}

void HubEditor::promptLocalName(const juce::String& songPath, const juce::String& taskId,
    const juce::String& checkpointId, bool rename)
{
    const bool checkpoint = taskId.isNotEmpty() && (!rename || checkpointId.isNotEmpty());
    juce::String current;
    if (rename)
    {
        const auto* task = catalog->findTask(songPath, taskId);
        if (task == nullptr) return;
        if (!checkpoint) current = task->name;
        else
            for (const auto& cp : task->checkpoints)
                if (cp.id == checkpointId) current = cp.name;
        if (current.isEmpty()) return;
    }
    auto* dialog = new juce::AlertWindow(rename ? "Rename" : "New item",
        checkpoint ? "Checkpoint name" : "Task name", juce::MessageBoxIconType::QuestionIcon);
    dialog->addTextEditor("name", current, checkpoint ? "Checkpoint" : "Task");
    dialog->addButton(rename ? "Save" : "Create", 1, juce::KeyPress(juce::KeyPress::returnKey));
    dialog->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    dialog->enterModalState(true, juce::ModalCallbackFunction::create(
        [safe = juce::Component::SafePointer<HubEditor>(this), dialog,
            songPath, taskId, checkpointId, checkpoint, rename](int result)
        {
            if (safe == nullptr || result != 1) return;
            const auto name = dialog->getTextEditorContents("name");
            bool changed = false;
            if (rename)
                changed = checkpoint
                    ? safe->catalog->editCheckpoint(songPath, taskId, checkpointId, name)
                    : safe->catalog->editTask(songPath, taskId, name);
            else if (checkpoint)
                changed = safe->catalog->addCheckpoint(songPath, taskId, name).isNotEmpty();
            else
                changed = safe->catalog->addTask(songPath, name).isNotEmpty();
            if (changed)
            {
                safe->expandedSongs.insert("project:" + songPath.toStdString());
                safe->catalogChanged();
            }
        }), true);
}

void HubEditor::showLocalStatusMenu(const Row& row, juce::Point<int> screen)
{
    juce::PopupMenu menu;
    const arranger::Status options[] {arranger::Status::pool, arranger::Status::todo,
        arranger::Status::wip, arranger::Status::draft, arranger::Status::wait,
        arranger::Status::done, arranger::Status::blocked};
    for (int i = 0; i < static_cast<int>(std::size(options)); ++i)
        menu.addItem(i + 1, arranger::label(options[i]), true, options[i] == row.manualStatus);
    menu.showMenuAsync(menuAt(screen, list),
        [safe = juce::Component::SafePointer<HubEditor>(this), row](int result)
        {
            if (safe == nullptr || result < 1 || result > 7) return;
            const arranger::Status statuses[] {arranger::Status::pool, arranger::Status::todo,
                arranger::Status::wip, arranger::Status::draft, arranger::Status::wait,
                arranger::Status::done, arranger::Status::blocked};
            const auto status = statuses[result - 1];
            const bool changed = row.kind == RowKind::task
                ? safe->catalog->setTaskStatus(row.songPath, row.taskId, status)
                : safe->catalog->setCheckpointStatus(row.songPath, row.taskId, row.checkpointId, status);
            if (changed) safe->catalogChanged();
        });
}

void HubEditor::showTaskMenu(const Row& row, juce::Point<int> screen)
{
    juce::PopupMenu menu;
    if (row.kind == RowKind::task) menu.addItem(1, "New checkpoint...");
    menu.addItem(2, "Rename...");
    menu.addItem(4, "Edit local note...");
    menu.addSeparator();
    menu.addItem(3, row.kind == RowKind::task ? "Delete task..." : "Delete checkpoint...");
    menu.showMenuAsync(menuAt(screen, list),
        [safe = juce::Component::SafePointer<HubEditor>(this), row](int result)
        {
            if (safe == nullptr || result == 0) return;
            if (result == 1) { safe->promptLocalName(row.songPath, row.taskId); return; }
            if (result == 2)
            {
                safe->promptLocalName(row.songPath, row.taskId, row.checkpointId, true);
                return;
            }
            if (result == 4) { safe->promptLocalNote(row); return; }
            if (result != 3) return;
            auto* dialog = new juce::AlertWindow("Delete local item",
                row.kind == RowKind::task ? "Delete task and all its checkpoints?"
                    : "Delete checkpoint?", juce::MessageBoxIconType::WarningIcon);
            dialog->addButton("Delete", 1);
            dialog->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
            dialog->enterModalState(true, juce::ModalCallbackFunction::create(
                [safe, row](int answer)
                {
                    if (safe == nullptr || answer != 1) return;
                    const bool changed = row.kind == RowKind::task
                        ? safe->catalog->removeTask(row.songPath, row.taskId)
                        : safe->catalog->removeCheckpoint(row.songPath, row.taskId, row.checkpointId);
                    if (changed) safe->catalogChanged();
                }), true);
        });
}

void HubEditor::showTrackOrClipMenu(const Row& row, juce::Point<int> screen)
{
    juce::PopupMenu menu;
    menu.addItem(1, "Edit local note...");
    menu.showMenuAsync(menuAt(screen, list),
        [safe = juce::Component::SafePointer<HubEditor>(this), row](int result)
        {
            if (safe != nullptr && result == 1) safe->promptLocalNote(row);
        });
}

void HubEditor::promptLocalNote(const Row& row)
{
    if (catalog == nullptr || row.songPath.isEmpty() || row.noteKey.isEmpty()) return;
    auto* dialog = new juce::AlertWindow("Local note", "Note for " + row.title,
        juce::MessageBoxIconType::QuestionIcon);
    // AlertWindow's built-in text editor is fixed to a one-line height.
    auto editor = std::make_shared<juce::TextEditor>("Note");
    editor->setMultiLine(true);
    editor->setReturnKeyStartsNewLine(true);
    editor->setText(catalog->localNote(row.songPath, row.noteKey));
    editor->setSize(480, 120);
    dialog->addCustomComponent(editor.get());
    dialog->addButton("Save", 1);
    dialog->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    dialog->enterModalState(true, juce::ModalCallbackFunction::create(
        [safe = juce::Component::SafePointer<HubEditor>(this), editor, row](int result)
        {
            if (safe == nullptr || result != 1) return;
            if (safe->catalog->setLocalNote(row.songPath, row.noteKey, editor->getText()))
                safe->catalogChanged();
        }), true);
}

void HubEditor::beginInlineEdit(int index, EditKind kind)
{
    if (catalog == nullptr || index < 0 || index >= static_cast<int>(rows.size())) return;
    finishInlineEdit(true);
    const auto& row = rows[static_cast<size_t>(index)];
    if (kind == EditKind::note && row.noteKey.isEmpty()) return;
    if (kind == EditKind::name && row.kind != RowKind::task && row.kind != RowKind::checkpoint) return;
    auto* content = list.getViewport() != nullptr ? list.getViewport()->getViewedComponent() : nullptr;
    if (content == nullptr) return;
    editRow = row;
    editIndex = index;
    editKind = kind;
    inlineEditor.setMultiLine(kind == EditKind::note);
    inlineEditor.setReturnKeyStartsNewLine(kind == EditKind::note);
    inlineEditor.setText(kind == EditKind::note ? row.notes : row.title, false);
    content->addAndMakeVisible(inlineEditor);
    positionInlineEditor();
    inlineEditor.toFront(false);
    inlineEditor.grabKeyboardFocus();
    if (kind == EditKind::name) inlineEditor.selectAll();
    else inlineEditor.moveCaretToEnd();
}

void HubEditor::finishInlineEdit(bool save)
{
    if (editKind == EditKind::none) return;
    const auto kind = editKind;
    const auto row = editRow;
    const auto value = inlineEditor.getText();
    editKind = EditKind::none;
    editIndex = -1;
    inlineEditor.setVisible(false);
    if (!save || catalog == nullptr) return;
    bool changed = false;
    if (kind == EditKind::note && value.trim() != row.notes.trim())
        changed = catalog->setLocalNote(row.songPath, row.noteKey, value);
    else if (kind == EditKind::name && value.trim().isNotEmpty() && value.trim() != row.title)
        changed = row.kind == RowKind::task
            ? catalog->editTask(row.songPath, row.taskId, value)
            : catalog->editCheckpoint(row.songPath, row.taskId, row.checkpointId, value);
    if (changed) catalogChanged();
}

void HubEditor::positionInlineEditor()
{
    if (editKind == EditKind::none) return;
    const auto layout = columnsFor(list.getWidth());
    int x = layout.notes;
    int width = layout.notesWidth - 3;
    if (editKind == EditKind::name)
    {
        const auto& row = editRow;
        x = 10 + row.depth * 19 + 18 + 19;
        width = layout.nameWidth - x;
    }
    inlineEditor.setBounds(x, editIndex * list.getRowHeight() + 2, std::max(30, width),
        list.getRowHeight() - 4);
    inlineEditor.toFront(false);
}

void HubEditor::listWasScrolled()
{
    positionInlineEditor();
}

juce::var HubEditor::getDragSourceDescription(const juce::SparseSet<int>& selected)
{
    if (catalog == nullptr || selected.size() != 1) return {};
    const auto index = selected[0];
    if (index < 0 || index >= static_cast<int>(rows.size())) return {};
    const auto& row = rows[static_cast<size_t>(index)];
    return row.kind == RowKind::task || row.kind == RowKind::checkpoint
        ? juce::var(juce::String(row.key)) : juce::var();
}

bool HubEditor::dropTarget(const juce::String& sourceKey, int x, int y,
    int& targetIndex, bool& after) const
{
    const auto source = std::find_if(rows.begin(), rows.end(), [&](const Row& row)
    { return row.key == sourceKey.toStdString(); });
    targetIndex = list.getRowContainingPosition(x, y);
    if (source == rows.end() || targetIndex < 0 || targetIndex >= static_cast<int>(rows.size())) return false;
    const auto& target = rows[static_cast<size_t>(targetIndex)];
    if ((source->kind != RowKind::task && source->kind != RowKind::checkpoint)
        || target.kind != source->kind || target.key == source->key
        || !target.songPath.equalsIgnoreCase(source->songPath)
        || (source->kind == RowKind::checkpoint && target.taskId != source->taskId)) return false;
    after = y >= list.getRowPosition(targetIndex, true).getCentreY();
    return true;
}

void HubEditor::reorderDrop(const juce::String& sourceKey, int index, bool after)
{
    if (catalog == nullptr || index < 0 || index >= static_cast<int>(rows.size())) return;
    const auto source = std::find_if(rows.begin(), rows.end(), [&](const Row& row)
    { return row.key == sourceKey.toStdString(); });
    if (source == rows.end()) return;
    const auto& target = rows[static_cast<size_t>(index)];
    const bool changed = source->kind == RowKind::task
        ? catalog->moveTask(source->songPath, source->taskId, target.taskId, after)
        : catalog->moveCheckpoint(source->songPath, source->taskId,
            source->checkpointId, target.checkpointId, after);
    if (changed) catalogChanged();
}

bool HubEditor::TaskList::isInterestedInDragSource(const SourceDetails& details)
{
    return details.sourceComponent.get() == this
        && details.description.isString();
}

void HubEditor::TaskList::itemDragMove(const SourceDetails& details)
{
    int index = -1;
    bool below = false;
    if (!owner.dropTarget(details.description.toString(), details.localPosition.x,
        details.localPosition.y, index, below)) index = -1;
    if (dropRow != index || after != below)
    {
        dropRow = index;
        after = below;
        repaint();
    }
}

void HubEditor::TaskList::itemDragExit(const SourceDetails&)
{
    dropRow = -1;
    repaint();
}

void HubEditor::TaskList::itemDropped(const SourceDetails& details)
{
    int index = -1;
    bool below = false;
    const bool valid = owner.dropTarget(details.description.toString(), details.localPosition.x,
        details.localPosition.y, index, below);
    dropRow = -1;
    repaint();
    if (valid) owner.reorderDrop(details.description.toString(), index, below);
}

void HubEditor::TaskList::paintOverChildren(juce::Graphics& g)
{
    if (dropRow < 0) return;
    const auto bounds = getRowPosition(dropRow, true);
    g.setColour(juce::Colour(0xff62b5f5));
    g.fillRect(0, (after ? bounds.getBottom() : bounds.getY()) - 2, getWidth(), 3);
}

void HubEditor::listBoxItemClicked(int index, const juce::MouseEvent& event)
{
    if (index < 0 || index >= static_cast<int>(rows.size())) return;
    if (editKind != EditKind::none) finishInlineEdit(true);
    const auto& row = rows[static_cast<size_t>(index)];
    if (catalog != nullptr && row.kind == RowKind::project && !event.mods.isPopupMenu())
    {
        const auto layout = columnsFor(list.getWidth());
        if (event.x >= layout.type && event.x < layout.parts)
        {
            showSongTypeMenu(row, event.getScreenPosition());
            return;
        }
    }
    if (catalog != nullptr && (row.kind == RowKind::project || row.kind == RowKind::task
        || row.kind == RowKind::checkpoint) && !event.mods.isPopupMenu())
    {
        const auto layout = columnsFor(list.getWidth());
        if (event.x >= layout.status && event.x < layout.status + layout.statusWidth)
        {
            if (row.kind == RowKind::project) showStatusMenu(row.songPath, event.getScreenPosition());
            else showLocalStatusMenu(row, event.getScreenPosition());
            return;
        }
    }
    if (catalog != nullptr && row.noteKey.isNotEmpty() && !event.mods.isPopupMenu())
    {
        const auto layout = columnsFor(list.getWidth());
        if (event.x >= layout.notes)
        {
            beginInlineEdit(index, EditKind::note);
            return;
        }
    }
    if (catalog != nullptr && event.mods.isPopupMenu())
    {
        const auto screen = event.getScreenPosition();
        if (row.kind == RowKind::project) showSongMenu(row.songPath, screen);
        else if (row.kind == RowKind::folder) showFolderMenu(row.folderId, screen);
        else if (row.kind == RowKind::task || row.kind == RowKind::checkpoint) showTaskMenu(row, screen);
        else if (row.kind == RowKind::track || row.kind == RowKind::clip) showTrackOrClipMenu(row, screen);
        return;
    }
    const auto key = row.key;
    if (catalog != nullptr)
    {
        if (row.kind == RowKind::folder)
        {
            selectedFolderId = row.folderId;
            auto targetName = juce::String("Songs");
            for (const auto& folder : catalog->folders)
                if (folder.id == selectedFolderId) { targetName = folder.name; break; }
            help.setText("Add .song to: " + targetName + "  |  Right-click song/task for tasks and checkpoints",
                juce::dontSendNotification);
        }
        if (row.kind == RowKind::project)
        {
            const auto selectedPath = row.songPath;
            if (getProjectPath() != selectedPath) setProjectPath(selectedPath);
            if (!expandedSongs.erase(key)) expandedSongs.insert(key);
            refreshCatalog();
            return;
        }
        if (row.songPath.isNotEmpty() && getProjectPath() != row.songPath)
        {
            const auto selectedPath = row.songPath;
            setProjectPath(selectedPath);
            refreshCatalog();
            return;
        }
    }
    if (!row.expandable) return;
    if (catalog != nullptr && row.kind == RowKind::task
        && event.x >= 10 + row.depth * 19 + 18) return;
    if (catalog != nullptr && row.kind == RowKind::track)
    {
        if (!expandedChildren.erase(key)) expandedChildren.insert(key);
        rebuildRows();
        list.updateContent();
        list.repaint();
        return;
    }
    if (!collapsed.erase(key)) collapsed.insert(key);
    rebuildRows();
    list.updateContent();
    list.repaint();
}

void HubEditor::listBoxItemDoubleClicked(int index, const juce::MouseEvent& event)
{
    if (catalog == nullptr || event.mods.isPopupMenu() || index < 0
        || index >= static_cast<int>(rows.size())) return;
    const auto& row = rows[static_cast<size_t>(index)];
    if ((row.kind == RowKind::task || row.kind == RowKind::checkpoint)
        && event.x >= 10 + row.depth * 19 + 18
        && event.x < columnsFor(list.getWidth()).type)
        beginInlineEdit(index, EditKind::name);
}

void HubEditor::paintListBoxItem(int index, juce::Graphics& g, int width, int height, bool selected)
{
    if (index < 0 || index >= static_cast<int>(rows.size()) || width <= 0 || height <= 0) return;
    const auto& row = rows[static_cast<size_t>(index)];
    const bool parent = row.kind == RowKind::folder || row.kind == RowKind::project
        || row.kind == RowKind::trackFolder;
    g.fillAll(selected ? juce::Colour(0xff354554)
        : parent ? juce::Colour(0xff2b3540)
        : (index % 2 ? juce::Colour(0xff26313b) : juce::Colour(0xff222b33)));
    g.setColour(juce::Colour(0xff39434d));
    g.fillRect(0, height - 1, width, 1);
    const auto layout = columnsFor(width);
    const int x = 10 + row.depth * 19;
    if (row.expandable)
    {
        g.setColour(juce::Colour(0xffaebdca));
        g.setFont(juce::FontOptions(13.0f));
        g.drawText(row.expanded ? "v" : ">", juce::Rectangle<int> {x, 0, 15, height}, juce::Justification::centred);
    }
    // Reserve the disclosure slot for every task and track, including leaf rows.
    const int textX = x + (row.expandable || row.kind == RowKind::track
        || row.kind == RowKind::task || row.kind == RowKind::checkpoint ? 18 : 9);
    g.setColour(parent ? juce::Colours::white : juce::Colour(0xffdce4eb));
    g.setFont(juce::FontOptions(14.0f, parent ? juce::Font::bold : juce::Font::plain));
    int titleX = textX;
    if (row.kind == RowKind::folder || row.kind == RowKind::trackFolder)
    {
        g.setColour(row.kind == RowKind::folder ? juce::Colour(0xffd3a438) : row.trackColour);
        g.fillRoundedRectangle(static_cast<float>(titleX), (height - 11.0f) * 0.5f,
            15.0f, 11.0f, 2.0f);
        g.fillRect(titleX + 1, (height - 15) / 2, 7, 4);
        titleX += 22;
    }
    if (row.kind == RowKind::track || row.kind == RowKind::task)
    {
        const auto circle = juce::Rectangle<float> {static_cast<float>(titleX), (height - 12.0f) * 0.5f, 12.0f, 12.0f};
        g.setColour(row.kind == RowKind::track ? row.trackColour : juce::Colour(0xff8795a3));
        g.fillEllipse(circle);
        g.setColour(juce::Colours::white.withAlpha(0.45f));
        g.drawEllipse(circle, 1.0f);
        titleX += 19;
    }
    else if (row.kind == RowKind::checkpoint)
    {
        g.setColour(juce::Colour(0xff8795a3));
        g.fillEllipse(static_cast<float>(titleX + 3), (height - 5.0f) * 0.5f, 5.0f, 5.0f);
        titleX += 19;
    }
    g.setColour(parent ? juce::Colours::white : juce::Colour(0xffdce4eb));
    const auto title = row.title + (row.detail.isNotEmpty() ? "   " + row.detail : "");
    g.drawFittedText(title, juce::Rectangle<int> {titleX, 0, std::max(1, layout.nameWidth - titleX), height},
        juce::Justification::centredLeft, 1);

    if (row.kind == RowKind::project && catalog != nullptr)
        songTypeIcon(g, layout.type, height, row.songType);
    else if (row.kind == RowKind::clip)
    {
        mediaIcon(g, layout.type, height, row.midi);
        const int visible = std::min(static_cast<int>(row.sections.size()),
            std::max(0, (layout.partsWidth - 7) / 18));
        const bool overflow = static_cast<int>(row.sections.size()) > visible;
        const int squares = overflow ? std::max(0, visible - 1) : visible;
        for (int i = 0; i < squares; ++i)
        {
            g.setColour(row.sections[static_cast<size_t>(i)].colour);
            g.fillRoundedRectangle(static_cast<float>(layout.parts + 3 + i * 18),
                (height - 13.0f) * 0.5f, 13.0f, 13.0f, 2.0f);
        }
        if (overflow)
        {
            g.setColour(juce::Colour(0xffaebdca));
            g.setFont(juce::FontOptions(11.0f));
            g.drawText("+" + juce::String(static_cast<int>(row.sections.size()) - squares),
                juce::Rectangle<int> {layout.parts + 3 + squares * 18, 0,
                    layout.partsWidth - 3 - squares * 18, height}, juce::Justification::centredLeft);
        }
        if (row.tag.valid)
            badge(g, {layout.status, 7, layout.statusWidth, height - 14},
                statusColor(row.tag.status), arranger::label(row.tag.status));
    }
    else if (row.kind == RowKind::track)
    {
        if (row.mediaType.equalsIgnoreCase("Audio")) mediaIcon(g, layout.type, height, false);
        else instrumentIcon(g, layout.type, height);
    }
    else if (row.kind == RowKind::task || row.kind == RowKind::checkpoint)
        taskIcon(g, layout.type, height, row.kind == RowKind::checkpoint);
    if ((catalog != nullptr && (row.kind == RowKind::project || row.kind == RowKind::task
        || row.kind == RowKind::checkpoint)) || row.kind == RowKind::track)
    {
        if (row.manualStatus != arranger::Status::unmarked)
            badge(g, {layout.status, 7, layout.statusWidth, height - 14},
                statusColor(row.manualStatus), arranger::label(row.manualStatus));
        else
        {
            g.setColour(juce::Colour(0xff8394a3));
            g.setFont(juce::FontOptions(12.0f));
            g.drawText(row.kind == RowKind::project ? "+ status" : "-",
                juce::Rectangle<int> {layout.status, 0, layout.statusWidth, height},
                juce::Justification::centredLeft);
        }
    }
    if (row.kind == RowKind::folder || row.kind == RowKind::project || row.kind == RowKind::track
        || (row.kind == RowKind::task && row.total > 0))
        progressBar(g, layout.prog, height, layout.progWidth, row.done, row.total);
    if (row.notes.isNotEmpty())
    {
        g.setColour(juce::Colour(0xffdce4eb));
        g.setFont(juce::FontOptions(12.0f));
        g.drawText(row.notes.replaceCharacters("\r\n", "  "),
            juce::Rectangle<int> {layout.notes, 0, layout.notesWidth, height},
            juce::Justification::centredLeft, true);
    }
    else if (catalog != nullptr && row.noteKey.isNotEmpty())
    {
        g.setColour(juce::Colour(0xff8394a3));
        g.setFont(juce::FontOptions(12.0f));
        g.drawText("+ note", juce::Rectangle<int> {layout.notes, 0, layout.notesWidth, height},
            juce::Justification::centredLeft);
    }
}

juce::String HubEditor::getTooltipForRow(int index)
{
    if (index < 0 || index >= static_cast<int>(rows.size())) return {};
    const auto& row = rows[static_cast<size_t>(index)];
    juce::String tip;
    if (row.kind == RowKind::clip && !row.sections.empty())
    {
        tip = "Parts: ";
        for (const auto& section : row.sections)
        {
            if (tip != "Parts: ") tip += ", ";
            tip += section.name;
        }
    }
    if (row.notes.isNotEmpty())
    {
        if (tip.isNotEmpty()) tip += "\n";
        tip += row.notes;
    }
    return tip;
}

void HubEditor::openCatalogSong(const juce::String& songPath)
{
    if (catalog == nullptr || catalog->findSong(songPath) == nullptr) return;
    finishInlineEdit(true);
    if (setProjectPath) setProjectPath(songPath);
    expandedSongs.insert("project:" + songPath.toStdString());
    if (const auto* song = catalog->findSong(songPath))
        collapsed.erase("folder:" + song->folderId.toStdString());
    refreshCatalog();
    for (int i = 0; i < static_cast<int>(rows.size()); ++i)
        if (rows[static_cast<size_t>(i)].kind == RowKind::project
            && rows[static_cast<size_t>(i)].songPath.equalsIgnoreCase(songPath))
        {
            list.selectRow(i);
            list.scrollToEnsureRowIsOnscreen(i);
            break;
        }
}
