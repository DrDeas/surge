/*
 * Deasynth - an analog-voiced hybrid synthesizer built on the Surge XT engine.
 *
 * Deasynth is released under the GNU General Public Licence v3 or later (GPL-3.0-or-later).
 * The license is found in the "LICENSE" file in the root of this repository, or at
 * https://www.gnu.org/licenses/gpl-3.0.en.html
 */

#include "DeasynthEditor.h"
#include "Overlays.h"

using namespace deasynth;

namespace
{
constexpr int headerH = 54;
constexpr int modBarH = 38;
constexpr int bottomH = 114;
constexpr int margin = 10;
constexpr int gap = 6;
} // namespace

struct DeasynthEditor::Root : juce::Component
{
    explicit Root(DeasynthEditor &e) : editor(e)
    {
        setOpaque(true);
        setComponentID("deasynth-root"); // see menuParentFor
    }
    void paint(juce::Graphics &g) override { editor.paintRoot(g); }
    DeasynthEditor &editor;
};

//==============================================================================
DeasynthEditor::DeasynthEditor(DeasynthProcessor &p)
    : juce::AudioProcessorEditor(&p), processor(p), ctx{p, p.surge.get()}
{
    // Set on this editor only, never as the process-wide default: a host can have several
    // Deasynth windows (and other JUCE plugins) open at once. Menus and tooltips are parented
    // to the editor, so they pick this up.
    setLookAndFeel(&lnf);

    root = std::make_unique<Root>(*this);
    addAndMakeVisible(*root);
    root->setBounds(0, 0, baseWidth, baseHeight);

    if (!ctx.synth)
    {
        // the engine failed to start; paintRoot explains why
        setSize(baseWidth, baseHeight);
        return;
    }

    ctx.scene = juce::jlimit(0, n_scenes - 1, ctx.patch().scene_active.val.i);
    ctx.structureChanged = [this]() { pendingStructure = true; };
    ctx.setHint = [this](const juce::String &h) {
        if (h != hint)
        {
            hint = h;
            root->repaint(hintArea);
        }
    };

    // ---- header
    static const char *pageNames[numPages] = {"SYNTH", "MODULATION", "EFFECTS"};
    for (int i = 0; i < numPages; ++i)
    {
        pageTabs[i] = std::make_unique<FlatButton>(pageNames[i], theme::osc);
        pageTabs[i]->setFontSize(11.5f);
        pageTabs[i]->onClick = [this, i]() { setPage(i); };
        root->addAndMakeVisible(*pageTabs[i]);
    }
    for (int i = 0; i < 2; ++i)
    {
        sceneButtons[i] = std::make_unique<FlatButton>(i == 0 ? "A" : "B", theme::osc);
        sceneButtons[i]->setFontSize(13.f);
        sceneButtons[i]->setTooltip("Deasynth holds two complete layers. This chooses which one "
                                    "the panel edits; right-click for how the layers are played.");
        sceneButtons[i]->onClick = [this, i]() { setScene(i); };
        sceneButtons[i]->onRightClick = [this]() {
            juce::PopupMenu m;
            m.addSectionHeader("Layers are played");
            auto &sm = ctx.patch().scenemode;
            for (int v = 0; v < n_scene_modes; ++v)
            {
                m.addItem(scene_mode_names[v], true, sm.val.i == v, [this, v]() {
                    auto &par = ctx.patch().scenemode;
                    ctx.synth->setParameter01(ctx.synth->idForParameter(&par),
                                              Parameter::intScaledToFloat(v, par.val_max.i, par.val_min.i),
                                              false, true);
                    pendingStructure = true;
                });
            }
            m.showMenuAsync(juce::PopupMenu::Options().withParentComponent(root.get()));
        };
        root->addAndMakeVisible(*sceneButtons[i]);
    }

    presetName = std::make_unique<FlatButton>("", theme::osc);
    presetName->setFontSize(14.5f);
    presetName->setJustification(juce::Justification::centredLeft);
    presetName->setGlyph(FlatButton::Glyph::Dropdown);
    presetName->setTooltip("Browse patches");
    presetName->onClick = [this]() { showBrowser(true); };
    prevPreset = std::make_unique<FlatButton>();
    prevPreset->setGlyph(FlatButton::Glyph::Prev);
    prevPreset->setTooltip("Previous patch");
    prevPreset->onClick = [this]() { stepPatch(false); };
    nextPreset = std::make_unique<FlatButton>();
    nextPreset->setGlyph(FlatButton::Glyph::Next);
    nextPreset->setTooltip("Next patch");
    nextPreset->onClick = [this]() { stepPatch(true); };
    savePreset = std::make_unique<FlatButton>();
    savePreset->setGlyph(FlatButton::Glyph::Save);
    savePreset->setTooltip("Save this sound as a patch");
    savePreset->onClick = [this]() { showSaveDialog(); };
    menuButton = std::make_unique<FlatButton>();
    menuButton->setGlyph(FlatButton::Glyph::Menu);
    menuButton->onClick = [this]() { showMainMenu(); };
    for (auto *b : {presetName.get(), prevPreset.get(), nextPreset.get(), savePreset.get(), menuButton.get()})
        root->addAndMakeVisible(*b);

    scope = std::make_unique<ScopeDisplay>(ctx);
    root->addAndMakeVisible(*scope);

    modBar = std::make_unique<ModBar>(ctx);
    modBar->onLfoChosen = [this](int i) {
        if (lfoPanel)
            lfoPanel->selectLfo(i);
    };
    root->addAndMakeVisible(*modBar);

    // ---- pages
    auto addPanel = [this](int pg, std::unique_ptr<Panel> panel) {
        root->addChildComponent(*panel);
        pagePanels[pg].push_back(std::move(panel));
    };
    for (int i = 0; i < n_oscs; ++i)
        addPanel(Synth, std::make_unique<OscPanel>(ctx, i));
    addPanel(Synth, std::make_unique<MixerPanel>(ctx));
    addPanel(Synth, std::make_unique<FilterPanel>(ctx, 0));
    addPanel(Synth, std::make_unique<FilterPanel>(ctx, 1));
    addPanel(Synth, std::make_unique<ShaperPanel>(ctx));
    addPanel(Synth, std::make_unique<EnvPanel>(ctx, adsr_ampeg));
    addPanel(Synth, std::make_unique<EnvPanel>(ctx, adsr_filteg));

    {
        auto lp = std::make_unique<LfoPanel>(ctx);
        lfoPanel = lp.get();
        addPanel(Mod, std::move(lp));
        addPanel(Mod, std::make_unique<MacroPanel>(ctx));
        addPanel(Mod, std::make_unique<MatrixPanel>(ctx));
        lfoPanel->selectLfo(processor.editorState.lfo);
    }

    addPanel(Fx, std::make_unique<FxPanel>(ctx));

    // ---- bottom
    bottomPanels.push_back(std::make_unique<VoicePanel>(ctx));
    bottomPanels.push_back(std::make_unique<OutputPanel>(ctx));
    for (auto &bp : bottomPanels)
        root->addAndMakeVisible(*bp);

    keyboard = std::make_unique<juce::MidiKeyboardComponent>(
        processor.keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard);
    keyboard->setColour(juce::MidiKeyboardComponent::whiteNoteColourId, juce::Colour(0xffe4e0d6));
    keyboard->setColour(juce::MidiKeyboardComponent::blackNoteColourId, juce::Colour(0xff17191d));
    keyboard->setColour(juce::MidiKeyboardComponent::keySeparatorLineColourId, juce::Colour(0xff8a8679));
    keyboard->setColour(juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, theme::osc.withAlpha(0.25f));
    keyboard->setColour(juce::MidiKeyboardComponent::keyDownOverlayColourId, theme::osc.withAlpha(0.85f));
    keyboard->setColour(juce::MidiKeyboardComponent::shadowColourId, juce::Colours::transparentBlack);
    keyboard->setColour(juce::MidiKeyboardComponent::upDownButtonBackgroundColourId, theme::panelHi);
    keyboard->setColour(juce::MidiKeyboardComponent::upDownButtonArrowColourId, theme::textDim);
    keyboard->setColour(juce::MidiKeyboardComponent::textLabelColourId, juce::Colour(0xff6b6a64));
    keyboard->setAvailableRange(12, 108);
    keyboard->setLowestVisibleKey(36);
    keyboard->setOctaveForMiddleC(3);
    keyboard->setScrollButtonWidth(18);
    keyboard->setVelocity(0.8f, true);
    root->addAndMakeVisible(*keyboard);

    pitchWheel = std::make_unique<Wheel>(true, "BEND");
    pitchWheel->onChange = [this](float v) {
        processor.pushGuiMidi({DeasynthProcessor::GuiMidi::PitchBend, 0, (int)(v * 8191.f), 0});
    };
    modWheel = std::make_unique<Wheel>(false, "MOD");
    modWheel->onChange = [this](float v) {
        processor.pushGuiMidi({DeasynthProcessor::GuiMidi::ModWheel, 0, (int)(v * 127.f), 0});
    };
    root->addAndMakeVisible(*pitchWheel);
    root->addAndMakeVisible(*modWheel);

    // ---- overlays
    browser = std::make_unique<PresetBrowser>(*this, ctx);
    saveDialog = std::make_unique<SaveDialog>(*this, ctx);
    about = std::make_unique<AboutPanel>(*this);
    notice = std::make_unique<NoticePanel>();
    for (auto *o : {(juce::Component *)browser.get(), (juce::Component *)saveDialog.get(),
                    (juce::Component *)about.get(), (juce::Component *)notice.get()})
        root->addChildComponent(*o);

    // from here on the engine's error reports (and any it saved up while starting) come to us
    ctx.synth->storage.addErrorListener(this);

    layoutRoot();
    setPage(juce::jlimit(0, numPages - 1, processor.editorState.page));

    setResizable(true, true);
    if (auto *c = getConstrainer())
    {
        c->setFixedAspectRatio((double)baseWidth / (double)baseHeight);
        c->setSizeLimits(baseWidth * 6 / 10, baseHeight * 6 / 10, baseWidth * 2, baseHeight * 2);
    }
    const float z = juce::jlimit(0.6f, 2.f, processor.editorState.zoom);
    setSize((int)std::round(baseWidth * z), (int)std::round(baseHeight * z));

    refreshAll(true);
    startTimerHz(30);
}

