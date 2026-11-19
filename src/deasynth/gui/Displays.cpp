/*
 * Deasynth - an analog-voiced hybrid synthesizer built on the Surge XT engine.
 *
 * Deasynth is released under the GNU General Public Licence v3 or later (GPL-3.0-or-later).
 * The license is found in the "LICENSE" file in the root of this repository, or at
 * https://www.gnu.org/licenses/gpl-3.0.en.html
 */

#include "Displays.h"
#include "../DeasynthProcessor.h"

#include "LFOModulationSource.h"
#include "FormulaModulationHelper.h"
#include "sst/filters/HalfRateFilter.h"
#include "sst/filters/FilterPlotter.h"

namespace deasynth
{
namespace
{
struct Fingerprint
{
    uint64_t h{1469598103934665603ULL};
    void add(uint64_t v)
    {
        h ^= v;
        h *= 1099511628211ULL;
    }
    void addInt(int v) { add((uint64_t)(uint32_t)v); }
    void addFloat(float f)
    {
        uint32_t u;
        memcpy(&u, &f, sizeof(u));
        add(u);
    }
    void addParam(const Parameter &p)
    {
        addInt(p.val.i);
        addInt(p.ctrltype);
        addInt((p.temposync ? 1 : 0) | (p.extend_range ? 2 : 0) | (p.deactivated ? 4 : 0) |
               (p.absolute ? 8 : 0) | (p.deform_type << 4));
    }
};

void strokeTrace(juce::Graphics &g, const juce::Path &p, juce::Colour c, float thickness = 1.6f)
{
    g.setColour(c.withAlpha(0.18f));
    g.strokePath(p, juce::PathStrokeType(thickness + 3.f, juce::PathStrokeType::curved,
                                         juce::PathStrokeType::rounded));
    g.setColour(c);
    g.strokePath(p, juce::PathStrokeType(thickness, juce::PathStrokeType::curved,
                                         juce::PathStrokeType::rounded));
}

void fillUnder(juce::Graphics &g, const juce::Path &p, juce::Rectangle<float> area, float baseY,
               juce::Colour c)
{
    if (p.isEmpty())
        return;
    juce::Path fill(p);
    fill.lineTo(p.getBounds().getRight(), baseY);
    fill.lineTo(p.getBounds().getX(), baseY);
    fill.closeSubPath();
    g.setGradientFill(juce::ColourGradient(c.withAlpha(0.28f), 0.f, area.getY(), c.withAlpha(0.02f),
                                           0.f, area.getBottom(), false));
    g.fillPath(fill);
}

void centreNote(juce::Graphics &g, juce::Rectangle<int> r, const juce::String &t)
{
    g.setColour(theme::textFaint);
    g.setFont(theme::labelFont(10.5f));
    g.drawFittedText(t.toUpperCase(), r, juce::Justification::centred, 2);
}
} // namespace

//==============================================================================
OscDisplay::OscDisplay(EditorContext &c, int i) : ctx(c), index(i)
{
    tp.assign(n_scene_params, pdata{});
}

void OscDisplay::refresh(bool force)
{
    auto &o = osc();
    Fingerprint f;
    f.addInt(ctx.scene);
    f.addParam(o.type);
    for (auto &p : o.p)
        f.addParam(p);
    f.addInt(o.wt.current_id);
    f.addInt(o.wt.n_tables);
    f.addInt(getWidth());
    f.addInt(getHeight());
    f.add(std::hash<std::string>()(o.wavetable_display_name));
    f.addInt(ctx.sceneData().mute_o1.val.b + 2 * ctx.sceneData().mute_o2.val.b +
             4 * ctx.sceneData().mute_o3.val.b);

    if (force || f.h != fingerprint)
    {
        fingerprint = f.h;
        recompute();
        repaint();
    }
}

void OscDisplay::recompute()
{
    haveWave = false;
    wave.clear();
    caption.clear();

    auto &o = osc();
    auto *storage = &ctx.synth->storage;
    const int type = o.type.val.i;

    if (uses_wavetabledata(type))
        caption = o.wavetable_display_name;

    if (getWidth() < 8 || storage->dsamplerate < 1000.0)
        return;

    std::fill(tp.begin(), tp.end(), pdata{});
    tp[(size_t)o.pitch.param_id_in_scene].f = 0.f;
    for (auto &p : o.p)
        tp[(size_t)p.param_id_in_scene].i = p.val.i;

    std::lock_guard<std::mutex> lock(storage->waveTableDataMutex);

    auto *os = spawn_osc(type, storage, &o, tp.data(), tp.data(), oscbuffer);
    if (!os)
        return;

    if (os->allow_display())
    {
        // roughly two cycles of a low note, whatever the sample rate
        const float pitch = (90.15f - 48.f) + 12.f * (float)std::log2(storage->dsamplerate / 44100.0);
        os->init(pitch, true, true);

        sst::filters::HalfRate::HalfRateFilter hr(6, true);
        hr.reset();

        constexpr int blocks = 30;
        constexpr int skipBlocks = 2; // let the half-band settle
        const float w = (float)getWidth() - 12.f, h = (float)getHeight();
        const float cy = h * 0.5f, amp = h * 0.36f;
        const int total = (blocks - skipBlocks) * BLOCK_SIZE;

        float tmp alignas(16)[2][BLOCK_SIZE_OS];
        int n = 0;
        for (int b = 0; b < blocks; ++b)
        {
            os->process_block(pitch);
            memcpy(tmp[0], os->output, sizeof(tmp[0]));
            memcpy(tmp[1], os->output, sizeof(tmp[1]));
            hr.process_block_D2(tmp[0], tmp[1], BLOCK_SIZE_OS);
            if (b < skipBlocks)
                continue;
            for (int i = 0; i < BLOCK_SIZE; i += 2)
            {
                const float v = juce::jlimit(-1.25f, 1.25f, 0.5f * (tmp[0][i] + tmp[0][i + 1]));
                const float x = 6.f + w * (float)n / (float)(total - 2);
                const float y = cy - v * amp;
                if (n == 0)
                    wave.startNewSubPath(x, y);
                else
                    wave.lineTo(x, y);
                n += 2;
            }
        }
        haveWave = true;
    }

    os->~Oscillator();
}

void OscDisplay::paint(juce::Graphics &g)
{
    auto b = getLocalBounds().toFloat();
    draw::displayWell(g, b);

    const bool muted = (index == 0   ? ctx.sceneData().mute_o1
                        : index == 1 ? ctx.sceneData().mute_o2
                                     : ctx.sceneData().mute_o3)
                           .val.b;
    const auto col = muted ? theme::textFaint : theme::osc;

    g.setColour(theme::outline.withAlpha(0.6f));
    g.drawHorizontalLine(getHeight() / 2, 6.f, b.getRight() - 6.f);

    if (haveWave)
    {
        g.saveState();
        g.reduceClipRegion(getLocalBounds().reduced(2));
        fillUnder(g, wave, b, b.getCentreY(), col);
        strokeTrace(g, wave, col);
        g.restoreState();
    }
    else
    {
        centreNote(g, getLocalBounds().reduced(8), osc_type_names[osc().type.val.i]);
    }

    if (caption.isNotEmpty())
    {
        auto strip = getLocalBounds().removeFromBottom(17).reduced(3, 2);
        g.setColour(theme::well.withAlpha(0.85f));
        g.fillRoundedRectangle(strip.toFloat(), 3.f);
        g.setColour(hovering ? theme::text : theme::textDim);
        g.setFont(theme::font(11.f, true));
        g.drawFittedText(caption, strip.reduced(5, 0), juce::Justification::centredLeft, 1, 0.8f);
        g.setFont(theme::font(10.f));
        g.setColour(theme::textFaint);
        g.drawText("WAVETABLE", strip.reduced(5, 0), juce::Justification::centredRight);
    }
}

void OscDisplay::mouseEnter(const juce::MouseEvent &)
{
    hovering = true;
    if (uses_wavetabledata(osc().type.val.i) && ctx.setHint)
        ctx.setHint("Click to choose a wavetable");
    repaint();
}

void OscDisplay::mouseExit(const juce::MouseEvent &)
{
    hovering = false;
    if (ctx.setHint)
        ctx.setHint({});
    repaint();
}

void OscDisplay::mouseDown(const juce::MouseEvent &)
{
    if (uses_wavetabledata(osc().type.val.i))
        showWavetableMenu();
}

void OscDisplay::showWavetableMenu()
{
    auto &storage = ctx.synth->storage;
    juce::PopupMenu menu;
    const int current = osc().wt.current_id;

    if (storage.wt_list.empty())
    {
        menu.addItem(-1, "No wavetables installed", false);
        menu.addItem(-1, "Install the Deasynth factory content to add them", false);
    }

    int section = 0;
    for (int ci = 0; ci < (int)storage.wtCategoryOrdering.size(); ++ci)
    {
        const int c = storage.wtCategoryOrdering[(size_t)ci];
        const auto &cat = storage.wt_category[(size_t)c];

        juce::PopupMenu sub;
        bool hasCurrent = false;
        int count = 0;
        for (int wi : storage.wtOrdering)
        {
            const auto &wt = storage.wt_list[(size_t)wi];
            if (wt.category != c)
                continue;
            if (count > 0 && count % 28 == 0)
                sub.addColumnBreak();
            sub.addItem(wi + 1, wt.name, true, wi == current);
            hasCurrent = hasCurrent || wi == current;
            ++count;
        }
        if (count == 0)
            continue;

        const int thisSection =
            ci >= storage.firstUserWTCategory ? 2 : (ci >= storage.firstThirdPartyWTCategory ? 1 : 0);
        if (section != thisSection || menu.getNumItems() == 0)
        {
            static const char *names[] = {"Factory", "Third party", "User"};
            menu.addSectionHeader(names[thisSection]);
            section = thisSection;
        }
        menu.addSubMenu(juce::String(cat.name).replaceCharacter('\\', '/'), sub, true, nullptr,
                        hasCurrent);
    }

    juce::Component::SafePointer<OscDisplay> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withParentComponent(
                           menuParentFor(*this)),
                       [safe](int r) {
                           if (safe && r > 0)
                           {
                               safe->osc().wt.queue_id = r - 1;
                               safe->ctx.markDirty();
                           }
                       });
}

