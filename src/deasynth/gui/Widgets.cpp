/*
 * Deasynth - an analog-voiced hybrid synthesizer built on the Surge XT engine.
 *
 * Deasynth is released under the GNU General Public Licence v3 or later (GPL-3.0-or-later).
 * The license is found in the "LICENSE" file in the root of this repository, or at
 * https://www.gnu.org/licenses/gpl-3.0.en.html
 */

#include "Widgets.h"
#include "../DeasynthProcessor.h"

namespace deasynth
{
//==============================================================================
juce::Colour EditorContext::colourForSource(int ms)
{
    if (ms == ms_ampeg)
        return theme::ampEnv;
    if (ms == ms_filtereg)
        return theme::filtEnv;
    if (ms >= ms_lfo1 && ms <= ms_lfo6)
        return theme::lfo;
    if (ms >= ms_slfo1 && ms <= ms_slfo6)
        return theme::sceneLfo;
    if (ms >= ms_ctrl1 && ms <= ms_ctrl8)
        return theme::macro;
    return theme::voice;
}

juce::String EditorContext::nameForSource(int ms, bool shortName) const
{
    if (ms <= ms_original || ms >= n_modsources)
        return {};

    if (ms >= ms_ctrl1 && ms <= ms_ctrl8)
    {
        if (shortName)
            return "M" + juce::String(ms - ms_ctrl1 + 1);
        juce::String label = patch().CustomControllerLabel[ms - ms_ctrl1];
        if (label.isEmpty() || label == "-")
            return "Macro " + juce::String(ms - ms_ctrl1 + 1);
        return label;
    }

    if (!shortName)
        return modsource_names_button[ms];

    switch (ms)
    {
    case ms_velocity:
        return "VEL";
    case ms_keytrack:
        return "KEY";
    case ms_polyaftertouch:
        return "P-AT";
    case ms_aftertouch:
        return "AT";
    case ms_pitchbend:
        return "BEND";
    case ms_modwheel:
        return "WHEEL";
    case ms_ampeg:
        return "AMP EG";
    case ms_filtereg:
        return "FILT EG";
    case ms_random_bipolar:
        return "RAND";
    case ms_alternate_bipolar:
        return "ALT";
    case ms_timbre:
        return "TIMBRE";
    case ms_releasevelocity:
        return "REL V";
    default:
        break;
    }
    if (ms >= ms_lfo1 && ms <= ms_lfo6)
        return juce::String(ms - ms_lfo1 + 1);
    if (ms >= ms_slfo1 && ms <= ms_slfo6)
        return juce::String(ms - ms_slfo1 + 1);
    return modsource_names_button[ms];
}

void EditorContext::beginEdit(Parameter *p) const
{
    if (auto *hp = proc.hostParamFor(p))
    {
        hp->inEditGesture = true;
        hp->beginChangeGesture();
    }
}

void EditorContext::setValue01(Parameter *p, float v) const
{
    if (!p)
        return;
    synth->setParameter01(synth->idForParameter(p), juce::jlimit(0.f, 1.f, v));
    if (auto *hp = proc.hostParamFor(p))
        hp->setValueNotifyingHost(p->get_value_f01());
}

void EditorContext::endEdit(Parameter *p) const
{
    if (auto *hp = proc.hostParamFor(p))
    {
        hp->endChangeGesture();
        hp->inEditGesture = false;
    }
}

juce::Component *menuParentFor(juce::Component &c)
{
    for (auto *p = &c; p != nullptr; p = p->getParentComponent())
    {
        if (p->getComponentID() == "deasynth-root")
            return p;
    }
    return c.getTopLevelComponent();
}

//==============================================================================
namespace draw
{
static juce::Path arc(juce::Point<float> c, float r, float a0, float a1)
{
    juce::Path p;
    p.addCentredArc(c.x, c.y, r, r, 0.f, a0, a1, true);
    return p;
}

void knob(juce::Graphics &g, juce::Rectangle<float> area, float v, bool bipolar,
          juce::Colour accent, bool enabled, bool hot)
{
    const float size = std::min(area.getWidth(), area.getHeight());
    const auto c = area.getCentre();
    const float r = size * 0.5f - 3.f;
    const float thick = juce::jlimit(2.4f, 4.5f, size * 0.075f);
    const float sweep = knobEnd - knobStart;
    v = juce::jlimit(0.f, 1.f, v);
    const float angle = knobStart + v * sweep;

    // track
    g.setColour(theme::track);
    g.strokePath(arc(c, r, knobStart, knobEnd),
                 juce::PathStrokeType(thick, juce::PathStrokeType::curved,
                                      juce::PathStrokeType::rounded));

    // value
    const auto lit = enabled ? accent : theme::textFaint;
    const float from = bipolar ? (knobStart + 0.5f * sweep) : knobStart;
    if (std::abs(angle - from) > 0.01f)
    {
        g.setColour(lit);
        g.strokePath(arc(c, r, std::min(from, angle), std::max(from, angle)),
                     juce::PathStrokeType(thick, juce::PathStrokeType::curved,
                                          juce::PathStrokeType::rounded));
    }
    else if (bipolar)
    {
        g.setColour(lit);
        g.fillEllipse(juce::Rectangle<float>(thick, thick).withCentre(c.getPointOnCircumference(r, from)));
    }

    // cap
    const float capR = r - thick - 2.5f;
    if (capR > 3.f)
    {
        auto cap = juce::Rectangle<float>(capR * 2.f, capR * 2.f).withCentre(c);
        g.setColour(juce::Colours::black.withAlpha(0.35f));
        g.fillEllipse(cap.translated(0.f, 1.5f));
        g.setGradientFill(juce::ColourGradient(hot ? theme::track : theme::panelHi.brighter(0.12f),
                                               c.x, cap.getY(), theme::chassis, c.x,
                                               cap.getBottom(), false));
        g.fillEllipse(cap);
        g.setColour(juce::Colours::white.withAlpha(0.07f));
        g.drawEllipse(cap.reduced(0.5f), 1.f);

        // pointer
        juce::Path ptr;
        ptr.startNewSubPath(c.getPointOnCircumference(capR * 0.38f, angle));
        ptr.lineTo(c.getPointOnCircumference(capR * 0.86f, angle));
        g.setColour(enabled ? theme::text : theme::textFaint);
        g.strokePath(ptr, juce::PathStrokeType(juce::jlimit(1.6f, 2.6f, size * 0.045f),
                                               juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));
    }
}

void modRing(juce::Graphics &g, juce::Rectangle<float> area, float v, float depth,
             juce::Colour colour)
{
    const float size = std::min(area.getWidth(), area.getHeight());
    const auto c = area.getCentre();
    const float r = size * 0.5f - 3.f;
    const float thick = juce::jlimit(2.4f, 4.5f, size * 0.075f);
    const float sweep = knobEnd - knobStart;
    const float a0 = knobStart + juce::jlimit(0.f, 1.f, v) * sweep;
    const float a1 = knobStart + juce::jlimit(0.f, 1.f, v + depth) * sweep;
    const float rr = r - thick - 0.5f;

    g.setColour(colour);
    if (std::abs(a1 - a0) > 0.01f)
    {
        g.strokePath(arc(c, rr, std::min(a0, a1), std::max(a0, a1)),
                     juce::PathStrokeType(2.2f, juce::PathStrokeType::curved,
                                          juce::PathStrokeType::rounded));
    }
    g.fillEllipse(juce::Rectangle<float>(4.4f, 4.4f).withCentre(c.getPointOnCircumference(rr, a1)));
}

juce::Path heart(juce::Rectangle<float> a)
{
    const float x = a.getX(), y = a.getY(), w = a.getWidth(), h = a.getHeight();
    juce::Path p;
    p.startNewSubPath(x + w * 0.5f, y + h * 0.95f);
    p.cubicTo(x + w * 0.10f, y + h * 0.66f, x - w * 0.04f, y + h * 0.34f, x + w * 0.10f, y + h * 0.16f);
    p.cubicTo(x + w * 0.24f, y - h * 0.02f, x + w * 0.44f, y + h * 0.04f, x + w * 0.5f, y + h * 0.26f);
    p.cubicTo(x + w * 0.56f, y + h * 0.04f, x + w * 0.76f, y - h * 0.02f, x + w * 0.90f, y + h * 0.16f);
    p.cubicTo(x + w * 1.04f, y + h * 0.34f, x + w * 0.90f, y + h * 0.66f, x + w * 0.5f, y + h * 0.95f);
    p.closeSubPath();
    return p;
}

void displayWell(juce::Graphics &g, juce::Rectangle<float> r)
{
    g.setColour(theme::well);
    g.fillRoundedRectangle(r, 5.f);
    g.setColour(juce::Colours::black.withAlpha(0.5f));
    g.drawRoundedRectangle(r.reduced(0.5f), 5.f, 1.f);
}

juce::String shortLabel(const juce::String &name)
{
    static const std::pair<const char *, const char *> swaps[] = {
        {"Unison", "Uni"},         {"Resonance", "Reso"},     {"Feedback", "Feedbk"},
        {"Frequency", "Freq"},     {"Modulation", "Mod"},     {"Modulator", "Mod"},
        {"Amount", "Amt"},         {"Bandwidth", "Width"},    {"Saturation", "Satur"},
        {"Diffusion", "Diffuse"},  {"Pre-Delay", "Predelay"}, {"Decay Time", "Decay"},
        {"Crossfeed", "X-feed"},   {"Threshold", "Thresh"},   {"Oscillator", "Osc"},
        {"Stiffness", "Stiff"},    {"Exciter", "Excite"},     {"Position", "Pos"},
        {"Asymmetry", "Asym"},     {"Harmonics", "Harm"},     {"Response", "Resp"},
        {"Interpolation", "Interp"}, {"Low Cut", "Lo Cut"},   {"High Cut", "Hi Cut"},
        {"Stereo ", "St "},        {"Intensity", "Intens"},   {"Balance", "Bal"},
    };
    juce::String out = name;
    if (out.length() > 9)
    {
        for (auto &[from, to] : swaps)
            out = out.replace(from, to, true);
    }
    return out;
}

void captionBelow(juce::Graphics &g, juce::Rectangle<int> area, const juce::String &text,
                  juce::Colour c)
{
    g.setColour(c);
    g.setFont(theme::labelFont(9.5f));
    g.drawFittedText(shortLabel(text).toUpperCase(), area, juce::Justification::centred, 1, 0.6f);
}
} // namespace draw

//==============================================================================
ParamControl::ParamControl(EditorContext &c, juce::Colour acc, Look l)
    : ctx(c), accent(acc), look(l)
{
    setRepaintsOnMouseActivity(false);
}

ParamControl::~ParamControl()
{
    if (inGesture)
        endGesture();
}

void ParamControl::setParam(Parameter *p)
{
    if (inGesture)
        endGesture();
    param = p;
    typeIn.reset();
    refresh(true);
}

bool ParamControl::usable() const { return param && param->ctrltype != ct_none; }

ParamControl::Look ParamControl::resolvedLook() const
{
    if (look != Look::Auto || !param)
        return look == Look::Auto ? Look::Knob : look;
    if (param->valtype == vt_bool)
        return Look::Toggle;
    if (param->valtype == vt_int)
        return Look::Choice;
    return Look::Knob;
}

bool ParamControl::canModulateWithArmed() const
{
    return usable() && ctx.isArmed() && param->valtype == vt_float &&
           ctx.synth->isValidModulation(param->id, (modsources)ctx.armedSource);
}

float ParamControl::value01() const { return param ? param->get_value_f01() : 0.f; }

juce::String ParamControl::labelText() const
{
    if (labelOverride.isNotEmpty())
        return labelOverride;
    return param ? juce::String(param->get_name()) : juce::String();
}

void ParamControl::beginGesture()
{
    if (inGesture || !param)
        return;
    inGesture = true;
    if (auto *hp = ctx.proc.hostParamFor(param))
    {
        hp->inEditGesture = true;
        hp->beginChangeGesture();
    }
}

void ParamControl::endGesture()
{
    if (!inGesture)
        return;
    inGesture = false;
    if (auto *hp = ctx.proc.hostParamFor(param))
    {
        hp->endChangeGesture();
        hp->inEditGesture = false;
    }
}

void ParamControl::setValue01(float v, bool forceInteger)
{
    if (!usable())
        return;
    v = juce::jlimit(0.f, 1.f, v);
    const bool wasInGesture = inGesture;
    if (!wasInGesture)
        beginGesture();

    const bool structural =
        ctx.synth->setParameter01(ctx.synth->idForParameter(param), v, false, forceInteger);

    if (auto *hp = ctx.proc.hostParamFor(param))
        hp->setValueNotifyingHost(value01());

    if (!wasInGesture)
        endGesture();

    if (onChange)
        onChange();
    if ((structural || param->affect_other_parameters || param->valtype != vt_float) &&
        ctx.structureChanged)
        ctx.structureChanged();
    repaint();
}

void ParamControl::setIntValue(int i)
{
    if (!usable())
        return;
    i = juce::jlimit(param->val_min.i, param->val_max.i, i);
    if (param->valtype == vt_bool)
        setValue01(i ? 1.f : 0.f);
    else
        setValue01(Parameter::intScaledToFloat(i, param->val_max.i, param->val_min.i), true);
}

void ParamControl::refresh(bool force)
{
    if (!param)
    {
        if (force)
            repaint();
        return;
    }

    const bool armedValid = canModulateWithArmed();
    const float mod = armedValid ? ctx.synth->getModDepth01(param->id, (modsources)ctx.armedSource,
                                                           ctx.scene, 0)
                                 : 0.f;
    const bool modUsed = param->valtype == vt_float && ctx.synth->isModDestUsed(param->id);
    const bool deact = param->appears_deactivated();
    const char *nm = param->get_name();

    if (force || param->val.i != cachedRaw || param->ctrltype != cachedType || mod != cachedMod ||
        modUsed != cachedModUsed || deact != cachedDeact || armedValid != cachedArmedValid ||
        cachedName != nm)
    {
        cachedRaw = param->val.i;
        cachedType = param->ctrltype;
        cachedMod = mod;
        cachedModUsed = modUsed;
        cachedDeact = deact;
        cachedArmedValid = armedValid;
        cachedName = nm;
        repaint();
    }
}

//------------------------------------------------------------------------------
void ParamControl::paint(juce::Graphics &g)
{
    if (!usable())
        return;

    switch (resolvedLook())
    {
    case Look::Toggle:
        paintToggle(g);
        break;
    case Look::Choice:
        paintChoice(g);
        break;
    default:
        paintKnob(g);
        break;
    }
}

void ParamControl::paintKnob(juce::Graphics &g)
{
    auto b = getLocalBounds();
    auto labelArea = showLabel ? b.removeFromBottom(15) : juce::Rectangle<int>();
    auto knobArea = b.toFloat();
    const bool enabled = !param->appears_deactivated();
    const float v = value01();
    const bool armedValid = canModulateWithArmed();

    draw::knob(g, knobArea, v, param->is_bipolar(), accent, enabled, hovering || dragging);

    if (armedValid)
    {
        const auto col = EditorContext::colourForSource(ctx.armedSource);
        const float depth =
            ctx.synth->getModDepth01(param->id, (modsources)ctx.armedSource, ctx.scene, 0);
        const bool active = ctx.synth->isActiveModulation(
            param->id, (modsources)ctx.armedSource, ctx.scene, 0);

        // a faint halo marks every knob the armed source can reach
        const float size = std::min(knobArea.getWidth(), knobArea.getHeight());
        g.setColour(col.withAlpha(active ? 0.55f : 0.34f));
        g.drawEllipse(juce::Rectangle<float>(size - 1.f, size - 1.f).withCentre(knobArea.getCentre()),
                      1.f);
        if (active)
        {
            // depth is a fraction of the knob's whole travel. A source that swings both ways
            // (an LFO, pitch bend) reaches as far below the knob position as above it.
            if (ctx.synth->isBipolarModulation((modsources)ctx.armedSource))
                draw::modRing(g, knobArea, v, -depth, col.withAlpha(0.4f));
            draw::modRing(g, knobArea, v, depth, col);
        }
    }
    else if (cachedModUsed)
    {
        // modulated by something: a small pip at the top right
        const float size = std::min(knobArea.getWidth(), knobArea.getHeight());
        auto c = knobArea.getCentre();
        g.setColour(theme::text);
        g.fillEllipse(juce::Rectangle<float>(4.f, 4.f).withCentre(
            {c.x + size * 0.5f - 4.f, c.y - size * 0.5f + 4.f}));
    }

    if (!showLabel || typeIn)
        return;

    juce::String text = draw::shortLabel(labelText()).toUpperCase();
    auto col = enabled ? theme::textDim : theme::textFaint;

    if (draggingMod || (armedValid && hovering))
    {
        char txt[TXT_SIZE];
        const float d = ctx.synth->getModDepth(param->id, (modsources)ctx.armedSource, ctx.scene, 0);
        param->get_display_of_modulation_depth(
            txt, d, ctx.synth->isBipolarModulation((modsources)ctx.armedSource), Parameter::Menu);
        text = txt;
        col = EditorContext::colourForSource(ctx.armedSource);
    }
    else if (hovering || dragging)
    {
        text = param->get_display();
        col = theme::text;
    }

    g.setColour(col);
    g.setFont((hovering || dragging) ? theme::font(11.f, true) : theme::labelFont(9.5f));
    g.drawFittedText(text, labelArea, juce::Justification::centred, 1, 0.6f);
}

void ParamControl::paintChoice(juce::Graphics &g)
{
    auto b = getLocalBounds();
    auto labelArea = showLabel ? b.removeFromBottom(15) : juce::Rectangle<int>();
    const bool enabled = !param->appears_deactivated();

    auto box = b.toFloat();
    if (showLabel)
        box = box.withSizeKeepingCentre(box.getWidth(), std::min(box.getHeight(), 26.f));
    box = box.reduced(1.f, 0.5f);

    g.setColour(hovering ? theme::panelHi.brighter(0.08f) : theme::well);
    g.fillRoundedRectangle(box, 5.f);
    g.setColour(hovering ? accent.withAlpha(0.7f) : theme::outline);
    g.drawRoundedRectangle(box, 5.f, 1.f);

    auto textArea = box.toNearestInt().reduced(7, 0);
    auto chev = textArea.removeFromRight(9).toFloat();
    juce::Path p;
    p.startNewSubPath(chev.getX(), chev.getCentreY() - 2.f);
    p.lineTo(chev.getCentreX(), chev.getCentreY() + 2.f);
    p.lineTo(chev.getRight(), chev.getCentreY() - 2.f);
    g.setColour(theme::textFaint);
    g.strokePath(p, juce::PathStrokeType(1.4f));

    g.setColour(enabled ? accent.interpolatedWith(theme::text, 0.55f) : theme::textFaint);
    g.setFont(theme::font(12.5f, true));
    g.drawFittedText(juce::String(param->get_display()).replace("Compensated", "Comp."), textArea,
                     juce::Justification::centredLeft, 1, 0.75f);

    if (showLabel)
        draw::captionBelow(g, labelArea, labelText(),
                           enabled ? theme::textDim : theme::textFaint);
}

void ParamControl::paintToggle(juce::Graphics &g)
{
    auto b = getLocalBounds().toFloat().reduced(1.f, 1.f);
    const bool on = param->val.b != inverted;
    const bool enabled = !param->appears_deactivated();

    g.setColour(on ? accent.withAlpha(0.16f) : (hovering ? theme::panelHi.brighter(0.06f) : theme::well));
    g.fillRoundedRectangle(b, 5.f);
    g.setColour(on ? accent.withAlpha(0.75f) : theme::outline);
    g.drawRoundedRectangle(b, 5.f, 1.f);

    g.setColour(on ? (enabled ? accent : theme::textDim) : theme::textFaint);
    g.setFont(theme::labelFont(10.f));
    g.drawFittedText(labelText().toUpperCase(), b.toNearestInt().reduced(4, 0),
                     juce::Justification::centred, 1, 0.7f);
}

//------------------------------------------------------------------------------
void ParamControl::mouseEnter(const juce::MouseEvent &)
{
    hovering = true;
    if (usable() && ctx.setHint)
    {
        juce::String h = juce::String(param->get_full_name()) + ":  " + param->get_display();
        if (canModulateWithArmed())
            h = "Drag to set how much " + ctx.nameForSource(ctx.armedSource) + " moves " +
                param->get_full_name();
        ctx.setHint(h);
    }
    repaint();
}

void ParamControl::mouseExit(const juce::MouseEvent &)
{
    hovering = false;
    if (ctx.setHint)
        ctx.setHint({});
    repaint();
}

void ParamControl::mouseDown(const juce::MouseEvent &e)
{
    if (!usable())
        return;

    if (e.mods.isPopupMenu())
    {
        showContextMenu();
        return;
    }

    const auto l = resolvedLook();
    if (l == Look::Toggle)
    {
        setIntValue(param->val.b ? 0 : 1);
        return;
    }
    if (l == Look::Choice)
    {
        showChoiceMenu();
        return;
    }

    dragging = true;
    lastDragPos = e.position;
    dragAccum = 0.f;
    draggingMod = canModulateWithArmed();
    if (draggingMod)
    {
        dragStartValue =
            ctx.synth->getModDepth01(param->id, (modsources)ctx.armedSource, ctx.scene, 0);
    }
    else
    {
        dragStartValue = value01();
        beginGesture();
    }
    repaint();
}

void ParamControl::mouseDrag(const juce::MouseEvent &e)
{
    if (!dragging || !usable())
        return;

    // vertical and horizontal travel both count; up and right increase
    const float d = (lastDragPos.y - e.position.y) + (e.position.x - lastDragPos.x);
    lastDragPos = e.position;
    const float speed = e.mods.isShiftDown() ? 0.0006f : 0.005f;
    dragAccum += d * speed;

    if (draggingMod)
    {
        const float range = 1.f;
        const float nv = juce::jlimit(-range, range, dragStartValue + dragAccum * 2.f);
        ctx.synth->setModDepth01(param->id, (modsources)ctx.armedSource, ctx.scene, 0, nv);
        ctx.markDirty();
        repaint();
        return;
    }

    if (param->valtype == vt_int)
    {
        const int span = param->val_max.i - param->val_min.i;
        const int start = Parameter::intUnscaledFromFloat(dragStartValue, param->val_max.i,
                                                          param->val_min.i);
        const int target = start + (int)std::round(dragAccum * (float)std::max(span, 1) * 1.5f);
        if (target != param->val.i)
            setIntValue(target);
    }
    else
    {
        setValue01(dragStartValue + dragAccum);
    }

    if (ctx.setHint)
        ctx.setHint(juce::String(param->get_full_name()) + ":  " + param->get_display());
}

void ParamControl::mouseUp(const juce::MouseEvent &)
{
    if (dragging)
    {
        dragging = false;
        if (draggingMod)
        {
            draggingMod = false;
            if (ctx.structureChanged)
                ctx.structureChanged();
        }
        else
        {
            endGesture();
        }
        repaint();
    }
}

void ParamControl::mouseDoubleClick(const juce::MouseEvent &e)
{
    if (!usable() || e.mods.isPopupMenu() || resolvedLook() != Look::Knob)
        return;

    if (canModulateWithArmed())
    {
        ctx.synth->clearModulation(param->id, (modsources)ctx.armedSource, ctx.scene, 0, false);
        ctx.markDirty();
        if (ctx.structureChanged)
            ctx.structureChanged();
        return;
    }
    setValue01(param->get_default_value_f01());
}

void ParamControl::mouseWheelMove(const juce::MouseEvent &e, const juce::MouseWheelDetails &w)
{
    if (!usable() || dragging)
        return;

    const float dy = (w.isReversed ? -w.deltaY : w.deltaY);
    if (dy == 0.f)
        return;

    if (param->valtype != vt_float)
    {
        wheelAccum += dy;
        if (std::abs(wheelAccum) < 0.12f)
            return;
        const int dir = wheelAccum > 0 ? 1 : -1;
        wheelAccum = 0.f;
        if (param->valtype == vt_bool)
        {
            setIntValue(dir > 0 ? 1 : 0);
            return;
        }
        int next = param->val.i + dir;
        while (choiceFilter && next >= param->val_min.i && next <= param->val_max.i &&
               !choiceFilter(next))
            next += dir;
        if (next >= param->val_min.i && next <= param->val_max.i)
            setIntValue(next);
        return;
    }

    if (canModulateWithArmed())
        return; // depth is set by dragging only, so a scroll cannot change it by accident

    setValue01(value01() + dy * (e.mods.isShiftDown() ? 0.01f : 0.08f));
    if (ctx.setHint)
        ctx.setHint(juce::String(param->get_full_name()) + ":  " + param->get_display());
}

//------------------------------------------------------------------------------
void ParamControl::showChoiceMenu()
{
    juce::PopupMenu menu;
    const int lo = param->val_min.i, hi = param->val_max.i;
    const int cur = param->val.i;
    auto nameOf = [this, lo, hi](int i) {
        return juce::String(param->get_display(true, Parameter::intScaledToFloat(i, hi, lo)));
    };
    auto allowed = [this](int i) { return !choiceFilter || choiceFilter(i); };

    auto *remap = dynamic_cast<ParameterDiscreteIndexRemapper *>(param->user_data);
    if (remap && remap->hasGroupNames() && remap->supportsTotalIndexOrdering())
    {
        // grouped: filters by response, waveshapers by family
        std::vector<std::pair<juce::String, juce::PopupMenu>> groups;
        for (int i : remap->totalIndexOrdering())
        {
            if (i < lo || i > hi || !allowed(i))
                continue;
            juce::String grp = remap->groupNameAtStreamedIndex(i);
            if (grp.isEmpty())
            {
                menu.addItem(i - lo + 1, nameOf(i), true, i == cur);
                continue;
            }
            auto it = std::find_if(groups.begin(), groups.end(),
                                   [&](auto &e) { return e.first == grp; });
            if (it == groups.end())
            {
                groups.emplace_back(grp, juce::PopupMenu());
                it = groups.end() - 1;
            }
            it->second.addItem(i - lo + 1, nameOf(i), true, i == cur);
        }
        if (!groups.empty())
            menu.addSeparator();
        for (auto &[name, sub] : groups)
        {
            bool has = false;
            for (int i : remap->totalIndexOrdering())
                if (i == cur && juce::String(remap->groupNameAtStreamedIndex(i)) == name)
                    has = true;
            menu.addSubMenu(name, sub, true, nullptr, has);
        }
    }
    else
    {
        int count = 0;
        for (int i = lo; i <= hi; ++i)
        {
            if (!allowed(i))
                continue;
            if (count > 0 && count % 16 == 0)
                menu.addColumnBreak();
            menu.addItem(i - lo + 1, nameOf(i), true, i == cur);
            ++count;
        }
    }

    juce::Component::SafePointer<ParamControl> safe(this);
    auto *opened = param; // the control may be pointed elsewhere while the menu is up
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withParentComponent(
                           menuParentFor(*this)),
                       [safe, lo, opened](int r) {
                           if (r > 0 && safe && safe->param == opened)
                               safe->setIntValue(r - 1 + lo);
                       });
}

