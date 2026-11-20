/*
 * Deasynth - an analog-voiced hybrid synthesizer built on the Surge XT engine.
 *
 * Deasynth is released under the GNU General Public Licence v3 or later (GPL-3.0-or-later).
 * The license is found in the "LICENSE" file in the root of this repository, or at
 * https://www.gnu.org/licenses/gpl-3.0.en.html
 */

#include "Overlays.h"
#include "DeasynthEditor.h"
#include "version.h"

namespace deasynth
{
//==============================================================================
ModBar::ModBar(EditorContext &c) : ctx(c)
{
    groups = {
        {"", theme::voice, {ms_velocity, ms_keytrack, ms_aftertouch, ms_polyaftertouch, ms_modwheel, ms_pitchbend}, 0, {}},
        {"", theme::filtEnv, {ms_filtereg}, 0, {}},
        {"", theme::ampEnv, {ms_ampeg}, 0, {}},
        {"LFO", theme::lfo, {ms_lfo1, ms_lfo2, ms_lfo3, ms_lfo4, ms_lfo5, ms_lfo6}, 24, {}},
        {"S-LFO", theme::sceneLfo, {ms_slfo1, ms_slfo2, ms_slfo3, ms_slfo4, ms_slfo5, ms_slfo6}, 24, {}},
        {"MACRO", theme::macro, {ms_ctrl1, ms_ctrl2, ms_ctrl3, ms_ctrl4, ms_ctrl5, ms_ctrl6, ms_ctrl7, ms_ctrl8}, 24, {}},
        {"", theme::voice, {ms_random_bipolar, ms_alternate_bipolar}, 0, {}},
    };

    for (auto &grp : groups)
    {
        for (int ms : grp.sources)
        {
            Chip chip;
            chip.source = ms;
            juce::String text = ctx.nameForSource(ms, true);
            if (ms >= ms_ctrl1 && ms <= ms_ctrl8)
                text = juce::String(ms - ms_ctrl1 + 1);
            chip.button = std::make_unique<FlatButton>(text, grp.colour);
            chip.button->setFontSize(10.5f);
            chip.button->onClick = [this, ms]() { arm(ms); };
            chip.button->onRightClick = [this, ms]() { sourceMenu(ms); };
            addAndMakeVisible(*chip.button);
            chips.push_back(std::move(chip));
        }
    }
}

void ModBar::arm(int source)
{
    ctx.armedSource = (ctx.armedSource == source) ? (int)ms_original : source;
    if (source >= ms_lfo1 && source <= ms_slfo6 && onLfoChosen && ctx.isArmed())
        onLfoChosen(source - ms_lfo1);
    if (ctx.structureChanged)
        ctx.structureChanged();
}

void ModBar::sourceMenu(int source)
{
    juce::PopupMenu menu;
    menu.addSectionHeader(ctx.nameForSource(source));
    const bool used = ctx.synth->isModsourceUsed((modsources)source);
    menu.addItem(1, "Remove everything this source modulates", used);
    juce::Component::SafePointer<ModBar> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withParentComponent(menuParentFor(*this)),
                       [safe, source](int r) {
                           if (!safe || r != 1)
                               return;
                           auto &c = safe->ctx;
                           for (auto *p : c.patch().param_ptr)
                           {
                               if (p && c.synth->isActiveModulation(p->id, (modsources)source, c.scene, 0))
                                   c.synth->clearModulation(p->id, (modsources)source, c.scene, 0, false);
                           }
                           c.markDirty();
                           if (c.structureChanged)
                               c.structureChanged();
                       });
}

void ModBar::refresh(bool force)
{
    // the engine works out which sources are routed anywhere in one pass; read that once
    // here rather than asking per chip, which would redo the pass (and take its lock) each time
    ctx.synth->updateUsedState();
    for (auto &chip : chips)
    {
        chip.button->setOn(ctx.armedSource == chip.source);
        chip.button->setMarker(ctx.synth->modsourceused[chip.source]);
        if (chip.source >= ms_ctrl1 && chip.source <= ms_ctrl8)
            chip.button->setTooltip(ctx.nameForSource(chip.source));
        else
            chip.button->setTooltip(juce::String(modsource_names[chip.source]));
    }
    if (force)
        repaint();
}