//==============================================================================
FilterDisplay::FilterDisplay(EditorContext &c, int u)
    : ctx(c), unit(u), plotter(std::make_unique<sst::filters::FilterPlotter>(13))
{
}

FilterDisplay::~FilterDisplay() = default;

void FilterDisplay::refresh(bool force)
{
    auto &fu = ctx.sceneData().filterunit[unit];
    Fingerprint f;
    f.addInt(ctx.scene);
    f.addParam(fu.type);
    f.addParam(fu.subtype);
    f.addParam(fu.cutoff);
    f.addParam(fu.resonance);
    if (unit == 1)
    {
        f.addInt(ctx.sceneData().f2_cutoff_is_offset.val.b);
        f.addInt(ctx.sceneData().f2_link_resonance.val.b);
        f.addParam(ctx.sceneData().filterunit[0].cutoff);
        f.addParam(ctx.sceneData().filterunit[0].resonance);
    }
    f.addInt(getWidth());
    f.addInt(getHeight());

    if (force || f.h != fingerprint)
    {
        fingerprint = f.h;
        recompute();
        repaint();
    }
}

void FilterDisplay::recompute()
{
    curve.clear();
    auto &scene = ctx.sceneData();
    auto &fu = scene.filterunit[unit];
    const int type = fu.type.val.i;
    off = (type == sst::filters::FilterType::fut_none);
    if (off || getWidth() < 8)
        return;

    float cutoff = fu.cutoff.val.f;
    float reso = fu.resonance.val.f;
    if (unit == 1)
    {
        if (scene.f2_cutoff_is_offset.val.b)
            cutoff += scene.filterunit[0].cutoff.val.f;
        if (scene.f2_link_resonance.val.b)
            reso = scene.filterunit[0].resonance.val.f;
    }

    sst::filters::FilterPlotParameters params;
    params.inputAmplitude = 0.1f; // quiet, so the nonlinear models show their small-signal shape
    params.freqSmoothOctaves = 1.f / 9.f;

    std::pair<std::vector<float>, std::vector<float>> data;
    try
    {
        data = plotter->plotFilterMagnitudeResponse((sst::filters::FilterType)type,
                                                    (sst::filters::FilterSubType)fu.subtype.val.i,
                                                    cutoff, reso, params);
    }
    catch (...)
    {
        return;
    }

    const auto &freqs = data.first;
    const auto &mags = data.second;
    const float w = (float)getWidth(), h = (float)getHeight();
    const float fLo = std::log(20.f), fHi = std::log(20000.f);
    const float dbTop = 18.f, dbBottom = -42.f;

    bool started = false;
    float lastX = -10.f;
    for (size_t i = 1; i < freqs.size() && i < mags.size(); ++i)
    {
        if (freqs[i] < 20.f || freqs[i] > 20000.f)
            continue;
        const float x = w * (std::log(freqs[i]) - fLo) / (fHi - fLo);
        if (x - lastX < 1.f)
            continue;
        lastX = x;
        float db = mags[i];
        if (!std::isfinite(db))
            db = dbBottom;
        const float y = h * (dbTop - juce::jlimit(dbBottom - 6.f, dbTop, db)) / (dbTop - dbBottom);
        if (!started)
        {
            curve.startNewSubPath(0.f, y);
            started = true;
        }
        curve.lineTo(x, y);
    }
}

