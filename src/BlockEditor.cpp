#include "BlockEditor.h"
#include "MRManagerStyle.h"

juce::String BlockEditor::tr(const char* s) { return juce::String::fromUTF8(s); }
struct BlockEditor::Row final : juce::Component
{
    BlockEditor& owner;
    arranger::WorkspaceBlock original;
    juce::TextEditor text;
    juce::TextButton handle, link;
    juce::ToggleButton checked;
    Row(BlockEditor& editor, arranger::WorkspaceBlock block) : owner(editor), original(std::move(block))
    {
        addAndMakeVisible(handle); handle.setButtonText(juce::String::fromUTF8("⋮⋮")); handle.getProperties().set("mrIconOnly", true);
        handle.setTooltip(BlockEditor::tr("Тип, порядок и действия блока"));
        handle.onClick = [this] { owner.menu(original.id); };
        addAndMakeVisible(text);
        text.setMultiLine(true); text.setReturnKeyStartsNewLine(false);
        text.setScrollbarsShown(false); text.setIndents(2, 3);
        text.setColour(juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
        text.setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
        text.setColour(juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
        text.setColour(juce::TextEditor::shadowColourId, juce::Colours::transparentBlack);
        text.setColour(juce::TextEditor::textColourId, mr::colour(mr::tokens::bright));
        text.setFont(juce::FontOptions(original.type == "heading" ? 23.0f : 15.0f));
        text.setText(original.text.isNotEmpty() ? original.text : original.name, false);
        text.setTextToShowWhenEmpty(BlockEditor::tr("Введите текст или / для выбора типа"), mr::colour(mr::tokens::muted));
        text.addKeyListener(&owner);
        text.onTextChange = [this]
        {
            if (owner.loading) return;
            owner.capture(); owner.startTimer(500); owner.layoutRows();
            if (text.getText() == "/") owner.menu(original.id, true);
        };
        text.onReturnKey = [this]
        {
            juce::Component::SafePointer<BlockEditor> safe(&owner); const auto id = original.id;
            juce::MessageManager::callAsync([safe, id] { if (safe) safe->addAfter(id); });
        };
        addAndMakeVisible(checked); checked.setToggleState(original.checked, juce::dontSendNotification);
        checked.setVisible(original.type == "checklist");
        checked.onClick = [this] { owner.capture(); owner.flush(); };
        addAndMakeVisible(link); link.setButtonText(juce::String::fromUTF8("↗")); link.getProperties().set("mrIconOnly", true);
        link.setTooltip(BlockEditor::tr("Открыть ссылку")); link.setVisible(original.type == "link");
        link.onClick = [this]
        {
            if (owner.flush().failed()) return;
            const auto url = original.url.trim();
            if (url.startsWithIgnoreCase("https://") || url.startsWithIgnoreCase("http://")) juce::URL(url).launchInDefaultBrowser();
        };
    }
    int wantedHeight(int width) const
    {
        const auto lines = juce::StringArray::fromLines(text.getText()); int count = 0;
        const int chars = juce::jmax(12, (width - 76) / (original.type == "heading" ? 12 : 8));
        for (const auto& line : lines) count += juce::jmax(1, (line.length() + chars - 1) / chars);
        return juce::jmax(original.type == "heading" ? 40 : 30, count * (original.type == "heading" ? 28 : 21) + 8);
    }
    void resized() override
    {
        auto r = getLocalBounds(); handle.setBounds(r.removeFromLeft(26).withHeight(26));
        if (original.type == "list") r.removeFromLeft(18);
        if (checked.isVisible()) checked.setBounds(r.removeFromLeft(24).withHeight(28));
        if (link.isVisible()) link.setBounds(r.removeFromRight(26).withHeight(26));
        text.setBounds(r);
    }
    void paint(juce::Graphics& g) override
    {
        if (original.type == "list") { g.setColour(mr::colour(mr::tokens::muted)); g.fillEllipse(26, 12, 4, 4); }
    }
};
BlockEditor::BlockEditor()
{
    addAndMakeVisible(viewport); viewport.setViewedComponent(&document, false);
    document.addAndMakeVisible(addLine); addLine.setButtonText(tr("+ Написать…"));
    addLine.setTooltip(tr("Добавить абзац. Внутри текста: Enter — следующий блок, Shift+Enter — перенос, / — тип."));
    addLine.onClick = [this] { addAfter(rows.empty() ? juce::String() : rows.back()->original.id); };
    addAndMakeVisible(feedback); feedback.setColour(juce::Label::textColourId, mr::colour(mr::tokens::muted));
    addKeyListener(this);
}
BlockEditor::~BlockEditor() { stopTimer(); viewport.setViewedComponent(nullptr, false); }
void BlockEditor::paint(juce::Graphics& g) { g.fillAll(mr::colour(mr::tokens::canvas)); }
void BlockEditor::resized()
{
    auto r = getLocalBounds(); feedback.setBounds(r.removeFromBottom(22)); viewport.setBounds(r); layoutRows();
}
void BlockEditor::layoutRows()
{
    const int width = juce::jmax(100, viewport.getWidth() - 18); int y = 8;
    for (auto& row : rows) { const int h = row->wantedHeight(width); row->setBounds(4, y, width - 12, h); y += h + 4; }
    addLine.setBounds(30, y + 4, juce::jmax(100, width - 44), 28);
    document.setSize(width, juce::jmax(viewport.getHeight(), y + 48));
}
void BlockEditor::capture()
{
    if (loading || boundId.isEmpty()) return;
    auto blocks = session.blocks();
    for (auto& block : blocks)
        for (const auto& row : rows) if (block.id == row->original.id)
        {
            // Old name-only blocks remain unchanged until their visible text is edited.
            const auto shown = row->original.text.isNotEmpty() ? row->original.text : row->original.name;
            if (row->text.getText() != shown)
            {
                block.text = row->text.getText();
                if (block.text.isEmpty() || (row->original.text.isEmpty() && row->original.name.isNotEmpty()) || row->original.name == row->original.text) block.name = block.text;
            }
            block.checked = row->checked.getToggleState(); break;
        }
    session.stage(std::move(blocks));
}
void BlockEditor::refresh(const juce::String& focusId)
{
    loading = true; rows.clear();
    for (const auto& block : session.blocks())
    {
        auto row = std::make_unique<Row>(*this, block); document.addAndMakeVisible(row.get()); rows.push_back(std::move(row));
    }
    loading = false; layoutRows();
    for (auto& row : rows) if (row->original.id == focusId) { row->text.grabKeyboardFocus(); row->text.moveCaretToEnd(); }
}
juce::Result BlockEditor::bind(const juce::String& id, Session::Read read, Session::Write write)
{
    capture(); const auto old = session.blocks(); const auto result = session.bind(id, std::move(read), std::move(write));
    updateStatus(result); if (result.failed()) return result;
    if (id != boundId || old != session.blocks()) { boundId = id; refresh(); }
    addLine.setEnabled(id.isNotEmpty()); return result;
}
juce::Result BlockEditor::flush()
{
    capture(); stopTimer(); const auto result = session.flush(); updateStatus(result);
    if (result.wasOk()) for (auto& row : rows) for (const auto& block : session.blocks()) if (row->original.id == block.id) row->original = block;
    return result;
}
void BlockEditor::timerCallback() { flush(); }
void BlockEditor::updateStatus(const juce::Result& result)
{
    feedback.setText(result.failed() ? tr("Не сохранено: ") + result.getErrorMessage() : juce::String(), juce::dontSendNotification);
    feedback.setColour(juce::Label::textColourId, mr::colour(mr::tokens::blockedDot));
}
void BlockEditor::addAfter(const juce::String& after, const juce::String& type, const juce::String& content)
{
    if (boundId.isEmpty() || flush().failed()) return;
    auto blocks = session.blocks(); auto it = std::find_if(blocks.begin(), blocks.end(), [&](const auto& b) { return b.id == after; });
    arranger::WorkspaceBlock block; block.id = juce::Uuid().toString(); block.type = type; block.text = content;
    const auto id = block.id; blocks.insert(it == blocks.end() ? blocks.end() : std::next(it), std::move(block));
    session.stage(std::move(blocks)); const auto result = session.flush(); updateStatus(result); refresh(id);
}
juce::Result BlockEditor::undo() { return history(false); }
juce::Result BlockEditor::redo() { return history(true); }
juce::Result BlockEditor::history(bool redo)
{
    const auto saved = flush(); if (saved.failed()) return saved;
    const auto result = redo ? session.redo() : session.undo(); updateStatus(result);
    if (result.wasOk()) refresh(); return result;
}
bool BlockEditor::keyPressed(const juce::KeyPress& key, juce::Component* component)
{
    if (key.getModifiers().isCommandDown() || key.getModifiers().isCtrlDown())
    {
        if (key.getKeyCode() == 'z' || key.getKeyCode() == 'Z')
        {
            juce::Component::SafePointer<BlockEditor> safe(this); const bool redo = key.getModifiers().isShiftDown();
            juce::MessageManager::callAsync([safe,redo] { if (safe) safe->history(redo); }); return true;
        }
        if (key.getKeyCode() == 'y' || key.getKeyCode() == 'Y') { juce::Component::SafePointer<BlockEditor> safe(this); juce::MessageManager::callAsync([safe] { if (safe) safe->history(true); }); return true; }
        if (key.getKeyCode() == 's' || key.getKeyCode() == 'S') { flush(); return true; }
    }
    if (key.getKeyCode() == juce::KeyPress::returnKey && key.getModifiers().isShiftDown())
        if (auto* field = dynamic_cast<juce::TextEditor*>(component)) { field->insertTextAtCaret("\n"); return true; }
    return false;
}
void BlockEditor::menu(const juce::String& id, bool slash)
{
    capture();
    juce::PopupMenu menu; menu.setLookAndFeel(&getLookAndFeel());
    const char* labels[] = {"Текст", "Заголовок", "Список", "Чек-лист", "Ссылка"};
    for (int i = 0; i < 5; ++i) menu.addItem(10 + i, tr(labels[i]));
    menu.addSeparator(); menu.addItem(1, tr("Добавить абзац ниже")); menu.addItem(2, tr("Дублировать"));
    menu.addItem(3, tr("Выше")); menu.addItem(4, tr("Ниже")); menu.addItem(5, tr("Удалить"));
    menu.addSeparator(); menu.addItem(6, tr("Отменить"), session.canUndo()); menu.addItem(7, tr("Повторить"), session.canRedo());
    menu.addItem(8, tr("Перечитать сохранённое…")); menu.addItem(9, tr("Свойства ссылки…"));
    juce::Component::SafePointer<BlockEditor> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this), [safe, id, slash](int action)
    {
        if (!safe) return;
        if (!action) return;
        if (action == 6 || action == 7) { safe->history(action == 7); return; }
        if (action == 8)
        {
            juce::AlertWindow::showOkCancelBox(juce::MessageBoxIconType::QuestionIcon, tr("Перечитать страницу"),
                tr("Отбросить несохранённое содержимое и загрузить сохранённую версию?"), tr("Перечитать"), tr("Отмена"), safe,
                juce::ModalCallbackFunction::create([safe](int choice) { if (safe && choice == 1) { safe->stopTimer(); const auto r = safe->session.reload(); safe->updateStatus(r); if (r.wasOk()) safe->refresh(); } }));
            return;
        }
        if (safe->flush().failed()) return;
        auto blocks = safe->session.blocks(); auto it = std::find_if(blocks.begin(), blocks.end(), [&](const auto& b) { return b.id == id; });
        if (it == blocks.end()) return;
        const auto index = static_cast<size_t>(std::distance(blocks.begin(), it));
        if (action == 1) { safe->addAfter(id); return; }
        if (action == 9)
        {
            const auto expected = *it;
            auto* dialog = new juce::AlertWindow(tr("Ссылка"), {}, juce::MessageBoxIconType::NoIcon, safe);
            dialog->addTextEditor("url", it->url, "URL"); dialog->addButton(tr("Сохранить"), 1); dialog->addButton(tr("Отмена"), 0);
            dialog->enterModalState(true, juce::ModalCallbackFunction::create([safe, dialog, id, expected](int result)
            {
                if (!safe || result != 1 || safe->flush().failed()) return;
                auto values = safe->session.blocks();
                for (auto& b : values) if (b.id == id && b == expected) { b.url = dialog->getTextEditorContents("url"); b.type = "link"; safe->session.stage(values); if (safe->flush().wasOk()) safe->refresh(id); return; }
            }), true); return;
        }
        if (action >= 10 && action < 15)
        {
            const char* types[] = {"text", "heading", "list", "checklist", "link"}; it->type = types[action - 10];
            if (slash && it->text == "/") it->text.clear();
        }
        else if (action == 2) { auto copy = *it; copy.id = juce::Uuid().toString(); blocks.insert(std::next(it), copy); }
        else if (action == 3 && index > 0) std::swap(blocks[index], blocks[index - 1]);
        else if (action == 4 && index + 1 < blocks.size()) std::swap(blocks[index], blocks[index + 1]);
        else if (action == 5) blocks.erase(it);
        else return;
        safe->session.stage(std::move(blocks));
        // Commit the staged structural change without restaging the still-visible widgets.
        safe->stopTimer(); const auto result = safe->session.flush(); safe->updateStatus(result); safe->refresh(id);
    });
}