void ParamControl::showContextMenu()
{
    juce::PopupMenu menu;
    menu.addSectionHeader(juce::String(param->get_full_name()) + "   " + param->get_display());

    enum
    {
        idTypeIn = 1,
        idReset,
        idTempo,
        idExtend,
        idAbsolute,
        idActive,
        idClearAll,
        idClearBase = 100
    };

    if (param->valtype == vt_float && param->can_setvalue_from_string())
        menu.addItem(idTypeIn, "Enter value...");
    menu.addItem(idReset, "Reset to default");

    if (param->can_temposync() || param->can_extend_range() || param->can_be_absolute() ||
        param->can_deactivate())
        menu.addSeparator();
    if (param->can_temposync())
        menu.addItem(idTempo, "Sync to tempo", true, param->temposync);
    if (param->can_extend_range())
        menu.addItem(idExtend, "Extended range", true, param->extend_range);
    if (param->can_be_absolute())
        menu.addItem(idAbsolute, "Absolute (Hz)", true, param->absolute);
    if (param->can_deactivate())
        menu.addItem(idActive, "Enabled", true, !param->deactivated);

    std::vector<int> sources;
    for (int ms = 1; ms < n_modsources; ++ms)
    {
        if (ctx.synth->isAnyActiveModulation(param->id, (modsources)ms, ctx.scene))
            sources.push_back(ms);
    }
    if (!sources.empty())
    {
        menu.addSeparator();
        menu.addSectionHeader("Modulation");
        for (int ms : sources)
        {
            char txt[TXT_SIZE];
            const float d = ctx.synth->getModDepth(param->id, (modsources)ms, ctx.scene, 0);
            param->get_display_of_modulation_depth(
                txt, d, ctx.synth->isBipolarModulation((modsources)ms), Parameter::Menu);
            menu.addItem(idClearBase + ms,
                         "Remove " + ctx.nameForSource(ms) + "   (" + juce::String(txt) + ")");
        }
        if (sources.size() > 1)
            menu.addItem(idClearAll, "Remove all modulation");
    }

    juce::Component::SafePointer<ParamControl> safe(this);
    menu.showMenuAsync(
        juce::PopupMenu::Options().withTargetComponent(this).withParentComponent(
            menuParentFor(*this)),
        [safe, sources, opened = param](int r) {
            if (!safe || r <= 0 || !safe->usable() || safe->param != opened)
                return;
            auto &self = *safe;
            auto *p = self.param;
            auto &ctx = self.ctx;
            bool structural = true;

            switch (r)
            {
            case idTypeIn:
                self.showTypeIn();
                structural = false;
                break;
            case idReset:
                self.setValue01(p->get_default_value_f01());
                break;
            case idTempo:
                p->temposync = !p->temposync;
                break;
            case idExtend:
                p->set_extend_range(!p->extend_range);
                break;
            case idAbsolute:
                p->absolute = !p->absolute;
                break;
            case idActive:
                p->deactivated = !p->deactivated;
                break;
            case idClearAll:
                for (int ms : sources)
                    ctx.synth->clearModulation(p->id, (modsources)ms, ctx.scene, 0, false);
                break;
            default:
                if (r >= idClearBase)
                    ctx.synth->clearModulation(p->id, (modsources)(r - idClearBase), ctx.scene, 0,
                                               false);
                break;
            }

            if (structural)
            {
                ctx.markDirty();
                if (ctx.structureChanged)
                    ctx.structureChanged();
                self.repaint();
            }
        });
}