void FilterDisplay::mouseEnter(const juce::MouseEvent &)
{
    if (ctx.setHint)
        ctx.setHint("Drag: left and right for cutoff, up and down for resonance");
}

void FilterDisplay::mouseExit(const juce::MouseEvent &)
{
    if (ctx.setHint)
        ctx.setHint({});
}

void FilterDisplay::mouseDown(const juce::MouseEvent &e)
{
    auto &fu = ctx.sceneData().filterunit[unit];
    if (e.mods.isPopupMenu() || fu.type.val.i == sst::filters::FilterType::fut_none)
        return;
    dragging = true;
    lastDrag = e.position;
    ctx.beginEdit(&fu.cutoff);
    ctx.beginEdit(&fu.resonance);
}

void FilterDisplay::mouseDrag(const juce::MouseEvent &e)
{
    if (!dragging)
        return;
    auto &fu = ctx.sceneData().filterunit[unit];
    const float fine = e.mods.isShiftDown() ? 0.2f : 1.f;
    const float dx = (e.position.x - lastDrag.x) / (float)std::max(1, getWidth());
    const float dy = (lastDrag.y - e.position.y) / (float)std::max(1, getHeight());
    lastDrag = e.position;
    // the picture spans 20 Hz to 20 kHz, about ten octaves of the cutoff control's range
    const float span = fu.cutoff.val_max.f - fu.cutoff.val_min.f;
    if (dx != 0.f)
        ctx.setValue01(&fu.cutoff, fu.cutoff.get_value_f01() + dx * fine * 120.f / std::max(1.f, span));
    if (dy != 0.f)
        ctx.setValue01(&fu.resonance, fu.resonance.get_value_f01() + dy * fine);
    if (ctx.setHint)
        ctx.setHint(juce::String("Cutoff ") + fu.cutoff.get_display() + ":  Resonance " +
                    fu.resonance.get_display());
}

