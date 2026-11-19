/*
 * Deasynth - an analog-voiced hybrid synthesizer built on the Surge XT engine.
 *
 * Deasynth is released under the GNU General Public Licence v3 or later (GPL-3.0-or-later).
 * The license is found in the "LICENSE" file in the root of this repository, or at
 * https://www.gnu.org/licenses/gpl-3.0.en.html
 */

#include "Panels.h"
#include "../DeasynthProcessor.h"

namespace deasynth
{
//==============================================================================
Panel::Panel(EditorContext &c, const juce::String &title, juce::Colour accent)
    : Section(title, accent), ctx(c)
{
}

void Panel::refresh(bool force)
{
    for (auto *r : refreshables)
        r->refresh(force);
}

ParamControl *Panel::knob(const juce::String &label, ParamControl::Look look)
{
    auto *k = add<ParamControl>(ctx, getAccent(), look);
    if (label.isNotEmpty())
        k->setLabel(label);
    return k;
}

void Panel::layoutRow(juce::Rectangle<int> row, std::initializer_list<juce::Component *> items)
{
    const int n = (int)items.size();
    if (n == 0)
        return;
    const float cell = (float)row.getWidth() / (float)n;
    int i = 0;
    for (auto *c : items)
    {
        if (c)
        {
            const int x0 = row.getX() + (int)std::round(cell * (float)i);
            const int x1 = row.getX() + (int)std::round(cell * (float)(i + 1));
            c->setBounds(juce::Rectangle<int>(x0, row.getY(), x1 - x0, row.getHeight()).reduced(3, 0));
        }
        ++i;
    }
}

//==============================================================================
OscPanel::OscPanel(EditorContext &c, int i)
    : Panel(c, "Oscillator " + juce::String(i + 1), theme::osc), index(i)
{
    display = add<OscDisplay>(ctx, index);
    type = knob({}, ParamControl::Look::Choice);
    type->setShowLabel(false);
    type->choiceFilter = [](int v) { return v != ot_audioinput; };
    keytrack = knob("Key", ParamControl::Look::Toggle);
    keytrack->setTooltip("Follow the keyboard. Off holds the oscillator at a fixed pitch.");
    retrigger = knob("Retrig", ParamControl::Look::Toggle);
    retrigger->setTooltip("Restart the waveform at the same phase on every note.");
    octave = knob("Octave", ParamControl::Look::Knob);
    pitch = knob("Pitch");
    for (auto &p : params)
        p = knob();
    rebind();
}

void OscPanel::rebind()
{
    auto &o = ctx.sceneData().osc[index];
    type->setParam(&o.type);
    keytrack->setParam(&o.keytrack);
    retrigger->setParam(&o.retrigger);
    octave->setParam(&o.octave);
    pitch->setParam(&o.pitch);
    for (int i = 0; i < n_osc_params; ++i)
        params[i]->setParam(&o.p[i]);
}

void OscPanel::resized()
{
    auto header = getHeaderBounds();
    retrigger->setBounds(header.removeFromRight(46));
    header.removeFromRight(4);
    keytrack->setBounds(header.removeFromRight(34));
    header.removeFromRight(6);
    type->setBounds(header.removeFromRight(std::min(118, header.getWidth())));

    auto b = getContentBounds();
    display->setBounds(b.removeFromTop(72));
    b.removeFromTop(8);
    const int rowH = (b.getHeight() - 6) / 2;
    auto r1 = b.removeFromTop(rowH);
    b.removeFromTop(6);
    auto r2 = b.removeFromTop(rowH);
    layoutRow(r1, {octave, pitch, params[0], params[1], params[2]});
    layoutRow(r2, {params[3], params[4], params[5], params[6], nullptr});
}

//==============================================================================
MixerPanel::MixerPanel(EditorContext &c) : Panel(c, "Mixer", theme::mixer)
{
    static const char *names[6] = {"Osc 1", "Osc 2", "Osc 3", "Noise", "Ring 1x2", "Ring 2x3"};
    for (int i = 0; i < 6; ++i)
    {
        level[i] = knob(names[i]);
        mute[i] = knob("On", ParamControl::Look::Toggle);
        mute[i]->setInverted(true);
    }
    noiseColour = knob("Colour");
    preGain = knob("Drive");
    preGain->setTooltip("Level into the filters. Push it to overdrive the ladder, the way an "
                        "overloaded mixer does on a classic monosynth.");
    fmRouting = knob("FM Routing", ParamControl::Look::Choice);
    fmDepth = knob("FM Depth");
    drift = knob("Drift");
    drift->setTooltip("Slow, independent pitch wander on each oscillator.");
    rebind();
}

void MixerPanel::rebind()
{
    auto &s = ctx.sceneData();
    Parameter *levels[6] = {&s.level_o1,    &s.level_o2,      &s.level_o3,
                            &s.level_noise, &s.level_ring_12, &s.level_ring_23};
    Parameter *mutes[6] = {&s.mute_o1,    &s.mute_o2,      &s.mute_o3,
                           &s.mute_noise, &s.mute_ring_12, &s.mute_ring_23};
    for (int i = 0; i < 6; ++i)
    {
        level[i]->setParam(levels[i]);
        mute[i]->setParam(mutes[i]);
    }
    noiseColour->setParam(&s.noise_colour);
    preGain->setParam(&s.level_pfg);
    fmRouting->setParam(&s.fm_switch);
    fmDepth->setParam(&s.fm_depth);
    drift->setParam(&s.drift);
}

void MixerPanel::resized()
{
    auto b = getContentBounds();
    auto top = b.removeFromTop(72);
    layoutRow(top, {level[0], level[1], level[2], level[3], level[4], level[5]});
    b.removeFromTop(4);
    auto mutes = b.removeFromTop(20);
    layoutRow(mutes.reduced(4, 0), {mute[0], mute[1], mute[2], mute[3], mute[4], mute[5]});
    b.removeFromTop(12);
    auto bottom = b.removeFromTop(std::min(76, b.getHeight()));
    auto fm = bottom.removeFromLeft(bottom.getWidth() * 2 / 6);
    layoutRow(bottom, {fmDepth, noiseColour, drift, preGain});
    fmRouting->setBounds(fm.reduced(3, 0).withSizeKeepingCentre(fm.getWidth() - 6, 44));
}

//==============================================================================
FilterPanel::FilterPanel(EditorContext &c, int u)
    : Panel(c, "Filter " + juce::String(u + 1), theme::filter), unit(u)
{
    display = add<FilterDisplay>(ctx, unit);
    type = knob({}, ParamControl::Look::Choice);
    type->setShowLabel(false);
    subtype = knob({}, ParamControl::Look::Choice);
    subtype->setShowLabel(false);
    cutoff = knob("Cutoff");
    resonance = knob("Reso");
    envmod = knob("Env Amt");
    keytrack = knob("Key Trk");
    if (unit == 1)
    {
        offset = knob("Cutoff follows F1", ParamControl::Look::Toggle);
        offset->setTooltip("Filter 2 cutoff becomes an offset from Filter 1, so one knob sweeps both.");
        link = knob("Link resonance", ParamControl::Look::Toggle);
    }
    rebind();
}

void FilterPanel::rebind()
{
    auto &s = ctx.sceneData();
    auto &f = s.filterunit[unit];
    type->setParam(&f.type);
    subtype->setParam(&f.subtype);
    cutoff->setParam(&f.cutoff);
    resonance->setParam(&f.resonance);
    envmod->setParam(&f.envmod);
    keytrack->setParam(&f.keytrack);
    if (offset)
        offset->setParam(&s.f2_cutoff_is_offset);
    if (link)
        link->setParam(&s.f2_link_resonance);
}

void FilterPanel::resized()
{
    auto header = getHeaderBounds();
    subtype->setBounds(header.removeFromRight(72));
    header.removeFromRight(4);
    type->setBounds(header);

    auto b = getContentBounds();
    display->setBounds(b.removeFromTop(96));
    b.removeFromTop(10);

    // both filter panels keep the strip for the Filter 2 switches, so their knobs line up
    auto toggles = b.removeFromBottom(22);
    b.removeFromBottom(8);
    if (offset)
    {
        offset->setBounds(toggles.removeFromLeft(toggles.getWidth() * 55 / 100).reduced(2, 0));
        link->setBounds(toggles.reduced(2, 0));
    }

    auto big = b.removeFromLeft(96);
    cutoff->setBounds(big.withSizeKeepingCentre(90, std::min(big.getHeight(), 104)));
    b.removeFromLeft(4);
    auto row = b.withSizeKeepingCentre(b.getWidth(), std::min(b.getHeight(), 72));
    layoutRow(row, {resonance, envmod, keytrack});
}

//==============================================================================
ShaperPanel::ShaperPanel(EditorContext &c) : Panel(c, "Routing & Shaper", theme::shaper)
{
    routing = knob("Filter Routing", ParamControl::Look::Choice);
    feedback = knob("Feedback");
    feedback->setTooltip("Feeds the filter output back into its input. Available in the Serial 2, "
                         "Serial 3, Dual 2, Stereo, Ring and Wide routings.");
    balance = knob("F1 / F2");
    highpass = knob("High Pass");
    shape = knob("Shaper", ParamControl::Look::Choice);
    drive = knob("Drive");
    rebind();
}

void ShaperPanel::rebind()
{
    auto &s = ctx.sceneData();
    routing->setParam(&s.filterblock_configuration);
    feedback->setParam(&s.feedback);
    balance->setParam(&s.filter_balance);
    highpass->setParam(&s.lowcut);
    shape->setParam(&s.wsunit.type);
    drive->setParam(&s.wsunit.drive);
}

void ShaperPanel::resized()
{
    auto b = getContentBounds();
    routing->setBounds(b.removeFromTop(44).reduced(3, 0));
    b.removeFromTop(10);
    layoutRow(b.removeFromTop(72), {feedback, balance, highpass});
    b.removeFromTop(14);
    auto last = b.removeFromTop(std::min(72, b.getHeight()));
    auto driveArea = last.removeFromRight(last.getWidth() / 3);
    drive->setBounds(driveArea.reduced(3, 0));
    shape->setBounds(last.reduced(3, 0).withSizeKeepingCentre(last.getWidth() - 6, 44));
}

//==============================================================================
EnvPanel::EnvPanel(EditorContext &c, int w)
    : Panel(c, w == adsr_ampeg ? "Amp Envelope" : "Filter Envelope",
            w == adsr_ampeg ? theme::ampEnv : theme::filtEnv),
      which(w)
{
    display = add<EnvDisplay>(ctx, which, getAccent());
    mode = knob({}, ParamControl::Look::Choice);
    mode->setShowLabel(false);
    mode->setTooltip("Analog follows the curves of a capacitor-based envelope circuit. Digital "
                     "gives each stage a selectable curve.");
    a = knob("Attack");
    d = knob("Decay");
    s = knob("Sustain");
    r = knob("Release");
    if (which == adsr_ampeg)
    {
        extra[0] = knob("Velocity");
        extra[1] = knob("Level");
        extra[2] = nullptr;
    }
    else
    {
        extra[0] = knob("A Curve", ParamControl::Look::Choice);
        extra[1] = knob("D Curve", ParamControl::Look::Choice);
        extra[2] = knob("R Curve", ParamControl::Look::Choice);
    }
    rebind();
}

void EnvPanel::rebind()
{
    auto &sc = ctx.sceneData();
    auto &e = sc.adsr[which];
    mode->setParam(&e.mode);
    a->setParam(&e.a);
    d->setParam(&e.d);
    s->setParam(&e.s);
    r->setParam(&e.r);
    if (which == adsr_ampeg)
    {
        extra[0]->setParam(&sc.vca_velsense);
        extra[1]->setParam(&sc.vca_level);
    }
    else
    {
        extra[0]->setParam(&e.a_s);
        extra[1]->setParam(&e.d_s);
        extra[2]->setParam(&e.r_s);
    }
}

void EnvPanel::resized()
{
    auto header = getHeaderBounds();
    mode->setBounds(header.removeFromRight(std::min(78, header.getWidth())));

    auto b = getContentBounds();
    display->setBounds(b.removeFromTop(96));
    b.removeFromTop(10);
    layoutRow(b.removeFromTop(68), {a, d, s, r});
    b.removeFromTop(8);
    auto last = b.removeFromTop(std::min(64, b.getHeight()));
    if (which == adsr_ampeg)
        layoutRow(last, {extra[0], extra[1], nullptr, nullptr});
    else
        layoutRow(last.withSizeKeepingCentre(last.getWidth(), 44), {extra[0], extra[1], extra[2]});
}

//==============================================================================
LfoPanel::LfoPanel(EditorContext &c) : Panel(c, "Modulators", theme::lfo)
{
    for (int i = 0; i < n_lfos; ++i)
    {
        const bool voice = i < n_lfos_voice;
        tabs[i] = add<FlatButton>((voice ? "LFO " : "S-LFO ") + juce::String(voice ? i + 1 : i - n_lfos_voice + 1),
                                  voice ? theme::lfo : theme::sceneLfo);
        tabs[i]->setFontSize(11.5f);
        tabs[i]->onClick = [this, i]() { selectLfo(i); };
        tabs[i]->setTooltip(voice ? "Voice LFO: every note gets its own copy, started when the note starts."
                                  : "Scene LFO: one shared modulator for the whole layer.");
    }
    assign = add<FlatButton>("Assign", theme::lfo);
    assign->setTooltip("Arm this modulator, then drag any knob to set how far it moves it.");
    assign->onClick = [this]() {
        const int ms = ms_lfo1 + current;
        ctx.armedSource = (ctx.armedSource == ms) ? (int)ms_original : ms;
        if (ctx.structureChanged)
            ctx.structureChanged();
    };

    display = add<LfoDisplay>(ctx);
    steps = add<StepEditor>(ctx);
    shape = knob("Shape", ParamControl::Look::Choice);
    rate = knob("Rate");
    phase = knob("Phase");
    amount = knob("Amount");
    deform = knob("Deform");
    trigger = knob("Trigger", ParamControl::Look::Choice);
    unipolar = knob("Unipolar", ParamControl::Look::Toggle);
    static const char *envNames[6] = {"Delay", "Attack", "Hold", "Decay", "Sustain", "Release"};
    for (int i = 0; i < 6; ++i)
        env[i] = knob(envNames[i]);
    rebind();
}

void LfoPanel::selectLfo(int i)
{
    current = juce::jlimit(0, n_lfos - 1, i);
    ctx.proc.editorState.lfo = current;
    rebind();
    resized();
    refresh(true);
}

void LfoPanel::rebind()
{
    auto &l = ctx.sceneData().lfo[current];
    const auto col = current < n_lfos_voice ? theme::lfo : theme::sceneLfo;
    setAccent(col);
    display->setLfo(current);
    steps->setLfo(current);
    shape->setParam(&l.shape);
    rate->setParam(&l.rate);
    phase->setParam(&l.start_phase);
    amount->setParam(&l.magnitude);
    deform->setParam(&l.deform);
    trigger->setParam(&l.trigmode);
    unipolar->setParam(&l.unipolar);
    Parameter *envp[6] = {&l.delay, &l.attack, &l.hold, &l.decay, &l.sustain, &l.release};
    for (int i = 0; i < 6; ++i)
    {
        env[i]->setParam(envp[i]);
        env[i]->setAccent(col);
    }
    for (auto *k : {shape, rate, phase, amount, deform, trigger, unipolar})
        k->setAccent(col);
    assign->setAccent(col);
}

void LfoPanel::refresh(bool force)
{
    const bool wantSteps = ctx.sceneData().lfo[current].shape.val.i == lt_stepseq;
    if (wantSteps != showingSteps)
    {
        showingSteps = wantSteps;
        resized();
        force = true;
    }

    for (int i = 0; i < n_lfos; ++i)
    {
        tabs[i]->setOn(i == current);
        tabs[i]->setMarker(ctx.synth->isModsourceUsed((modsources)(ms_lfo1 + i)));
    }
    const bool armed = ctx.armedSource == ms_lfo1 + current;
    assign->setOn(armed);
    assign->setText(armed ? "Assigning..." : "Assign");
    Panel::refresh(force);
}

void LfoPanel::resized()
{
    auto b = getContentBounds();
    b.removeFromTop(2);
    auto tabRow = b.removeFromTop(26);
    auto assignArea = tabRow.removeFromRight(96);
    assign->setBounds(assignArea);
    tabRow.removeFromRight(10);
    const float tw = (float)(tabRow.getWidth() - 12) / (float)n_lfos;
    for (int i = 0; i < n_lfos; ++i)
    {
        const int gap = i >= n_lfos_voice ? 12 : 0;
        tabs[i]->setBounds(juce::Rectangle<int>(tabRow.getX() + gap + (int)(tw * (float)i), tabRow.getY(),
                                                (int)tw - 3, tabRow.getHeight()));
    }
    b.removeFromTop(10);

    auto envRow = b.removeFromBottom(70);
    b.removeFromBottom(10);
    auto mainRow = b.removeFromBottom(70);
    b.removeFromBottom(12);

    if (showingSteps)
    {
        auto half = b.removeFromBottom(b.getHeight() * 45 / 100);
        b.removeFromBottom(6);
        steps->setBounds(half);
        steps->setVisible(true);
    }
    else
    {
        steps->setVisible(false);
    }
    display->setBounds(b);

    auto shapeArea = mainRow.removeFromLeft(150);
    shape->setBounds(shapeArea.reduced(3, 0).withSizeKeepingCentre(shapeArea.getWidth() - 6, 44));
    auto trigArea = mainRow.removeFromRight(210);
    auto uniArea = trigArea.removeFromRight(84);
    unipolar->setBounds(uniArea.reduced(3, 0).withSizeKeepingCentre(uniArea.getWidth() - 6, 26).translated(0, -8));
    trigger->setBounds(trigArea.reduced(3, 0).withSizeKeepingCentre(trigArea.getWidth() - 6, 44));
    layoutRow(mainRow, {rate, phase, amount, deform});
    layoutRow(envRow, {env[0], env[1], env[2], env[3], env[4], env[5]});
}

//==============================================================================
MacroPanel::MacroPanel(EditorContext &c) : Panel(c, "Macros", theme::macro)
{
    for (int i = 0; i < n_customcontrollers; ++i)
    {
        auto *k = add<ValueKnob>(theme::macro, "Macro " + juce::String(i + 1));
        k->getValue = [this, i]() { return ctx.synth->getMacroParameterTarget01(i); };
        k->setValue = [this, i](float v) {
            ctx.synth->setMacroParameter01(i, v);
            if (i < (int)ctx.proc.macroParams.size())
                ctx.proc.macroParams[(size_t)i]->setValueNotifyingHost(v);
        };
        k->gesture = [this, i](bool begin) {
            if (i >= (int)ctx.proc.macroParams.size())
                return;
            if (begin)
                ctx.proc.macroParams[(size_t)i]->beginChangeGesture();
            else
                ctx.proc.macroParams[(size_t)i]->endChangeGesture();
        };
        k->extendMenu = [this, i](juce::PopupMenu &m) {
            m.addItem("Rename...", [this, i]() { rename(i); });
        };
        knobs[i] = k;

        auto *a = add<FlatButton>("Assign", theme::macro);
        a->setFontSize(10.5f);
        a->setTooltip("Arm this macro, then drag any knob to set how far the macro moves it. "
                      "Right-click to rename the macro.");
        a->onClick = [this, i]() {
            const int ms = ms_ctrl1 + i;
            ctx.armedSource = (ctx.armedSource == ms) ? (int)ms_original : ms;
            if (ctx.structureChanged)
                ctx.structureChanged();
        };
        a->onRightClick = [this, i]() { rename(i); };
        assign[i] = a;
    }
}

void MacroPanel::rename(int i)
{
    // an in-place text box over the macro's caption
    renameBox = std::make_unique<juce::TextEditor>();
    auto area = knobs[i]->getBounds().removeFromBottom(20).expanded(14, 2);
    addAndMakeVisible(*renameBox);
    renameBox->setBounds(area.getIntersection(getLocalBounds().reduced(4)));
    renameBox->setFont(theme::font(12.f));
    renameBox->setJustification(juce::Justification::centred);
    renameBox->setInputRestrictions(CUSTOM_CONTROLLER_LABEL_SIZE - 1);
    juce::String current = ctx.patch().CustomControllerLabel[i];
    renameBox->setText(current == "-" ? juce::String() : current, false);
    renameBox->selectAll();
    renameBox->grabKeyboardFocus();

    juce::Component::SafePointer<MacroPanel> safe(this);
    auto close = [safe]() {
        juce::MessageManager::callAsync([safe]() {
            if (safe)
            {
                safe->renameBox.reset();
                safe->refresh(true);
            }
        });
    };
    renameBox->onReturnKey = [safe, close, i]() {
        if (safe && safe->renameBox)
        {
            auto name = safe->renameBox->getText().trim();
            if (name.isEmpty())
                name = "Macro " + juce::String(i + 1);
            auto &label = safe->ctx.patch().CustomControllerLabel[i];
            memset(label, 0, CUSTOM_CONTROLLER_LABEL_SIZE);
            name.copyToUTF8(label, CUSTOM_CONTROLLER_LABEL_SIZE - 1);
            safe->ctx.markDirty();
            parameterInfoUpdated = true; // the host shows macro names too
        }
        close();
    };
    renameBox->onEscapeKey = close;
    renameBox->onFocusLost = close;
}

void MacroPanel::refresh(bool force)
{
    for (int i = 0; i < n_customcontrollers; ++i)
    {
        juce::String name = ctx.patch().CustomControllerLabel[i];
        if (name.isEmpty() || name == "-")
            name = "Macro " + juce::String(i + 1);
        knobs[i]->label = name;
        const bool armed = ctx.armedSource == ms_ctrl1 + i;
        assign[i]->setOn(armed);
        assign[i]->setMarker(ctx.synth->isModsourceUsed((modsources)(ms_ctrl1 + i)));
    }
    Panel::refresh(force);
}

void MacroPanel::resized()
{
    auto b = getContentBounds();
    const float cell = (float)b.getWidth() / n_customcontrollers;
    for (int i = 0; i < n_customcontrollers; ++i)
    {
        auto c = juce::Rectangle<int>(b.getX() + (int)(cell * (float)i), b.getY(), (int)cell, b.getHeight())
                     .reduced(4, 0);
        auto btn = c.removeFromBottom(22).withSizeKeepingCentre(std::min(c.getWidth(), 64), 20);
        assign[i]->setBounds(btn);
        c.removeFromBottom(6);
        knobs[i]->setBounds(c.withSizeKeepingCentre(std::min(c.getWidth(), 84), std::min(c.getHeight(), 84)));
    }
}

//==============================================================================
class MatrixPanel::RowComponent : public juce::Component
{
  public:
    RowComponent(MatrixPanel &o) : owner(o), remove({}, theme::danger), mute("M", theme::textDim)
    {
        remove.setGlyph(FlatButton::Glyph::Close);
        remove.setQuiet(true);
        remove.setTooltip("Remove this routing");
        remove.onClick = [this]() {
            auto &c = owner.ctx;
            c.synth->clearModulation(row.paramId, (modsources)row.source, c.scene, 0, false);
            c.markDirty();
            if (c.structureChanged)
                c.structureChanged();
        };
        mute.setFontSize(10.5f);
        mute.setTooltip("Mute this routing without losing its depth");
        mute.onClick = [this]() {
            auto &c = owner.ctx;
            const bool m = c.synth->isModulationMuted(row.paramId, (modsources)row.source, c.scene, 0);
            c.synth->muteModulation(row.paramId, (modsources)row.source, c.scene, 0, !m);
            c.markDirty();
            repaint();
        };
        addAndMakeVisible(remove);
        addAndMakeVisible(mute);
    }