void ParamControl::showTypeIn()
{
    typeIn = std::make_unique<juce::TextEditor>();
    auto b = getLocalBounds();
    auto area = b.removeFromBottom(20).expanded(8, 0);
    area = area.withWidth(std::max(area.getWidth(), 64)).withCentre({b.getCentreX(), area.getCentreY()});
    addAndMakeVisible(*typeIn);
    typeIn->setBounds(area.getIntersection(getLocalBounds().expanded(0, 0)));
    typeIn->setFont(theme::font(12.f));
    typeIn->setJustification(juce::Justification::centred);
    typeIn->setText(param->get_display(), false);
    typeIn->selectAll();
    typeIn->grabKeyboardFocus();

    juce::Component::SafePointer<ParamControl> safe(this);
    auto *box = typeIn.get();
    auto close = [safe, box]() {
        juce::MessageManager::callAsync([safe, box]() {
            // only if this is still the box that asked; a newer one may have replaced it
            if (safe && safe->typeIn.get() == box)
            {
                safe->typeIn.reset();
                safe->repaint();
            }
        });
    };

    typeIn->onReturnKey = [safe, close]() {
        if (safe && safe->typeIn && safe->usable())
        {
            pdata onto;
            std::string err;
            if (safe->param->set_value_from_string_onto(safe->typeIn->getText().toStdString(), onto,
                                                        err))
            {
                safe->setValue01(safe->param->value_to_normalized(onto.f));
            }
            else if (safe->ctx.setHint)
            {
                safe->ctx.setHint(err.empty() ? "That value could not be understood" : err);
            }
        }
        close();
    };
    typeIn->onEscapeKey = close;
    typeIn->onFocusLost = close;
}