DeasynthEditor::~DeasynthEditor()
{
    stopTimer();
    if (ctx.synth)
        ctx.synth->storage.removeErrorListener(this);
    setLookAndFeel(nullptr);
}

//==============================================================================
void DeasynthEditor::layoutRoot()
{
    juce::Rectangle<int> all(0, 0, baseWidth, baseHeight);

    // header
    auto header = all.removeFromTop(headerH).reduced(0, 12);
    header.removeFromLeft(184);
    for (int i = 0; i < numPages; ++i)
    {
        pageTabs[i]->setBounds(header.removeFromLeft(i == 0 ? 60 : 92));
        header.removeFromLeft(4);
    }
    header.removeFromLeft(12);
    header.removeFromLeft(44); // "LAYER" caption
    sceneButtons[0]->setBounds(header.removeFromLeft(30));
    header.removeFromLeft(3);
    sceneButtons[1]->setBounds(header.removeFromLeft(30));
    header.removeFromLeft(18);

    prevPreset->setBounds(header.removeFromLeft(26));
    header.removeFromLeft(3);
    presetName->setBounds(header.removeFromLeft(260));
    header.removeFromLeft(3);
    nextPreset->setBounds(header.removeFromLeft(26));
    header.removeFromLeft(6);
    savePreset->setBounds(header.removeFromLeft(30));

    header.removeFromRight(12);
    menuButton->setBounds(header.removeFromRight(32));
    header.removeFromRight(8);
    scope->setBounds(header.removeFromRight(136).expanded(0, 5));
    header.removeFromRight(10);
    header.removeFromLeft(12);
    hintArea = header.expanded(0, 6);

    modBar->setBounds(all.removeFromTop(modBarH));

    // bottom: voice | wheels + keyboard | output
    auto bottom = all.removeFromBottom(bottomH).reduced(margin, 0).withTrimmedBottom(margin - 2);
    bottomPanels[0]->setBounds(bottom.removeFromLeft(400));
    bottom.removeFromLeft(gap);
    bottomPanels[1]->setBounds(bottom.removeFromRight(248));
    bottom.removeFromRight(gap);
    auto wheels = bottom.removeFromLeft(62);
    pitchWheel->setBounds(wheels.removeFromLeft(29));
    wheels.removeFromLeft(4);
    modWheel->setBounds(wheels);
    bottom.removeFromLeft(6);
    keyboard->setBounds(bottom);
    keyboard->setKeyWidth((float)bottom.getWidth() / 36.f); // five octaves and a C

    // pages
    auto content = all.reduced(margin, 8);
    {
        auto c = content;
        auto rowA = c.removeFromTop(262);
        c.removeFromTop(gap + 2);
        auto rowB = c;

        auto &pp = pagePanels[Synth];
        for (int i = 0; i < 3; ++i)
        {
            pp[(size_t)i]->setBounds(rowA.removeFromLeft(298));
            rowA.removeFromLeft(gap);
        }
        pp[3]->setBounds(rowA);

        pp[4]->setBounds(rowB.removeFromLeft(272));
        rowB.removeFromLeft(gap);
        pp[5]->setBounds(rowB.removeFromLeft(272));
        rowB.removeFromLeft(gap);
        pp[6]->setBounds(rowB.removeFromLeft(214));
        rowB.removeFromLeft(gap);
        pp[7]->setBounds(rowB.removeFromLeft(244));
        rowB.removeFromLeft(gap);
        pp[8]->setBounds(rowB);
    }
    {
        auto c = content;
        auto right = c.removeFromRight(500);
        c.removeFromRight(gap);
        auto &pp = pagePanels[Mod];
        pp[2]->setBounds(right);
        pp[1]->setBounds(c.removeFromBottom(170));
        c.removeFromBottom(gap + 2);
        pp[0]->setBounds(c);
    }
    pagePanels[Fx][0]->setBounds(content);

    auto overlay = juce::Rectangle<int>(0, 0, baseWidth, baseHeight);
    browser->setBounds(overlay);
    saveDialog->setBounds(overlay);
    about->setBounds(overlay);
    notice->setBounds(overlay);
}