void ModBar::resized()
{
    auto b = getLocalBounds().reduced(10, 6);
    b.removeFromLeft(82); // "MODULATE" title
    size_t ci = 0;
    for (auto &grp : groups)
    {
        if (grp.label.isNotEmpty())
        {
            const int lw = (int)std::ceil(juce::GlyphArrangement::getStringWidth(theme::labelFont(10.f), grp.label)) + 8;
            grp.labelArea = b.removeFromLeft(lw);
        }
        for (size_t i = 0; i < grp.sources.size(); ++i, ++ci)
        {
            auto &btn = *chips[ci].button;
            int w = grp.chipWidth;
            if (w == 0)
                w = (int)std::ceil(juce::GlyphArrangement::getStringWidth(theme::font(10.5f, true), btn.getText())) + 16;
            btn.setBounds(b.removeFromLeft(w));
            b.removeFromLeft(3);
        }
        b.removeFromLeft(9);
    }
    messageArea = b;
}

void ModBar::paint(juce::Graphics &g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour(theme::chassis);
    g.fillRect(b);
    g.setColour(theme::outline.withAlpha(0.6f));
    g.drawHorizontalLine(getHeight() - 1, 0.f, b.getRight());

    g.setFont(theme::labelFont(10.5f));
    g.setColour(ctx.isArmed() ? EditorContext::colourForSource(ctx.armedSource) : theme::textDim);
    g.drawText("MODULATE", getLocalBounds().reduced(14, 0).withWidth(80), juce::Justification::centredLeft);

    g.setFont(theme::labelFont(10.f));
    for (auto &grp : groups)
    {
        if (grp.label.isEmpty())
            continue;
        g.setColour(grp.colour.withAlpha(0.85f));
        g.drawText(grp.label, grp.labelArea, juce::Justification::centredLeft);
    }

    if (messageArea.getWidth() > 60)
    {
        g.setFont(theme::font(11.5f));
        if (ctx.isArmed())
        {
            g.setColour(EditorContext::colourForSource(ctx.armedSource));
            g.drawFittedText("Drag a knob to assign. Click again to finish.", messageArea,
                             juce::Justification::centredRight, 2, 0.8f);
        }
        else
        {
            g.setColour(theme::textFaint);
            g.drawFittedText("Click a source, then drag knobs.", messageArea,
                             juce::Justification::centredRight, 2, 0.8f);
        }
    }
}

//==============================================================================
Wheel::Wheel(bool s, const juce::String &l) : sprung(s), label(l) {}

void Wheel::setValue(float v)
{
    v = juce::jlimit(sprung ? -1.f : 0.f, 1.f, v);
    if (v != value)
    {
        value = v;
        repaint();
    }
}

void Wheel::paint(juce::Graphics &g)
{
    auto b = getLocalBounds();
    auto cap = b.removeFromBottom(14);
    g.setFont(theme::labelFont(9.f));
    g.setColour(theme::textFaint);
    g.drawText(label, cap, juce::Justification::centred);

    auto slot = b.toFloat().reduced(3.f, 2.f);
    g.setColour(theme::well);
    g.fillRoundedRectangle(slot, 5.f);
    g.setColour(theme::outline);
    g.drawRoundedRectangle(slot, 5.f, 1.f);

    const float norm = sprung ? (value * 0.5f + 0.5f) : value;
    if (sprung)
    {
        g.setColour(theme::outline);
        g.fillRect(slot.getX() + 3.f, slot.getCentreY() - 0.5f, slot.getWidth() - 6.f, 1.f);
    }
    else if (norm > 0.f)
    {
        g.setColour(theme::voice.withAlpha(0.18f));
        g.fillRoundedRectangle(slot.reduced(2.f).withTop(slot.getBottom() - 6.f - norm * (slot.getHeight() - 12.f)), 3.f);
    }

    const float y = slot.getBottom() - 6.f - norm * (slot.getHeight() - 12.f);
    g.setColour(theme::text);
    g.fillRoundedRectangle(slot.getX() + 2.f, y - 2.5f, slot.getWidth() - 4.f, 5.f, 2.f);
}

void Wheel::mouseDrag(const juce::MouseEvent &e)
{
    auto slot = getLocalBounds().withTrimmedBottom(14).toFloat().reduced(3.f, 8.f);
    const float norm = juce::jlimit(0.f, 1.f, (slot.getBottom() - e.position.y) / slot.getHeight());
    setValue(sprung ? norm * 2.f - 1.f : norm);
    if (onChange)
        onChange(value);
}