    void setRow(const Row &r)
    {
        row = r;
        repaint();
    }

    void resized() override
    {
        auto b = getLocalBounds().reduced(2, 3);
        remove.setBounds(b.removeFromRight(22));
        b.removeFromRight(2);
        mute.setBounds(b.removeFromRight(22));
    }

    juce::Rectangle<int> sliderArea() const
    {
        return getLocalBounds().reduced(2, 3).withTrimmedRight(50).removeFromRight(150);
    }

    void paint(juce::Graphics &g) override
    {
        auto &c = owner.ctx;
        auto *p = c.patch().param_ptr[(size_t)row.paramId];
        const bool muted = c.synth->isModulationMuted(row.paramId, (modsources)row.source, c.scene, 0);
        mute.setOn(muted);
        const auto col = EditorContext::colourForSource(row.source);

        auto b = getLocalBounds().reduced(2, 2).toFloat();
        g.setColour(theme::well.withAlpha(0.55f));
        g.fillRoundedRectangle(b, 5.f);

        auto text = getLocalBounds().reduced(10, 0).withTrimmedRight(204);
        auto src = text.removeFromLeft(text.getWidth() * 36 / 100);
        g.setFont(theme::font(12.5f, true));
        g.setColour(muted ? theme::textFaint : col);
        g.drawFittedText(c.nameForSource(row.source), src, juce::Justification::centredLeft, 1, 0.75f);
        g.setFont(theme::font(12.5f));
        g.setColour(muted ? theme::textFaint : theme::text);
        g.drawFittedText(p->get_full_name(), text.withTrimmedLeft(6), juce::Justification::centredLeft, 1, 0.7f);

        // depth slider: centre is zero
        auto s = sliderArea().toFloat();
        auto track = s.withSizeKeepingCentre(s.getWidth(), 4.f).translated(0.f, 7.f);
        g.setColour(theme::track);
        g.fillRoundedRectangle(track, 2.f);
        const float d = juce::jlimit(-1.f, 1.f, c.synth->getModDepth01(row.paramId, (modsources)row.source, c.scene, 0));
        const float cx = track.getCentreX();
        const float x = cx + d * track.getWidth() * 0.5f;
        g.setColour(muted ? theme::textFaint : col);
        g.fillRoundedRectangle(juce::Rectangle<float>(std::min(cx, x), track.getY(), std::abs(x - cx), track.getHeight()), 2.f);
        g.fillEllipse(juce::Rectangle<float>(9.f, 9.f).withCentre({x, track.getCentreY()}));
        g.setColour(theme::textFaint);
        g.fillRect(cx - 0.5f, track.getY() - 3.f, 1.f, track.getHeight() + 6.f);

        char txt[TXT_SIZE];
        p->get_display_of_modulation_depth(
            txt, c.synth->getModDepth(row.paramId, (modsources)row.source, c.scene, 0),
            c.synth->isBipolarModulation((modsources)row.source), Parameter::Menu);
        g.setColour(theme::textDim);
        g.setFont(theme::font(10.5f));
        g.drawFittedText(txt, s.toNearestInt().withTrimmedBottom(13), juce::Justification::centred, 1, 0.7f);
    }