void DeasynthEditor::resized()
{
    if (!root)
        return;
    const float scale = (float)getWidth() / (float)baseWidth;
    root->setTransform(juce::AffineTransform::scale(scale));
    root->setBounds(0, 0, baseWidth, baseHeight);
    processor.editorState.zoom = scale;
}

void DeasynthEditor::paint(juce::Graphics &g) { g.fillAll(theme::backdrop); }

void DeasynthEditor::paintRoot(juce::Graphics &g)
{
    g.fillAll(theme::backdrop);

    // header
    juce::Rectangle<int> header(0, 0, baseWidth, headerH);
    g.setGradientFill(juce::ColourGradient(theme::panel, 0.f, 0.f, theme::chassis, 0.f, (float)headerH, false));
    g.fillRect(header);
    g.setColour(theme::outline.withAlpha(0.6f));
    g.drawHorizontalLine(headerH - 1, 0.f, (float)baseWidth);

    // the logo, then the name
    theme::drawLogo(g, juce::Rectangle<float>(12.f, 6.f, 40.f, (float)headerH - 12.f));
    g.setColour(theme::text);
    g.setFont(theme::font(19.f, true).withExtraKerningFactor(0.2f));
    g.drawText("DEASYNTH", 58, 0, 126, headerH, juce::Justification::centredLeft);

    if (!ctx.synth)
    {
        g.setColour(theme::danger);
        g.setFont(theme::font(18.f, true));
        g.drawText("Deasynth could not start its sound engine", 0, 280, baseWidth, 30, juce::Justification::centred);
        g.setColour(theme::textDim);
        g.setFont(theme::font(14.f));
        g.drawFittedText(processor.fatalErrorMessage, 200, 320, baseWidth - 400, 200, juce::Justification::centredTop, 10);
        return;
    }

    g.setFont(theme::labelFont(10.f));
    g.setColour(theme::textDim);
    g.drawText("LAYER", sceneButtons[0]->getX() - 44, 0, 40, headerH, juce::Justification::centredRight);

    // hint / status
    if (hint.isNotEmpty())
    {
        const int split = hint.indexOf(":  ");
        if (split > 0)
        {
            auto a = hintArea;
            g.setFont(theme::font(11.f));
            g.setColour(theme::textDim);
            g.drawFittedText(hint.substring(0, split), a.removeFromTop(a.getHeight() / 2), juce::Justification::bottomRight, 1, 0.75f);
            g.setFont(theme::font(13.5f, true));
            g.setColour(theme::text);
            g.drawFittedText(hint.substring(split + 3), a, juce::Justification::topRight, 1, 0.75f);
        }
        else
        {
            g.setFont(theme::font(11.5f));
            g.setColour(theme::textDim);
            g.drawFittedText(hint, hintArea, juce::Justification::centredRight, 2, 0.8f);
        }
    }
    else
    {
        const int voices = ctx.synth->storage.activeVoiceCount.load();
        g.setFont(theme::font(11.5f));
        g.setColour(theme::textFaint);
        juce::String s = juce::String(voices) + (voices == 1 ? " voice" : " voices");
        if (ctx.synth->mpeEnabled)
            s = "MPE   " + s;
        g.drawFittedText(s, hintArea, juce::Justification::centredRight, 1);
    }

    // bottom tray
    juce::Rectangle<int> tray(0, baseHeight - bottomH - 2, baseWidth, bottomH + 2);
    g.setColour(theme::chassis);
    g.fillRect(tray);
    g.setColour(theme::outline.withAlpha(0.6f));
    g.drawHorizontalLine(tray.getY(), 0.f, (float)baseWidth);
}