void Wheel::mouseUp(const juce::MouseEvent &)
{
    if (sprung)
    {
        setValue(0.f);
        if (onChange)
            onChange(0.f);
    }
}

//==============================================================================
struct PresetBrowser::CategoryModel : juce::ListBoxModel
{
    explicit CategoryModel(PresetBrowser &o) : owner(o) {}
    int getNumRows() override { return (int)owner.categories.size(); }
    void paintListBoxItem(int row, juce::Graphics &g, int w, int h, bool) override
    {
        if (row < 0 || row >= (int)owner.categories.size())
            return;
        const auto &e = owner.categories[(size_t)row];
        if (e.category == -2)
        {
            g.setFont(theme::labelFont(10.f));
            g.setColour(theme::textFaint);
            g.drawText(e.label.toUpperCase(), 10, 4, w - 20, h - 4, juce::Justification::bottomLeft);
            return;
        }
        // while a search is active the results come from every category
        const bool searching = owner.search.getText().trim().isNotEmpty();
        const bool sel = searching ? row == 0 : row == owner.selectedCategoryRow;
        if (sel)
        {
            g.setColour(theme::osc.withAlpha(0.18f));
            g.fillRoundedRectangle(4.f, 1.f, (float)w - 8.f, (float)h - 2.f, 5.f);
        }
        g.setFont(theme::font(13.5f, sel));
        g.setColour(sel ? theme::text : theme::textDim);
        g.drawFittedText(e.label, 14, 0, w - 64, h, juce::Justification::centredLeft, 1, 0.8f);
        g.setFont(theme::font(11.f));
        g.setColour(theme::textFaint);
        g.drawText(juce::String(e.count), w - 50, 0, 38, h, juce::Justification::centredRight);
    }
    void listBoxItemClicked(int row, const juce::MouseEvent &) override { owner.selectCategory(row); }
    PresetBrowser &owner;
};

struct PresetBrowser::PatchModel : juce::ListBoxModel
{
    explicit PatchModel(PresetBrowser &o) : owner(o) {}
    int getNumRows() override { return (int)owner.patches.size(); }
    void paintListBoxItem(int row, juce::Graphics &g, int w, int h, bool) override
    {
        if (row < 0 || row >= (int)owner.patches.size())
            return;
        auto &storage = owner.ctx.synth->storage;
        const int id = owner.patches[(size_t)row];
        if (id < 0 || id >= (int)storage.patch_list.size())
            return;
        const auto &p = storage.patch_list[(size_t)id];
        const bool current = id == owner.selectedPatchId;
        if (current)
        {
            g.setColour(theme::osc.withAlpha(0.18f));
            g.fillRoundedRectangle(4.f, 1.f, (float)w - 8.f, (float)h - 2.f, 5.f);
        }
        g.setFont(theme::font(13.5f, current));
        g.setColour(current ? theme::osc : theme::text);
        g.drawFittedText(p.name, 14, 0, w / 2, h, juce::Justification::centredLeft, 1, 0.8f);

        if (p.category >= 0 && p.category < (int)storage.patch_category.size())
        {
            g.setFont(theme::font(11.5f));
            g.setColour(theme::textFaint);
            g.drawFittedText(juce::String(storage.patch_category[(size_t)p.category].name)
                                 .replaceCharacter('\\', '/')
                                 .replace("/", "  /  "),
                             w / 2 + 20, 0, w / 2 - 34, h, juce::Justification::centredRight, 1, 0.8f);
        }
    }
    void listBoxItemClicked(int row, const juce::MouseEvent &) override
    {
        owner.choosePatchRow(row);
    }
    void listBoxItemDoubleClicked(int row, const juce::MouseEvent &e) override
    {
        listBoxItemClicked(row, e);
        owner.editor.showBrowser(false);
    }
    PresetBrowser &owner;
};