//==============================================================================
ValueKnob::ValueKnob(juce::Colour acc, const juce::String &l) : label(l), accent(acc) {}

void ValueKnob::refresh(bool force)
{
    const float v = getValue ? getValue() : 0.f;
    if (force || v != cached || label != cachedLabel)
    {
        cached = v;
        cachedLabel = label;
        repaint();
    }
}

void ValueKnob::paint(juce::Graphics &g)
{
    auto b = getLocalBounds();
    auto labelArea = b.removeFromBottom(15);
    const float v = getValue ? getValue() : 0.f;
    draw::knob(g, b.toFloat(), v, false, accent, true, hovering || dragging);

    juce::String text = label.toUpperCase();
    if (hovering || dragging)
        text = format ? format(v) : juce::String(v * 100.f, 0) + " %";

    g.setColour((hovering || dragging) ? theme::text : theme::textDim);
    g.setFont((hovering || dragging) ? theme::font(11.f, true) : theme::labelFont(9.5f));
    g.drawFittedText(text, labelArea, juce::Justification::centred, 1, 0.6f);
}

void ValueKnob::mouseDown(const juce::MouseEvent &e)
{
    if (e.mods.isPopupMenu())
    {
        juce::PopupMenu menu;
        menu.addSectionHeader(label);
        menu.addItem(1, "Reset to default");
        if (extendMenu)
            extendMenu(menu);
        juce::Component::SafePointer<ValueKnob> safe(this);
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withParentComponent(
                               menuParentFor(*this)),
                           [safe](int r) {
                               if (safe && r == 1 && safe->setValue)
                               {
                                   if (safe->gesture)
                                       safe->gesture(true);
                                   safe->setValue(safe->defaultValue);
                                   if (safe->gesture)
                                       safe->gesture(false);
                                   safe->repaint();
                               }
                           });
        return;
    }
    dragging = true;
    lastDragPos = e.position;
    if (gesture)
        gesture(true);
    repaint();
}