void FilterDisplay::mouseUp(const juce::MouseEvent &)
{
    if (!dragging)
        return;
    dragging = false;
    auto &fu = ctx.sceneData().filterunit[unit];
    ctx.endEdit(&fu.cutoff);
    ctx.endEdit(&fu.resonance);
}

void FilterDisplay::paint(juce::Graphics &g)
{
    auto b = getLocalBounds().toFloat();
    draw::displayWell(g, b);

    const float h = b.getHeight();
    const float zeroY = h * 18.f / 60.f;
    g.setColour(theme::outline.withAlpha(0.6f));
    g.drawHorizontalLine((int)zeroY, 4.f, b.getRight() - 4.f);
    // decade lines at 100 Hz, 1 kHz, 10 kHz
    for (float f : {100.f, 1000.f, 10000.f})
    {
        const float x = b.getWidth() * (std::log(f) - std::log(20.f)) / (std::log(20000.f) - std::log(20.f));
        g.setColour(theme::outline.withAlpha(0.35f));
        g.drawVerticalLine((int)x, 4.f, h - 4.f);
    }

    if (off)
    {
        centreNote(g, getLocalBounds(), "Filter off");
        return;
    }

    g.saveState();
    g.reduceClipRegion(getLocalBounds().reduced(2));
    fillUnder(g, curve, b, b.getBottom() + 4.f, theme::filter);
    strokeTrace(g, curve, theme::filter);
    g.restoreState();
}

//==============================================================================
EnvDisplay::EnvDisplay(EditorContext &c, int w, juce::Colour a) : ctx(c), which(w), accent(a) {}

void EnvDisplay::refresh(bool force)
{
    auto &eg = ctx.sceneData().adsr[which];
    Fingerprint f;
    f.addInt(ctx.scene);
    for (auto *p : {&eg.a, &eg.d, &eg.s, &eg.r, &eg.a_s, &eg.d_s, &eg.r_s, &eg.mode})
        f.addParam(*p);
    if (force || f.h != fingerprint)
    {
        fingerprint = f.h;
        repaint();
    }
}

EnvDisplay::Geometry EnvDisplay::geometry() const
{
    auto &eg = ctx.sceneData().adsr[which];
    Geometry gm;
    gm.area = getLocalBounds().toFloat().reduced(8.f, 9.f);

    // Stage widths follow the time controls on their own (logarithmic) scale, so every stage
    // stays readable from a few milliseconds to half a minute.
    auto norm = [](const Parameter &p) {
        return juce::jlimit(0.f, 1.f, (p.val.f - p.val_min.f) / (p.val_max.f - p.val_min.f));
    };
    const float W = gm.area.getWidth();
    gm.xDecay = gm.area.getX() + (stageMin + stageSpan * norm(eg.a)) * W;
    gm.xSus = gm.xDecay + (stageMin + stageSpan * norm(eg.d)) * W;
    gm.xRel = gm.xSus + sustainWidth * W;
    gm.xEnd = gm.xRel + (stageMin + stageSpan * norm(eg.r)) * W;
    gm.sustainY = gm.area.getBottom() - juce::jlimit(0.f, 1.f, eg.s.val.f) * gm.area.getHeight();
    return gm;
}

int EnvDisplay::handleAt(juce::Point<float> p) const
{
    const auto gm = geometry();
    const juce::Point<float> handles[3] = {{gm.xDecay, gm.area.getY()},
                                           {gm.xSus, gm.sustainY},
                                           {gm.xEnd, gm.area.getBottom()}};
    int best = -1;
    float bestDist = 14.f;
    for (int i = 0; i < 3; ++i)
    {
        const float d = handles[i].getDistanceFrom(p);
        if (d < bestDist)
        {
            bestDist = d;
            best = i;
        }
    }
    return best;
}