PresetBrowser::PresetBrowser(DeasynthEditor &ed, EditorContext &c) : editor(ed), ctx(c)
{
    categoryModel = std::make_unique<CategoryModel>(*this);
    patchModel = std::make_unique<PatchModel>(*this);
    categoryList.setModel(categoryModel.get());
    patchList.setModel(patchModel.get());
    categoryList.setRowHeight(27);
    patchList.setRowHeight(27);
    // typing always goes to the search box, and Escape always reaches the browser
    categoryList.setWantsKeyboardFocus(false);
    patchList.setWantsKeyboardFocus(false);

    search.setFont(theme::font(14.f));
    search.setTextToShowWhenEmpty("Search all patches", theme::textFaint);
    search.setIndents(10, 7);
    search.addListener(this);
    search.onEscapeKey = [this]() { editor.showBrowser(false); };
    search.onNavigationKey = [this](const juce::KeyPress &k) { return handleNavigationKey(k); };

    close.setGlyph(FlatButton::Glyph::Close);
    close.setQuiet(true);
    close.onClick = [this]() { editor.showBrowser(false); };

    addAndMakeVisible(search);
    addAndMakeVisible(close);
    addAndMakeVisible(categoryList);
    addAndMakeVisible(patchList);
    setWantsKeyboardFocus(true);
}

PresetBrowser::~PresetBrowser()
{
    categoryList.setModel(nullptr);
    patchList.setModel(nullptr);
}

void PresetBrowser::rebuild()
{
    auto &storage = ctx.synth->storage;
    categories.clear();
    categories.push_back({-1, "All patches", (int)storage.patch_list.size()});

    std::vector<int> direct(storage.patch_category.size(), 0);
    for (const auto &p : storage.patch_list)
        if (p.category >= 0 && p.category < (int)direct.size())
            ++direct[(size_t)p.category];

    int section = -1;
    for (int oi = 0; oi < (int)storage.patchCategoryOrdering.size(); ++oi)
    {
        const int c = storage.patchCategoryOrdering[(size_t)oi];
        if (c < 0 || c >= (int)direct.size() || direct[(size_t)c] == 0)
            continue;
        const int thisSection = oi >= storage.firstUserCategory ? 2 : (oi >= storage.firstThirdPartyCategory ? 1 : 0);
        if (thisSection != section)
        {
            static const char *names[] = {"Factory", "Third party", "User"};
            categories.push_back({-2, names[thisSection], 0});
            section = thisSection;
        }
        categories.push_back({c,
                              juce::String(storage.patch_category[(size_t)c].name)
                                  .replaceCharacter('\\', '/')
                                  .replace("/", "  /  "),
                              direct[(size_t)c]});
    }

    selectedCategoryRow = juce::jlimit(0, (int)categories.size() - 1, selectedCategoryRow);
    if (categories[(size_t)selectedCategoryRow].category == -2)
        selectedCategoryRow = 0;

    // open on the category of the patch that is loaded, the first time
    const int pid = selectedPatchId;
    if (selectedCategoryRow == 0 && pid >= 0 && pid < (int)storage.patch_list.size())
    {
        const int cat = storage.patch_list[(size_t)pid].category;
        for (int i = 0; i < (int)categories.size(); ++i)
            if (categories[(size_t)i].category == cat)
                selectedCategoryRow = i;
    }

    categoryList.updateContent();
    categoryList.repaint();
    rebuildPatches();
}

void PresetBrowser::selectCategory(int row)
{
    if (row < 0 || row >= (int)categories.size() || categories[(size_t)row].category == -2)
        return;
    selectedCategoryRow = row;
    search.setText({}, false);
    categoryList.repaint();
    rebuildPatches();
}

void PresetBrowser::rebuildPatches()
{
    categoryList.repaint();
    auto &storage = ctx.synth->storage;
    patches.clear();
    const auto query = search.getText().trim();
    const int cat = categories.empty() ? -1 : categories[(size_t)selectedCategoryRow].category;

    for (int id : storage.patchOrdering)
    {
        if (id < 0 || id >= (int)storage.patch_list.size())
            continue;
        const auto &p = storage.patch_list[(size_t)id];
        if (query.isNotEmpty())
        {
            if (!juce::String(p.name).containsIgnoreCase(query))
                continue;
        }
        else if (cat >= 0 && p.category != cat)
        {
            continue;
        }
        patches.push_back(id);
    }
    patchList.updateContent();
    patchList.repaint();
    repaint();
}

void PresetBrowser::visibilityChanged()
{
    if (isVisible())
    {
        selectedPatchId = editor.currentLibraryPatch();
        rebuild();
        search.grabKeyboardFocus();
        const int row = selectedPatchRow();
        if (row >= 0)
            patchList.scrollToEnsureRowIsOnscreen(row);
    }
}

int PresetBrowser::selectedPatchRow() const
{
    auto it = std::find(patches.begin(), patches.end(), selectedPatchId);
    return it == patches.end() ? -1 : (int)(it - patches.begin());
}