void ValueKnob::mouseDrag(const juce::MouseEvent &e)
{
    if (!dragging || !setValue || !getValue)
        return;
    const float d = (lastDragPos.y - e.position.y) + (e.position.x - lastDragPos.x);
    lastDragPos = e.position;
    setValue(juce::jlimit(0.f, 1.f, getValue() + d * (e.mods.isShiftDown() ? 0.0006f : 0.005f)));
    repaint();
}

void ValueKnob::mouseUp(const juce::MouseEvent &)
{
    if (dragging)
    {
        dragging = false;
        if (gesture)
            gesture(false);
        repaint();
    }
}

void ValueKnob::mouseDoubleClick(const juce::MouseEvent &e)
{
    if (e.mods.isPopupMenu() || !setValue)
        return;
    if (gesture)
        gesture(true);
    setValue(defaultValue);
    if (gesture)
        gesture(false);
    repaint();
}

void ValueKnob::mouseWheelMove(const juce::MouseEvent &e, const juce::MouseWheelDetails &w)
{
    if (dragging || !setValue || !getValue)
        return;
    const float dy = (w.isReversed ? -w.deltaY : w.deltaY);
    if (dy == 0.f)
        return;
    if (gesture)
        gesture(true);
    setValue(juce::jlimit(0.f, 1.f, getValue() + dy * (e.mods.isShiftDown() ? 0.01f : 0.08f)));
    if (gesture)
        gesture(false);
    repaint();
}