    void mouseDown(const juce::MouseEvent &e) override
    {
        dragging = sliderArea().expanded(4, 0).contains(e.getPosition());
        last = e.position;
    }

    void mouseDrag(const juce::MouseEvent &e) override
    {
        if (!dragging)
            return;
        auto &c = owner.ctx;
        const float dx = e.position.x - last.x;
        last = e.position;
        const float w = (float)sliderArea().getWidth() * 0.5f;
        const float cur = c.synth->getModDepth01(row.paramId, (modsources)row.source, c.scene, 0);
        const float nv = juce::jlimit(-1.f, 1.f, cur + dx / w * (e.mods.isShiftDown() ? 0.1f : 1.f));
        c.synth->setModDepth01(row.paramId, (modsources)row.source, c.scene, 0, nv);
        c.markDirty();
        repaint();
    }

    void mouseDoubleClick(const juce::MouseEvent &e) override
    {
        if (!sliderArea().contains(e.getPosition()))
            return;
        auto &c = owner.ctx;
        c.synth->setModDepth01(row.paramId, (modsources)row.source, c.scene, 0, 0.f);
        c.markDirty();
        repaint();
    }

  private:
    MatrixPanel &owner;
    Row row{0, 0};
    FlatButton remove, mute;
    bool dragging{false};
    juce::Point<float> last;
};

MatrixPanel::MatrixPanel(EditorContext &c) : Panel(c, "Modulation Matrix", theme::voice)
{
    list.setModel(this);
    list.setRowHeight(38);
    list.setColour(juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    addAndMakeVisible(list);
    rebuild();
}

void MatrixPanel::rebuild()
{
    rows.clear();
    auto &patch = ctx.patch();
    for (int ms : modsource_display_order)
    {
        if (ms == ms_original)
            continue;
        for (auto *p : patch.param_ptr)
        {
            if (!p || p->valtype != vt_float)
                continue;
            if (p->scene != 0 && p->scene != ctx.scene + 1)
                continue;
            if (ctx.synth->isActiveModulation(p->id, (modsources)ms, ctx.scene, 0))
                rows.push_back({ms, p->id});
        }
    }
    list.updateContent();
    list.repaint();
    repaint();
}

juce::Component *MatrixPanel::refreshComponentForRow(int row, bool, juce::Component *existing)
{
    auto *rc = dynamic_cast<RowComponent *>(existing);
    if (row < 0 || row >= (int)rows.size())
    {
        delete existing;
        return nullptr;
    }
    if (!rc)
    {
        delete existing;
        rc = new RowComponent(*this);
    }
    rc->setRow(rows[(size_t)row]);
    return rc;
}

void MatrixPanel::refresh(bool force)
{
    if (force)
        rebuild();
    Panel::refresh(force);
}

void MatrixPanel::resized() { list.setBounds(getContentBounds()); }

void MatrixPanel::paintOverChildren(juce::Graphics &g)
{
    if (!rows.empty())
        return;
    auto b = getContentBounds().reduced(24, 0);
    g.setColour(theme::textDim);
    g.setFont(theme::font(14.f, true));
    g.drawFittedText("Nothing is being modulated yet", b.withHeight(80).translated(0, 60),
                     juce::Justification::centred, 1);
    g.setColour(theme::textFaint);
    g.setFont(theme::font(13.f));
    g.drawFittedText("Click a source in the Modulate strip above (an envelope, an LFO, a macro, "
                     "velocity...) to arm it, then drag any knob. The ring that appears is how "
                     "far that source will move the knob. Click the source again when you are "
                     "done.\n\nEvery routing you make is listed here.",
                     b.withHeight(150).translated(0, 122), juce::Justification::centredTop, 8);
}

//==============================================================================
FxPanel::FxPanel(EditorContext &c) : Panel(c, "Effects", theme::fx)
{
    current = juce::jlimit(0, n_fx_slots - 1, ctx.proc.editorState.fxSlot);
    for (int i = 0; i < n_fx_slots; ++i)
    {
        slots[i] = add<FlatButton>("", theme::fx);
        slots[i]->setFontSize(12.f);
        slots[i]->onClick = [this, i]() { selectSlot(i); };
        slots[i]->onRightClick = [this, i]() { toggleBypass(i); };
        slots[i]->setTooltip("Click to edit. Right-click to bypass.");
    }
    chainBypass = knob("Active Chains", ParamControl::Look::Choice);
    for (int i = 0; i < n_send_slots; ++i)
        sends[i] = knob("Send " + juce::String(i + 1));
    type = knob({}, ParamControl::Look::Choice);
    type->setShowLabel(false);
    type->choiceFilter = [](int v) { return v != fxt_audio_input; };
    returnLevel = knob("Return");
    for (auto &p : params)
        p = knob();
    rebind();
}

void FxPanel::selectSlot(int slot)
{
    current = slot;
    ctx.proc.editorState.fxSlot = slot;
    rebind();
    resized();
    refresh(true);
}

void FxPanel::toggleBypass(int slot)
{
    auto &p = ctx.patch().fx_disable;
    p.val.i ^= (1 << slot);
    ctx.synth->fx_suspend_bitmask = p.val.i;
    ctx.markDirty();
    refresh(true);
}

void FxPanel::rebind()
{
    auto &patch = ctx.patch();
    auto &fx = patch.fx[current];
    chainBypass->setParam(&patch.fx_bypass);
    for (int i = 0; i < n_send_slots; ++i)
        sends[i]->setParam(&ctx.sceneData().send_level[i]);
    type->setParam(&fx.type);
    returnLevel->setParam(&fx.return_level);
    for (int i = 0; i < n_fx_params; ++i)
        params[i]->setParam(&fx.p[i]);
}

void FxPanel::refresh(bool force)
{
    auto &patch = ctx.patch();
    for (int i = 0; i < n_fx_slots; ++i)
    {
        const int t = juce::jlimit(0, (int)n_fx_types - 1, patch.fx[i].type.val.i);
        const bool bypassed = (patch.fx_disable.val.i & (1 << i)) != 0;
        juce::String name = t == fxt_off ? "Empty" : fx_type_shortnames[t];
        if (bypassed && t != fxt_off)
            name += "  (bypassed)";
        slots[i]->setText(name);
        slots[i]->setOn(i == current);
        slots[i]->setMarker(t != fxt_off && !bypassed);
    }
    const bool isSend = current == fxslot_send1 || current == fxslot_send2 ||
                        current == fxslot_send3 || current == fxslot_send4;
    returnLevel->setVisible(isSend);
    if (force)
        repaint();
    Panel::refresh(force);
}

void FxPanel::resized()
{
    auto b = getContentBounds();
    auto left = b.removeFromLeft(430);
    b.removeFromLeft(18);
    editorArea = b;

    // the four chains
    static const int chainSlots[n_fx_chains][n_fx_per_chain] = {
        {fxslot_ains1, fxslot_ains2, fxslot_ains3, fxslot_ains4},
        {fxslot_bins1, fxslot_bins2, fxslot_bins3, fxslot_bins4},
        {fxslot_send1, fxslot_send2, fxslot_send3, fxslot_send4},
        {fxslot_global1, fxslot_global2, fxslot_global3, fxslot_global4}};

    left.removeFromTop(4);
    for (int c = 0; c < n_fx_chains; ++c)
    {
        chainLabelArea[c] = left.removeFromTop(20);
        auto row = left.removeFromTop(34);
        const int w = (row.getWidth() - 3 * 6) / 4;
        for (int s = 0; s < n_fx_per_chain; ++s)
        {
            slots[chainSlots[c][s]]->setBounds(row.removeFromLeft(w));
            row.removeFromLeft(6);
        }
        left.removeFromTop(12);
    }

    left.removeFromTop(8);
    sendsLabelArea = left.removeFromTop(20);
    auto sendRow = left.removeFromTop(72);
    layoutRow(sendRow, {sends[0], sends[1], sends[2], sends[3]});
    left.removeFromTop(16);
    chainBypass->setBounds(left.removeFromTop(44).removeFromLeft(210));

    // the selected effect
    auto e = editorArea;
    auto head = e.removeFromTop(30);
    head.removeFromLeft(170);
    type->setBounds(head.removeFromLeft(190).reduced(0, 1));
    e.removeFromTop(22);
    const int rowH = 92;
    for (int r = 0; r < 2; ++r)
    {
        auto row = e.removeFromTop(rowH);
        e.removeFromTop(22);
        layoutRow(row, {params[r * 6 + 0], params[r * 6 + 1], params[r * 6 + 2],
                        params[r * 6 + 3], params[r * 6 + 4], params[r * 6 + 5]});
    }
    returnLevel->setBounds(e.removeFromTop(rowH).removeFromLeft(editorArea.getWidth() / 6).reduced(3, 0));
}

void FxPanel::paint(juce::Graphics &g)
{
    Section::paint(g);

    static const char *chainNames[n_fx_chains] = {"Layer A inserts", "Layer B inserts",
                                                  "Send effects  (fed by the Send knobs below)",
                                                  "Master effects"};
    g.setFont(theme::labelFont(10.f));
    g.setColour(theme::textDim);
    for (int c = 0; c < n_fx_chains; ++c)
        g.drawText(juce::String(chainNames[c]).toUpperCase(), chainLabelArea[c],
                   juce::Justification::centredLeft);
    g.drawText(juce::String("SENDS FROM LAYER ") + (ctx.scene == 0 ? "A" : "B"), sendsLabelArea,
               juce::Justification::centredLeft);

    // divider and title of the slot being edited
    g.setColour(theme::outline);
    g.drawVerticalLine(editorArea.getX() - 9, (float)editorArea.getY(), (float)editorArea.getBottom());
    g.setColour(theme::text);
    g.setFont(theme::font(15.f, true));
    g.drawText(fxslot_names[current], editorArea.withHeight(30), juce::Justification::centredLeft);

    if (ctx.patch().fx[current].type.val.i == fxt_off)
    {
        g.setColour(theme::textFaint);
        g.setFont(theme::font(13.f));
        g.drawFittedText("This slot is empty. Choose an effect from the selector above.",
                         editorArea.withTrimmedTop(70).withHeight(40), juce::Justification::centredLeft, 2);
    }
}

//==============================================================================
VoicePanel::VoicePanel(EditorContext &c) : Panel(c, "Voice", theme::voice)
{
    mode = knob("Play Mode", ParamControl::Look::Choice);
    glide = knob("Glide");
    bendUp = knob("Bend +", ParamControl::Look::Knob);
    bendDown = knob("Bend -", ParamControl::Look::Knob);

    auto &proc = ctx.proc;
    vintage = add<ValueKnob>(theme::osc, "Vintage");
    vintage->defaultValue = 0.2f;
    vintage->getValue = [&proc]() { return proc.vintageParam->get(); };
    vintage->setValue = [&proc](float v) { proc.vintageParam->setValueNotifyingHost(v); };
    vintage->gesture = [&proc](bool b) {
        b ? proc.vintageParam->beginChangeGesture() : proc.vintageParam->endChangeGesture();
    };
    vintage->setTooltip("Voice-to-voice variance. Every note gets its own slightly different "
                        "tuning and filter cutoff, like the voice cards of a vintage polysynth.");

    warmth = add<ValueKnob>(theme::filter, "Warmth");
    warmth->getValue = [&proc]() { return proc.warmthParam->get(); };
    warmth->setValue = [&proc](float v) { proc.warmthParam->setValueNotifyingHost(v); };
    warmth->gesture = [&proc](bool b) {
        b ? proc.warmthParam->beginChangeGesture() : proc.warmthParam->endChangeGesture();
    };
    warmth->setTooltip("Drives the output stage: soft, slightly asymmetric saturation that "
                       "thickens the lows and rounds off peaks.");
    rebind();
}

void VoicePanel::rebind()
{
    auto &s = ctx.sceneData();
    mode->setParam(&s.polymode);
    glide->setParam(&s.portamento);
    bendUp->setParam(&s.pbrange_up);
    bendDown->setParam(&s.pbrange_dn);
}

void VoicePanel::resized()
{
    auto b = getContentBounds();
    auto modeArea = b.removeFromLeft(112);
    mode->setBounds(modeArea.reduced(2, 0).withSizeKeepingCentre(modeArea.getWidth() - 4, 44));
    layoutRow(b, {glide, bendUp, bendDown, vintage, warmth});
}

//==============================================================================
OutputPanel::OutputPanel(EditorContext &c) : Panel(c, "Output", theme::voice)
{
    volume = knob("Layer Vol");
    pan = knob("Pan");
    width = knob("Width");
    master = knob("Master");
    rebind();
}

void OutputPanel::rebind()
{
    auto &s = ctx.sceneData();
    volume->setParam(&s.volume);
    pan->setParam(&s.pan);
    width->setParam(&s.width);
    master->setParam(&ctx.patch().volume);
}

void OutputPanel::resized() { layoutRow(getContentBounds(), {volume, pan, width, master}); }
} // namespace deasynth