void PresetBrowser::choosePatchRow(int row)
{
    if (row < 0 || row >= (int)patches.size())
        return;
    selectedPatchId = patches[(size_t)row];
    editor.loadPatch(selectedPatchId);
    patchList.scrollToEnsureRowIsOnscreen(row);
    patchList.repaint();
    search.grabKeyboardFocus();
}

void PresetBrowser::stepSelection(int delta)
{
    if (patches.empty())
        return;
    const int current = selectedPatchRow();
    // with nothing selected in this list, down starts at the top and up at the bottom
    int row = current < 0 ? (delta > 0 ? 0 : (int)patches.size() - 1) : current + delta;
    row = juce::jlimit(0, (int)patches.size() - 1, row);
    if (row != current)
        choosePatchRow(row);
}

bool PresetBrowser::handleNavigationKey(const juce::KeyPress &key)
{
    const int page = juce::jmax(1, patchList.getHeight() / juce::jmax(1, patchList.getRowHeight()) - 1);
    if (key == juce::KeyPress::upKey)
        stepSelection(-1);
    else if (key == juce::KeyPress::downKey)
        stepSelection(1);
    else if (key == juce::KeyPress::pageUpKey)
        stepSelection(-page);
    else if (key == juce::KeyPress::pageDownKey)
        stepSelection(page);
    else if (key == juce::KeyPress::returnKey)
        editor.showBrowser(false); // the selected patch is already loaded
    else
        return false;
    return true;
}

bool PresetBrowser::keyPressed(const juce::KeyPress &key)
{
    if (handleNavigationKey(key))
        return true;
    if (key == juce::KeyPress::escapeKey)
    {
        editor.showBrowser(false);
        return true;
    }
    return false;
}

void PresetBrowser::mouseDown(const juce::MouseEvent &e)
{
    if (!card.contains(e.getPosition()))
        editor.showBrowser(false);
}

void PresetBrowser::resized()
{
    card = getLocalBounds().reduced(120, 26);
    auto b = card.reduced(16);
    auto top = b.removeFromTop(34);
    close.setBounds(top.removeFromRight(30).reduced(2));
    top.removeFromRight(10);
    top.removeFromLeft(150);
    search.setBounds(top);
    b.removeFromTop(14);
    categoryList.setBounds(b.removeFromLeft(300));
    b.removeFromLeft(14);
    patchList.setBounds(b);
}

void PresetBrowser::paint(juce::Graphics &g)
{
    g.fillAll(juce::Colours::black.withAlpha(0.6f));
    g.setColour(theme::panel);
    g.fillRoundedRectangle(card.toFloat(), 12.f);
    g.setColour(theme::outline);
    g.drawRoundedRectangle(card.toFloat().reduced(0.5f), 12.f, 1.f);

    g.setColour(theme::text);
    g.setFont(theme::font(18.f, true));
    g.drawText("Patches", card.reduced(18, 16).withHeight(34), juce::Justification::centredLeft);

    auto lists = card.reduced(16).withTrimmedTop(48);
    g.setColour(theme::well.withAlpha(0.6f));
    g.fillRoundedRectangle(lists.withWidth(300).toFloat(), 8.f);
    g.fillRoundedRectangle(lists.withTrimmedLeft(314).toFloat(), 8.f);

    if (ctx.synth->storage.patch_list.empty())
    {
        auto msg = lists.withTrimmedLeft(314).reduced(40);
        g.setColour(theme::textDim);
        g.setFont(theme::font(15.f, true));
        g.drawFittedText("No patches found", msg.withHeight(40), juce::Justification::centred, 1);
        g.setColour(theme::textFaint);
        g.setFont(theme::font(13.f));
        g.drawFittedText("The factory library is not installed. Run scripts/deasynth/install-content.sh "
                         "from the source tree to install it, then reopen Deasynth.\n\n"
                         "Deasynth looked for it in:\n" +
                             juce::String(ctx.synth->storage.datapath.u8string()) +
                             "\n\nPatches you save yourself go to:\n" +
                             juce::String(ctx.synth->storage.userPatchesPath.u8string()),
                         msg.withTrimmedTop(50), juce::Justification::centredTop, 12);
    }
    else if (patches.empty())
    {
        g.setColour(theme::textFaint);
        g.setFont(theme::font(13.f));
        g.drawFittedText("Nothing matches that search.", lists.withTrimmedLeft(314),
                         juce::Justification::centred, 1);
    }
}