//==============================================================================
void DeasynthEditor::setPage(int p)
{
    page = juce::jlimit(0, numPages - 1, p);
    processor.editorState.page = page;
    for (int i = 0; i < numPages; ++i)
    {
        pageTabs[i]->setOn(i == page);
        for (auto &panel : pagePanels[i])
            panel->setVisible(i == page);
    }
    refreshAll(true);
}

void DeasynthEditor::setScene(int s)
{
    s = juce::jlimit(0, n_scenes - 1, s);
    auto &par = ctx.patch().scene_active;
    if (par.val.i != s)
    {
        ctx.synth->setParameter01(ctx.synth->idForParameter(&par),
                                  Parameter::intScaledToFloat(s, par.val_max.i, par.val_min.i), false, true);
    }
    ctx.scene = s;
    shownScene = -1;
    pendingStructure = true;
}

void DeasynthEditor::setZoom(float z)
{
    z = juce::jlimit(0.6f, 2.f, z);
    setSize((int)std::round(baseWidth * z), (int)std::round(baseHeight * z));
}

juce::String DeasynthEditor::currentPatchName() const
{
    juce::String n = ctx.patch().name;
    return n.isEmpty() ? juce::String("Untitled") : n;
}

void DeasynthEditor::loadPatch(int id)
{
    if (id < 0 || id >= (int)ctx.synth->storage.patch_list.size())
        return;
    ctx.armedSource = ms_original;
    ctx.synth->patchid_queue = id;
    ctx.synth->processAudioThreadOpsWhenAudioEngineUnavailable();
    pendingStructure = true;
}