//==============================================================================
FlatButton::FlatButton(const juce::String &t, juce::Colour acc) : text(t), accent(acc) {}

void FlatButton::paint(juce::Graphics &g)
{
    auto b = getLocalBounds().toFloat().reduced(0.5f);
    const float radius = std::min(6.f, b.getHeight() * 0.5f);

    // a quiet heart shows its state by filling in, not with a lit box around it
    const bool boxed = on && !(quiet && glyph == Glyph::Heart);
    if (boxed)
    {
        g.setColour(accent.withAlpha(down ? 0.34f : 0.2f));
        g.fillRoundedRectangle(b, radius);
        g.setColour(accent.withAlpha(0.85f));
        g.drawRoundedRectangle(b, radius, 1.f);
    }
    else if (!quiet || hovering)
    {
        g.setColour(down ? theme::track : (hovering ? theme::panelHi.brighter(0.08f) : theme::panelHi));
        g.fillRoundedRectangle(b, radius);
        if (!quiet)
        {
            g.setColour(theme::outline);
            g.drawRoundedRectangle(b, radius, 1.f);
        }
    }

    auto col = on ? accent.interpolatedWith(theme::text, 0.35f)
                  : (hovering ? theme::text : theme::textDim);
    if (!isEnabled())
        col = theme::textFaint;
    g.setColour(col);

    auto content = getLocalBounds().reduced(just == juce::Justification::centred ? 3 : 9, 0);
    content.removeFromLeft(leftInset);

    if (glyph != Glyph::None)
    {
        auto gb = (text.isEmpty() ? content.toFloat()
                                  : content.removeFromRight(14).toFloat())
                      .withSizeKeepingCentre(10.f, 10.f);
        juce::Path p;
        if (glyph == Glyph::Heart)
        {
            auto hb = gb.expanded(2.5f, 2.f);
            g.setColour(on ? accent : col);
            if (on)
                g.fillPath(draw::heart(hb));
            else
                g.strokePath(draw::heart(hb), juce::PathStrokeType(1.5f));
            g.setColour(col);
        }
        switch (glyph)
        {
        case Glyph::Prev:
            p.startNewSubPath(gb.getRight() - 2.f, gb.getY());
            p.lineTo(gb.getX() + 2.f, gb.getCentreY());
            p.lineTo(gb.getRight() - 2.f, gb.getBottom());
            break;
        case Glyph::Next:
            p.startNewSubPath(gb.getX() + 2.f, gb.getY());
            p.lineTo(gb.getRight() - 2.f, gb.getCentreY());
            p.lineTo(gb.getX() + 2.f, gb.getBottom());
            break;
        case Glyph::Menu:
            for (int i = 0; i < 3; ++i)
            {
                p.startNewSubPath(gb.getX(), gb.getY() + 1.f + (float)i * 4.f);
                p.lineTo(gb.getRight(), gb.getY() + 1.f + (float)i * 4.f);
            }
            break;
        case Glyph::Close:
            p.startNewSubPath(gb.getTopLeft());
            p.lineTo(gb.getBottomRight());
            p.startNewSubPath(gb.getTopRight());
            p.lineTo(gb.getBottomLeft());
            break;
        case Glyph::Dropdown:
            p.startNewSubPath(gb.getX() + 1.f, gb.getCentreY() - 2.f);
            p.lineTo(gb.getCentreX(), gb.getCentreY() + 2.5f);
            p.lineTo(gb.getRight() - 1.f, gb.getCentreY() - 2.f);
            break;
        case Glyph::Library: // books on a shelf, the last one leaning
            for (int i = 0; i < 3; ++i)
            {
                p.startNewSubPath(gb.getX() + (float)i * 2.6f, gb.getY());
                p.lineTo(gb.getX() + (float)i * 2.6f, gb.getBottom());
            }
            p.startNewSubPath(gb.getX() + 7.2f, gb.getY() + 0.6f);
            p.lineTo(gb.getRight(), gb.getBottom());
            break;
        case Glyph::Save:
            p.startNewSubPath(gb.getCentreX(), gb.getY());
            p.lineTo(gb.getCentreX(), gb.getBottom() - 3.f);
            p.startNewSubPath(gb.getX() + 1.5f, gb.getCentreY() - 0.5f);
            p.lineTo(gb.getCentreX(), gb.getBottom() - 3.f);
            p.lineTo(gb.getRight() - 1.5f, gb.getCentreY() - 0.5f);
            p.startNewSubPath(gb.getX(), gb.getBottom());
            p.lineTo(gb.getRight(), gb.getBottom());
            break;
        default:
            break;
        }
        g.strokePath(p, juce::PathStrokeType(1.6f, juce::PathStrokeType::curved,
                                             juce::PathStrokeType::rounded));
    }

    if (text.isNotEmpty())
    {
        g.setFont(theme::font(fontSize, true));
        g.drawFittedText(text, content, just, 1, 0.7f);
    }

    if (marker && !on)
    {
        g.setColour(accent);
        g.fillEllipse(juce::Rectangle<float>(4.f, 4.f).withCentre({b.getRight() - 5.f, b.getY() + 5.f}));
    }
}