//==============================================================================
SaveDialog::SaveDialog(DeasynthEditor &ed, EditorContext &c)
    : editor(ed), ctx(c), save("Save", theme::osc), cancel("Cancel", theme::textDim)
{
    for (auto *t : {&name, &category, &author})
    {
        t->setFont(theme::font(14.f));
        t->setIndents(10, 7);
        t->onReturnKey = [this]() { commit(); };
        t->onEscapeKey = [this]() { setVisible(false); };
        addAndMakeVisible(*t);
    }
    save.setOn(true);
    save.onClick = [this]() { commit(); };
    cancel.onClick = [this]() { setVisible(false); };
    addAndMakeVisible(save);
    addAndMakeVisible(cancel);
}

void SaveDialog::visibilityChanged()
{
    if (!isVisible())
        return;
    auto &patch = ctx.patch();
    error.clear();
    name.setText(patch.name, false);
    juce::String cat = patch.category;
    if (cat.isEmpty() || cat == "Init")
        cat = "My Patches";
    category.setText(cat, false);
    author.setText(patch.author == "Deasynth" ? juce::String() : juce::String(patch.author), false);
    name.grabKeyboardFocus();
    name.selectAll();
}

bool SaveDialog::keyPressed(const juce::KeyPress &key)
{
    if (key == juce::KeyPress::escapeKey)
    {
        setVisible(false);
        return true;
    }
    return false;
}

void SaveDialog::commit()
{
    const auto n = name.getText().trim();
    const auto c = category.getText().trim();
    if (n.isEmpty())
    {
        error = "Give the patch a name.";
        repaint();
        return;
    }
    if (n.containsAnyOf("/\\:*?\"<>|") || c.containsAnyOf(":*?\"<>|") || c.contains("..") ||
        c.startsWithChar('/') || c.startsWithChar('\\'))
    {
        error = "Names cannot contain / \\ : * ? \" < > | or \"..\", and a category cannot "
                "start with a slash";
        repaint();
        return;
    }

    auto &patch = ctx.patch();
    const auto oldName = patch.name, oldCategory = patch.category, oldAuthor = patch.author;
    patch.name = n.toStdString();
    patch.category = (c.isEmpty() ? juce::String("My Patches") : c).toStdString();
    patch.author = author.getText().trim().toStdString();

    // The engine reports a failed save through its error channel and writes nothing, so the
    // only proof of success is the file: it must exist and have been written just now.
    const auto file = juce::File(juce::String(ctx.synth->storage.userPatchesPath.u8string()))
                          .getChildFile(juce::String(patch.category))
                          .getChildFile(n + ".fxp");
    const auto before = juce::Time::getCurrentTime() - juce::RelativeTime::seconds(2.0);
    ctx.synth->savePatch();

    if (!file.existsAsFile() || file.getLastModificationTime() < before)
    {
        patch.name = oldName;
        patch.category = oldCategory;
        patch.author = oldAuthor;
        error = "The patch could not be saved. Check that " +
                juce::String(ctx.synth->storage.userPatchesPath.u8string()) + " can be written to.";
        repaint();
        return;
    }

    patch.isDirty = false;
    setVisible(false);
    if (ctx.structureChanged)
        ctx.structureChanged();
}

void SaveDialog::resized()
{
    card = getLocalBounds().withSizeKeepingCentre(460, 330);
    auto b = card.reduced(24);
    b.removeFromTop(44);
    for (auto *t : {&name, &category, &author})
    {
        b.removeFromTop(18);
        t->setBounds(b.removeFromTop(32));
        b.removeFromTop(8);
    }
    auto buttons = card.reduced(24).removeFromBottom(32);
    save.setBounds(buttons.removeFromRight(100));
    buttons.removeFromRight(8);
    cancel.setBounds(buttons.removeFromRight(90));
}