void DeasynthEditor::stepPatch(bool forward)
{
    if (ctx.synth->storage.patch_list.empty())
    {
        showBrowser(true);
        return;
    }
    ctx.armedSource = ms_original;
    ctx.synth->jogPatch(forward, true);
    ctx.synth->processAudioThreadOpsWhenAudioEngineUnavailable();
    pendingStructure = true;
}

void DeasynthEditor::initPatch()
{
    ctx.armedSource = ms_original;
    processor.loadInitPatch();
    pendingStructure = true;
}

void DeasynthEditor::showBrowser(bool show)
{
    saveDialog->setVisible(false);
    about->setVisible(false);
    browser->setVisible(show);
    if (show)
        browser->toFront(true);
}

void DeasynthEditor::showSaveDialog()
{
    browser->setVisible(false);
    about->setVisible(false);
    saveDialog->setVisible(true);
    saveDialog->toFront(true);
}

void DeasynthEditor::showAbout(bool show)
{
    browser->setVisible(false);
    saveDialog->setVisible(false);
    about->setVisible(show);
    if (show)
        about->toFront(true);
}

void DeasynthEditor::showMainMenu()
{
    juce::PopupMenu m;
    m.addItem("Initialise patch", [this]() { initPatch(); });
    m.addItem("Browse patches...", [this]() { showBrowser(true); });
    m.addItem("Save patch...", [this]() { showSaveDialog(); });
    m.addItem("Show my patches folder", [this]() {
        juce::File f(juce::String(ctx.synth->storage.userPatchesPath.u8string()));
        f.createDirectory();
        f.revealToUser();
    });
    m.addSeparator();

    juce::PopupMenu zoom;
    const float cur = (float)getWidth() / (float)baseWidth;
    for (int pct : {75, 100, 125, 150, 200})
    {
        zoom.addItem(juce::String(pct) + " %", true, std::abs(cur - (float)pct / 100.f) < 0.02f,
                     [this, pct]() { setZoom((float)pct / 100.f); });
    }
    m.addSubMenu("Window size", zoom);

    juce::PopupMenu layers;
    for (int v = 0; v < n_scene_modes; ++v)
    {
        layers.addItem(scene_mode_names[v], true, ctx.patch().scenemode.val.i == v, [this, v]() {
            auto &par = ctx.patch().scenemode;
            ctx.synth->setParameter01(ctx.synth->idForParameter(&par),
                                      Parameter::intScaledToFloat(v, par.val_max.i, par.val_min.i), false, true);
            pendingStructure = true;
        });
    }
    m.addSubMenu("Layers A and B are played", layers);

    juce::PopupMenu poly;
    for (int v : {4, 8, 12, 16, 24, 32, 48, 64})
    {
        poly.addItem(juce::String(v) + " voices", true, ctx.patch().polylimit.val.i == v, [this, v]() {
            auto &par = ctx.patch().polylimit;
            ctx.synth->setParameter01(ctx.synth->idForParameter(&par),
                                      Parameter::intScaledToFloat(v, par.val_max.i, par.val_min.i), false, true);
        });
    }
    m.addSubMenu("Polyphony limit", poly);

    m.addItem("MPE (per-note expression)", true, ctx.synth->mpeEnabled, [this]() {
        ctx.synth->mpeEnabled = !ctx.synth->mpeEnabled;
        root->repaint();
    });
    m.addSeparator();
    m.addItem("About Deasynth", [this]() { showAbout(true); });

    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(menuButton.get()).withParentComponent(root.get()));
}