void FlatButton::mouseDown(const juce::MouseEvent &e)
{
    if (e.mods.isPopupMenu())
    {
        if (onRightClick)
            onRightClick();
        return;
    }
    down = true;
    repaint();
}

void FlatButton::mouseUp(const juce::MouseEvent &e)
{
    if (!down)
        return;
    down = false;
    repaint();
    if (getLocalBounds().contains(e.getPosition()) && onClick)
        onClick();
}

//==============================================================================
Section::Section(const juce::String &t, juce::Colour acc) : title(t), accent(acc)
{
    setInterceptsMouseClicks(false, true);
}

juce::Rectangle<int> Section::getHeaderBounds() const
{
    const int titleW = (int)std::ceil(juce::GlyphArrangement::getStringWidth(
                           theme::labelFont(11.5f), title.toUpperCase())) +
                       30;
    return getLocalBounds().removeFromTop(headerHeight).withTrimmedLeft(titleW).reduced(6, 4);
}

juce::Rectangle<int> Section::getContentBounds() const
{
    return getLocalBounds().withTrimmedTop(headerHeight).reduced(8, 6).withTrimmedTop(-2);
}

void Section::paint(juce::Graphics &g)
{
    auto b = getLocalBounds().toFloat().reduced(0.5f);
    g.setColour(theme::panel);
    g.fillRoundedRectangle(b, 8.f);
    g.setColour(theme::outline.withAlpha(0.7f));
    g.drawRoundedRectangle(b, 8.f, 1.f);

    // the accent tab and title
    g.setColour(accent);
    g.fillRoundedRectangle(12.f, 12.f, 3.f, 9.f, 1.5f);
    g.setFont(theme::labelFont(11.5f));
    g.setColour(theme::text);
    g.drawText(title.toUpperCase(), 21, 0, getWidth() - 24, headerHeight + 3,
               juce::Justification::centredLeft);
}
} // namespace deasynth