void SaveDialog::paint(juce::Graphics &g)
{
    g.fillAll(juce::Colours::black.withAlpha(0.6f));
    g.setColour(theme::panel);
    g.fillRoundedRectangle(card.toFloat(), 12.f);
    g.setColour(theme::outline);
    g.drawRoundedRectangle(card.toFloat().reduced(0.5f), 12.f, 1.f);

    auto b = card.reduced(24);
    g.setColour(theme::text);
    g.setFont(theme::font(18.f, true));
    g.drawText("Save patch", b.removeFromTop(44), juce::Justification::centredLeft);

    g.setFont(theme::labelFont(10.f));
    g.setColour(theme::textDim);
    for (auto *label : {"NAME", "CATEGORY", "AUTHOR"})
    {
        g.drawText(label, b.removeFromTop(18), juce::Justification::centredLeft);
        b.removeFromTop(40);
    }

    if (error.isNotEmpty())
    {
        g.setColour(theme::danger);
        g.setFont(theme::font(12.f));
        g.drawFittedText(error, card.reduced(24).removeFromBottom(32).withTrimmedRight(210),
                         juce::Justification::centredLeft, 2);
    }
}

//==============================================================================
void NoticePanel::show(const juce::String &t, const juce::String &m)
{
    // several reports in a row are shown together rather than replacing one another
    if (isVisible() && message.isNotEmpty() && !message.contains(m))
        message << "\n\n" << m;
    else
        message = m;
    title = t;
    setVisible(true);
    setWantsKeyboardFocus(true);
    toFront(true);
    repaint();
}

void NoticePanel::paint(juce::Graphics &g)
{
    g.fillAll(juce::Colours::black.withAlpha(0.65f));
    auto card = getLocalBounds().withSizeKeepingCentre(560, 300);
    g.setColour(theme::panel);
    g.fillRoundedRectangle(card.toFloat(), 12.f);
    g.setColour(theme::danger.withAlpha(0.8f));
    g.drawRoundedRectangle(card.toFloat().reduced(0.5f), 12.f, 1.f);

    auto b = card.reduced(28, 24);
    g.setColour(theme::text);
    g.setFont(theme::font(18.f, true));
    g.drawFittedText(title, b.removeFromTop(34), juce::Justification::centredLeft, 1);
    b.removeFromTop(8);
    auto foot = b.removeFromBottom(22);
    g.setColour(theme::textDim);
    g.setFont(theme::font(13.5f));
    g.drawFittedText(message, b, juce::Justification::topLeft, 12);
    g.setColour(theme::textFaint);
    g.setFont(theme::font(12.f));
    g.drawText("Click anywhere to dismiss", foot, juce::Justification::centredLeft);
}

//==============================================================================
AboutPanel::AboutPanel(DeasynthEditor &ed) : editor(ed) {}

void AboutPanel::mouseDown(const juce::MouseEvent &) { editor.showAbout(false); }

void AboutPanel::paint(juce::Graphics &g)
{
    g.fillAll(juce::Colours::black.withAlpha(0.65f));
    auto card = getLocalBounds().withSizeKeepingCentre(620, 400);
    g.setColour(theme::panel);
    g.fillRoundedRectangle(card.toFloat(), 12.f);
    g.setColour(theme::outline);
    g.drawRoundedRectangle(card.toFloat().reduced(0.5f), 12.f, 1.f);

    auto b = card.reduced(34, 28);
    theme::drawLogo(g, juce::Rectangle<float>((float)b.getRight() - 84.f, (float)b.getY() - 4.f, 84.f, 92.f));
    g.setColour(theme::text);
    g.setFont(theme::font(30.f, true).withExtraKerningFactor(0.16f));
    g.drawText("DEASYNTH", b.removeFromTop(40), juce::Justification::centredLeft);
    g.setColour(theme::osc);
    g.setFont(theme::font(13.f));
    g.drawText(juce::String("Version ") + Surge::Build::FullVersionStr, b.removeFromTop(22),
               juce::Justification::centredLeft);
    b.removeFromTop(16);

    g.setColour(theme::textDim);
    g.setFont(theme::font(13.5f));
    g.drawFittedText(
        "An analog-voiced hybrid synthesizer.\n\n"
        "Deasynth is built on the Surge XT synthesis engine by the Surge Synth Team, "
        "originally created by Claes Johanson of Vember Audio, and uses JUCE. The transistor "
        "ladder, diode ladder, K35 and state variable filter models, the oscillators and the "
        "effects are theirs and those of the open source projects they build on; the "
        "interface, the voice variance and the output stage are Deasynth's own.\n\n"
        "Deasynth is free software, released under the GNU General Public License, version 3 "
        "or later. You may use, study, share and change it under those terms, and its "
        "complete source code is available to you. It comes with no warranty.\n\n"
        "Click anywhere to close.",
        b, juce::Justification::topLeft, 20);
}
} // namespace deasynth