bool DeasynthEditor::keyPressed(const juce::KeyPress &key)
{
    if (key == juce::KeyPress::escapeKey && ctx.isArmed())
    {
        ctx.armedSource = ms_original;
        pendingStructure = true;
        return true;
    }
    return false;
}

//==============================================================================
void DeasynthEditor::refreshAll(bool force)
{
    if (!ctx.synth)
        return;

    if (ctx.scene != shownScene)
    {
        shownScene = ctx.scene;
        for (auto &pp : pagePanels)
            for (auto &panel : pp)
                panel->rebind();
        for (auto &panel : bottomPanels)
            panel->rebind();
        force = true;
    }

    for (int i = 0; i < 2; ++i)
        sceneButtons[i]->setOn(ctx.scene == i);

    const auto name = currentPatchName();
    const bool dirty = ctx.patch().isDirty;
    if (name != shownPatchName || dirty != shownDirty)
    {
        shownPatchName = name;
        shownDirty = dirty;
        presetName->setText(name + (dirty ? "  *" : ""));
    }

    const int voices = ctx.synth->storage.activeVoiceCount.load();
    if (voices != shownVoices)
    {
        shownVoices = voices;
        if (hint.isEmpty())
            root->repaint(hintArea);
    }

    modBar->refresh(force);
    scope->refresh(force);
    for (auto &panel : pagePanels[page])
        panel->refresh(force);
    for (auto &panel : bottomPanels)
        panel->refresh(force);

    if (force)
        root->repaint();
}

void DeasynthEditor::onSurgeError(const std::string &msg, const std::string &title,
                                  const SurgeStorage::ErrorType &type)
{
    if (type == SurgeStorage::AUDIO_INPUT_LATENCY_WARNING)
        return; // Deasynth has no audio input
    std::lock_guard<std::mutex> g(errorMutex);
    if (pendingErrors.size() < 8)
        pendingErrors.emplace_back(juce::String(title), juce::String(msg));
}

void DeasynthEditor::timerCallback()
{
    if (!ctx.synth)
        return;

    {
        std::vector<std::pair<juce::String, juce::String>> errors;
        {
            std::lock_guard<std::mutex> g(errorMutex);
            errors.swap(pendingErrors);
        }
        for (auto &[title, message] : errors)
            notice->show(title, message);
    }

    // Watchdog: the audio callback zeroes processRunning every block. If it stops being
    // zeroed the host has stopped calling us (transport parked, track frozen, no device), and
    // the engine's deferred work has to be done from here instead.
    if (++watchdogTick >= 4)
    {
        watchdogTick = 0;
        if (++ctx.synth->processRunning > 10)
            ctx.synth->audio_processing_active = false;
    }

    // Loading a patch, swapping an oscillator or effect type and reading wavetables are all
    // deferred to the audio thread. This does them when there is no audio thread running.
    ctx.synth->processAudioThreadOpsWhenAudioEngineUnavailable();

    bool force = pendingStructure;
    pendingStructure = false;

    if (ctx.synth->refresh_editor)
    {
        ctx.synth->refresh_editor = false;
        force = true;
    }

    // a patch load, or the host, may have changed which layer is active
    const int active = juce::jlimit(0, n_scenes - 1, ctx.patch().scene_active.val.i);
    if (active != ctx.scene && shownScene == ctx.scene)
        ctx.scene = active;

    // the armed source may have been made invalid by a patch load
    if (ctx.armedSource < 0 || ctx.armedSource >= n_modsources)
        ctx.armedSource = ms_original;

    refreshAll(force);
}