void EnvDisplay::mouseMove(const juce::MouseEvent &e)
{
    const int h = handleAt(e.position);
    if (h != hot)
    {
        hot = h;
        static const char *hints[3] = {"Drag to set the attack time",
                                       "Drag to set decay time and sustain level",
                                       "Drag to set the release time"};
        if (ctx.setHint)
            ctx.setHint(h >= 0 ? hints[h] : "");
        setMouseCursor(h >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void EnvDisplay::mouseExit(const juce::MouseEvent &)
{
    if (grabbed < 0 && hot >= 0)
    {
        hot = -1;
        if (ctx.setHint)
            ctx.setHint({});
        repaint();
    }
}

void EnvDisplay::mouseDown(const juce::MouseEvent &e)
{
    grabbed = e.mods.isPopupMenu() ? -1 : handleAt(e.position);
    if (grabbed < 0)
        return;
    lastDrag = e.position;
    auto &eg = ctx.sceneData().adsr[which];
    if (grabbed == 0)
        ctx.beginEdit(&eg.a);
    else if (grabbed == 1)
    {
        ctx.beginEdit(&eg.d);
        ctx.beginEdit(&eg.s);
    }
    else
        ctx.beginEdit(&eg.r);
}

void EnvDisplay::mouseDrag(const juce::MouseEvent &e)
{
    if (grabbed < 0)
        return;
    auto &eg = ctx.sceneData().adsr[which];
    const auto gm = geometry();
    const float fine = e.mods.isShiftDown() ? 0.2f : 1.f;
    const float dx = (e.position.x - lastDrag.x) / (stageSpan * gm.area.getWidth()) * fine;
    const float dy = (lastDrag.y - e.position.y) / gm.area.getHeight() * fine;
    lastDrag = e.position;

    Parameter *timeParam = grabbed == 0 ? &eg.a : (grabbed == 1 ? &eg.d : &eg.r);
    if (dx != 0.f)
        ctx.setValue01(timeParam, timeParam->get_value_f01() + dx);
    if (grabbed == 1 && dy != 0.f)
        ctx.setValue01(&eg.s, eg.s.get_value_f01() + dy);

    if (ctx.setHint)
    {
        juce::String h = juce::String(timeParam->get_name()) + " " + timeParam->get_display();
        if (grabbed == 1)
            h += juce::String(":  Sustain ") + eg.s.get_display();
        ctx.setHint(h);
    }
}

void EnvDisplay::mouseUp(const juce::MouseEvent &e)
{
    if (grabbed < 0)
        return;
    auto &eg = ctx.sceneData().adsr[which];
    if (grabbed == 0)
        ctx.endEdit(&eg.a);
    else if (grabbed == 1)
    {
        ctx.endEdit(&eg.d);
        ctx.endEdit(&eg.s);
    }
    else
        ctx.endEdit(&eg.r);
    grabbed = -1;
    mouseMove(e);
}

void EnvDisplay::paint(juce::Graphics &g)
{
    auto b = getLocalBounds().toFloat();
    draw::displayWell(g, b);

    auto &eg = ctx.sceneData().adsr[which];
    const auto gm = geometry();
    const auto area = gm.area;
    const bool analog = eg.mode.val.i == emt_analog;
    const float sus = juce::jlimit(0.f, 1.f, eg.s.val.f);
    auto Y = [&](float level) { return area.getBottom() - level * area.getHeight(); };

    // shape exponents: analog stages are RC curves; digital ones follow the shape switches
    const float attackCurve = analog ? 0.45f : (eg.a_s.val.i == 0 ? 0.5f : (eg.a_s.val.i == 2 ? 2.f : 1.f));
    const float decayCurve = analog ? 2.6f : (eg.d_s.val.i == 0 ? 1.f : (eg.d_s.val.i == 1 ? 2.f : 3.f));
    const float releaseCurve = analog ? 2.6f : (eg.r_s.val.i == 0 ? 1.f : (eg.r_s.val.i == 1 ? 2.f : 3.f));

    juce::Path p;
    p.startNewSubPath(area.getX(), Y(0.f));
    constexpr int steps = 24;
    for (int i = 1; i <= steps; ++i)
    {
        const float t = (float)i / steps;
        p.lineTo(area.getX() + t * (gm.xDecay - area.getX()), Y(std::pow(t, attackCurve)));
    }
    for (int i = 1; i <= steps; ++i)
    {
        const float t = (float)i / steps;
        p.lineTo(gm.xDecay + t * (gm.xSus - gm.xDecay), Y(sus + (1.f - sus) * std::pow(1.f - t, decayCurve)));
    }
    p.lineTo(gm.xRel, Y(sus));
    for (int i = 1; i <= steps; ++i)
    {
        const float t = (float)i / steps;
        p.lineTo(gm.xRel + t * (gm.xEnd - gm.xRel), Y(sus * std::pow(1.f - t, releaseCurve)));
    }

    g.setColour(theme::outline.withAlpha(0.5f));
    for (float gx : {gm.xDecay, gm.xSus, gm.xRel})
        g.drawVerticalLine((int)gx, area.getY(), area.getBottom());

    fillUnder(g, p, b, area.getBottom(), accent);
    strokeTrace(g, p, accent);

    const juce::Point<float> handles[3] = {{gm.xDecay, Y(1.f)}, {gm.xSus, Y(sus)}, {gm.xEnd, Y(0.f)}};
    for (int i = 0; i < 3; ++i)
    {
        const bool lit = i == hot || i == grabbed;
        const float d = lit ? 9.f : 6.f;
        g.setColour(theme::well);
        g.fillEllipse(juce::Rectangle<float>(d + 3.f, d + 3.f).withCentre(handles[i]));
        g.setColour(lit ? theme::text : accent);
        g.fillEllipse(juce::Rectangle<float>(d, d).withCentre(handles[i]));
    }
    // the end of the sustain plateau is where the key is released; it is not a handle
    g.setColour(accent.withAlpha(0.6f));
    g.fillEllipse(juce::Rectangle<float>(4.f, 4.f).withCentre({gm.xRel, Y(sus)}));
}

//==============================================================================
LfoDisplay::LfoDisplay(EditorContext &c) : ctx(c) { tp.assign(n_scene_params, pdata{}); }

void LfoDisplay::refresh(bool force)
{
    auto &l = ctx.sceneData().lfo[lfo];
    Fingerprint f;
    f.addInt(ctx.scene);
    f.addInt(lfo);
    for (Parameter *p = &l.rate; p <= &l.release; ++p)
        f.addParam(*p);
    f.addFloat(ctx.synth->storage.temposyncratio);
    f.addInt(getWidth());
    f.addInt(getHeight());
    if (l.shape.val.i == lt_stepseq)
    {
        auto &ss = ctx.patch().stepsequences[ctx.scene][lfo];
        for (float s : ss.steps)
            f.addFloat(s);
        f.addInt(ss.loop_start);
        f.addInt(ss.loop_end);
        f.addFloat(ss.shuffle);
    }

    if (force || f.h != fingerprint)
    {
        fingerprint = f.h;
        recompute();
        repaint();
    }
}

void LfoDisplay::recompute()
{
    wave.clear();
    auto &storage = ctx.synth->storage;
    auto &patch = ctx.patch();
    auto &l = ctx.sceneData().lfo[lfo];
    const float w = (float)getWidth() - 12.f, h = (float)getHeight();
    if (w < 8.f || storage.samplerate < 1000.f)
        return;

    std::fill(tp.begin(), tp.end(), pdata{});
    for (Parameter *p = &l.rate; p <= &l.release; ++p)
        tp[(size_t)p->param_id_in_scene].i = p->val.i;

    float rateHz = std::pow(2.f, l.rate.val.f);
    if (l.rate.temposync)
        rateHz *= storage.temposyncratio;
    rateHz = juce::jlimit(0.01f, 2000.f, rateHz);

    const int shape = l.shape.val.i;
    if (shape == lt_envelope)
    {
        float t = 0.f;
        for (auto *p : {&l.delay, &l.attack, &l.hold, &l.decay})
            if (p->val.f > p->val_min.f)
                t += std::pow(2.f, p->val.f);
        windowSeconds = juce::jlimit(0.3f, 8.f, t * 1.5f);
    }
    else if (shape == lt_stepseq)
    {
        windowSeconds = juce::jlimit(0.25f, 8.f, 16.f / rateHz);
    }
    else if (shape == lt_mseg || shape == lt_formula)
    {
        windowSeconds = 4.f;
    }
    else
    {
        windowSeconds = juce::jlimit(0.05f, 8.f, 3.f / rateHz);
    }

    auto mod = std::make_unique<LFOModulationSource>();
    mod->assign(&storage, &l, tp.data(), nullptr, &patch.stepsequences[ctx.scene][lfo],
                &patch.msegs[ctx.scene][lfo], &patch.formulamods[ctx.scene][lfo], true);
    mod->setIsVoice(lfo < n_lfos_voice);
    if (mod->isVoice)
        mod->formulastate.velocity = 100;
    Surge::Formula::setupEvaluatorStateFrom(mod->formulastate, patch, ctx.scene);
    mod->attack();

    const int blocks =
        juce::jlimit(32, 20000, (int)(windowSeconds * storage.samplerate / (float)BLOCK_SIZE));
    const int columns = juce::jmax(16, (int)w);
    const float cy = h * 0.5f, amp = h * 0.40f;
    const bool uni = l.unipolar.val.b;

    int lastCol = -1;
    float acc = 0.f;
    int accN = 0;
    bool started = false;
    for (int i = 0; i < blocks; ++i)
    {
        mod->process_block();
        float v = mod->get_output(0);
        if (!std::isfinite(v))
            v = 0.f;
        const int col = (int)((int64_t)i * columns / blocks);
        if (col != lastCol && accN > 0)
        {
            const float val = juce::jlimit(-1.15f, 1.15f, acc / (float)accN);
            const float x = 6.f + w * (float)lastCol / (float)(columns - 1);
            const float y = uni ? (h - 8.f - val * (h - 16.f)) : (cy - val * amp);
            if (!started)
                wave.startNewSubPath(x, y);
            else
                wave.lineTo(x, y);
            started = true;
            acc = 0.f;
            accN = 0;
        }
        lastCol = col;
        acc += v;
        ++accN;
    }

    mod->completedModulation();
}

void LfoDisplay::paint(juce::Graphics &g)
{
    auto b = getLocalBounds().toFloat();
    draw::displayWell(g, b);

    const auto col = lfo < n_lfos_voice ? theme::lfo : theme::sceneLfo;
    const bool uni = ctx.sceneData().lfo[lfo].unipolar.val.b;
    const float baseY = uni ? b.getBottom() - 8.f : b.getCentreY();

    g.setColour(theme::outline.withAlpha(0.6f));
    g.drawHorizontalLine((int)baseY, 6.f, b.getRight() - 6.f);

    g.saveState();
    g.reduceClipRegion(getLocalBounds().reduced(2));
    fillUnder(g, wave, b, baseY, col);
    strokeTrace(g, wave, col);
    g.restoreState();

    g.setColour(theme::textFaint);
    g.setFont(theme::font(10.5f));
    juce::String span = windowSeconds >= 1.f ? juce::String(windowSeconds, 1) + " s"
                                             : juce::String((int)(windowSeconds * 1000.f)) + " ms";
    g.drawText(span, getLocalBounds().reduced(8, 5), juce::Justification::bottomRight);
}

//==============================================================================
StepEditor::StepEditor(EditorContext &c) : ctx(c) {}

void StepEditor::refresh(bool force)
{
    Fingerprint f;
    f.addInt(ctx.scene);
    f.addInt(lfo);
    auto &ss = steps();
    for (float s : ss.steps)
        f.addFloat(s);
    f.addInt(ss.loop_start);
    f.addInt(ss.loop_end);
    f.addInt(ctx.sceneData().lfo[lfo].unipolar.val.b);
    if (force || f.h != fingerprint)
    {
        fingerprint = f.h;
        repaint();
    }
}

void StepEditor::paint(juce::Graphics &g)
{
    auto b = getLocalBounds().toFloat();
    draw::displayWell(g, b);
    auto area = b.reduced(6.f, 6.f);
    auto &ss = steps();
    const bool uni = ctx.sceneData().lfo[lfo].unipolar.val.b;
    const auto col = lfo < n_lfos_voice ? theme::lfo : theme::sceneLfo;
    const float sw = area.getWidth() / n_stepseqsteps;
    const float zeroY = uni ? area.getBottom() : area.getCentreY();

    for (int i = 0; i < n_stepseqsteps; ++i)
    {
        auto cell = juce::Rectangle<float>(area.getX() + (float)i * sw, area.getY(), sw, area.getHeight())
                        .reduced(1.5f, 0.f);
        const bool inLoop = i >= ss.loop_start && i <= ss.loop_end;
        g.setColour(theme::panel.withAlpha(inLoop ? 1.f : 0.45f));
        g.fillRoundedRectangle(cell, 2.f);

        float v = juce::jlimit(-1.f, 1.f, ss.steps[i]);
        if (uni)
            v = juce::jlimit(0.f, 1.f, v);
        const float y = uni ? area.getBottom() - v * area.getHeight()
                            : area.getCentreY() - v * area.getHeight() * 0.5f;
        auto bar = juce::Rectangle<float>(cell.getX(), std::min(y, zeroY), cell.getWidth(),
                                          std::max(2.f, std::abs(y - zeroY)));
        g.setColour(col.withAlpha(inLoop ? 0.85f : 0.3f));
        g.fillRoundedRectangle(bar, 2.f);
    }

    if (!uni)
    {
        g.setColour(theme::outline);
        g.drawHorizontalLine((int)zeroY, area.getX(), area.getRight());
    }
}

void StepEditor::edit(const juce::MouseEvent &e)
{
    auto area = getLocalBounds().toFloat().reduced(6.f, 6.f);
    const int i = juce::jlimit(0, n_stepseqsteps - 1,
                               (int)((e.position.x - area.getX()) / area.getWidth() * n_stepseqsteps));
    const bool uni = ctx.sceneData().lfo[lfo].unipolar.val.b;
    const float t = juce::jlimit(0.f, 1.f, (e.position.y - area.getY()) / area.getHeight());
    float v = uni ? (1.f - t) : (1.f - 2.f * t);
    if (!e.mods.isShiftDown())
    {
        // snap to twelfths, so steps land on semitones when the depth is an octave
        v = std::round(v * 12.f) / 12.f;
    }
    steps().steps[i] = v;
    ctx.markDirty();
    repaint();
}

void StepEditor::mouseDoubleClick(const juce::MouseEvent &e)
{
    auto area = getLocalBounds().toFloat().reduced(6.f, 6.f);
    const int i = juce::jlimit(0, n_stepseqsteps - 1,
                               (int)((e.position.x - area.getX()) / area.getWidth() * n_stepseqsteps));
    steps().steps[i] = 0.f;
    ctx.markDirty();
    repaint();
}

//==============================================================================
ScopeDisplay::ScopeDisplay(EditorContext &c) : ctx(c) {}

void ScopeDisplay::refresh(bool force)
{
    auto &proc = ctx.proc;
    constexpr int N = DeasynthProcessor::scopeSize;
    constexpr int shown = 720;

    float buf[N];
    const int wpos = proc.scopeWritePos.load(std::memory_order_relaxed);
    float maxAbs = 0.f;
    for (int i = 0; i < N; ++i)
    {
        buf[i] = proc.scopeBuffer[(size_t)((wpos + i) % N)].load(std::memory_order_relaxed);
        maxAbs = std::max(maxAbs, std::abs(buf[i]));
    }

    for (int c = 0; c < 2; ++c)
    {
        const float p = proc.outputPeak[c].exchange(0.f, std::memory_order_relaxed);
        peak[c] = std::max(p, peak[c] * 0.86f);
        hold[c] = std::max(p, hold[c] * 0.985f);
        if (peak[c] < 1e-5f)
            peak[c] = 0.f;
        if (hold[c] < 1e-5f)
            hold[c] = 0.f;
    }

    const bool silent = maxAbs < 1e-5f && peak[0] == 0.f && peak[1] == 0.f && hold[0] == 0.f;
    if (silent && wasSilent && !force)
        return;
    wasSilent = silent;

    // trigger on a rising zero crossing so that steady tones stand still
    int start = N - shown - 1;
    for (int i = N - shown - 1; i > 1; --i)
    {
        if (buf[i - 1] <= 0.f && buf[i] > 0.f)
        {
            start = i;
            break;
        }
    }

    const float w = (float)getWidth() - 26.f, h = (float)getHeight();
    const float scale = 1.f / std::max(1.f, maxAbs);
    trace.clear();
    for (int i = 0; i < shown; i += 2)
    {
        const float x = 4.f + w * (float)i / (float)(shown - 2);
        const float y = h * 0.5f - buf[start + i] * scale * h * 0.42f;
        if (i == 0)
            trace.startNewSubPath(x, y);
        else
            trace.lineTo(x, y);
    }
    repaint();
}

void ScopeDisplay::paint(juce::Graphics &g)
{
    auto b = getLocalBounds().toFloat();
    draw::displayWell(g, b);

    g.setColour(theme::outline.withAlpha(0.6f));
    g.drawHorizontalLine(getHeight() / 2, 4.f, b.getRight() - 24.f);

    g.saveState();
    g.reduceClipRegion(getLocalBounds().reduced(2));
    strokeTrace(g, trace, theme::voice, 1.3f);
    g.restoreState();

    // meters: -48 dB to +6 dB
    auto meters = getLocalBounds().removeFromRight(20).reduced(3, 5);
    for (int c = 0; c < 2; ++c)
    {
        auto m = meters.removeFromLeft(6).toFloat();
        meters.removeFromLeft(2);
        g.setColour(theme::panel);
        g.fillRoundedRectangle(m, 2.f);

        auto frac = [](float lin) {
            if (lin <= 0.f)
                return 0.f;
            return juce::jlimit(0.f, 1.f, (juce::Decibels::gainToDecibels(lin) + 48.f) / 54.f);
        };
        const float f = frac(peak[c]);
        const bool hot = peak[c] > 1.f;
        if (f > 0.f)
        {
            g.setColour(hot ? theme::danger : theme::ampEnv);
            g.fillRoundedRectangle(m.withTrimmedTop(m.getHeight() * (1.f - f)), 2.f);
        }
        const float hf = frac(hold[c]);
        if (hf > 0.f)
        {
            g.setColour(hold[c] > 1.f ? theme::danger : theme::text);
            g.fillRect(m.getX(), m.getY() + m.getHeight() * (1.f - hf), m.getWidth(), 1.5f);
        }
    }
}
} // namespace deasynth
