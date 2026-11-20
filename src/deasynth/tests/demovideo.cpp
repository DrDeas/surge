/*
 * Deasynth - an analog-voiced hybrid synthesizer built on the Surge XT engine.
 *
 * Deasynth is released under the GNU General Public Licence v3 or later (GPL-3.0-or-later).
 * The license is found in the "LICENSE" file in the root of this repository, or at
 * https://www.gnu.org/licenses/gpl-3.0.en.html
 */

/*
 * Renders the Deasynth demo film: 90 seconds of the real plugin being played and operated.
 *
 *   deasynth-selftest --render-demo <directory>
 *
 * Nothing in it is mocked up. The window is the actual editor; knobs are turned, switches
 * thrown and menus opened by sending the editor the same mouse events a person would; and
 * the soundtrack is the instrument's own output, rendered sample-accurately in step with
 * the picture. What this file adds is the script (what happens when), a camera that moves
 * around the window, a pointer, captions and the title cards.
 *
 * It writes numbered frames (1920 x 1080, 30 per second) and soundtrack.wav into the
 * directory; scripts/deasynth/render-demo.sh runs it and encodes the result with ffmpeg.
 * It needs a display (xvfb-run on Linux) and the factory library installed.
 */

#include "../DeasynthProcessor.h"
#include "../gui/DeasynthEditor.h"
#include "../gui/Overlays.h"

#include "juce_audio_formats/juce_audio_formats.h"

#include <iostream>

using namespace deasynth;

namespace
{
constexpr int fps = 30;
constexpr double seconds = 90.0;
constexpr int outW = 1920, outH = 1080;
constexpr double sampleRate = 48000.0;
constexpr int audioBlock = 64;
constexpr float snapScale = 2.f; // the window is captured at twice size so zooms stay sharp

float ease(float u)
{
    u = juce::jlimit(0.f, 1.f, u);
    return u * u * (3.f - 2.f * u);
}

struct Demo : juce::Timer
{
    using Pt = juce::Point<float>;
    using PtFn = std::function<Pt()>;

    //================================================================ set-up
    explicit Demo(const juce::File &dir) : outDir(dir)
    {
        outDir.createDirectory();
        proc.prepareToPlay(sampleRate, audioBlock);
        *proc.vintageParam = 0.15f;
        *proc.warmthParam = 0.f;
        proc.editorState.page = 0;
        proc.editorState.zoom = 1.f;

        editorBase.reset(proc.createEditor());
        ed = dynamic_cast<DeasynthEditor *>(editorBase.get());
        // Frames are painted from the editor directly, not grabbed from the screen, and hover
        // is delivered by updateHover() so that it is in step with the script frame by frame.
        // A real, focused window: popup menus close themselves if the app has no focus.
        ed->addToDesktop(juce::ComponentPeer::windowAppearsOnTaskbar);
        ed->setTopLeftPosition(0, 0);
        ed->setVisible(true);
        ed->toFront(true);
        ed->grabKeyboardFocus();

        const auto total = (size_t)(seconds * sampleRate) + 48000;
        audioL.reserve(total);
        audioR.reserve(total);

        writeScript();
        std::stable_sort(notes.begin(), notes.end(),
                         [](const NoteEvent &a, const NoteEvent &b) { return a.sample < b.sample; });
        std::stable_sort(actions.begin(), actions.end(),
                         [](const Action &a, const Action &b) { return a.t < b.t; });
        startTimer(55); // real time between frames, for menus and hover to settle
    }

    SurgeSynthesizer &synth() { return *proc.surge; }
    SurgePatch &patch() { return synth().storage.getPatch(); }
    SurgeSceneStorage &sc() { return patch().scene[0]; }

    //================================================================ finding things on screen
    template <typename T> T *find(std::function<bool(T &)> pred, juce::Component *from = nullptr)
    {
        if (!from)
            from = ed;
        for (auto *c : from->getChildren())
        {
            if (!c->isVisible())
                continue;
            if (auto *t = dynamic_cast<T *>(c))
                if (!pred || pred(*t))
                    return t;
            if (auto *r = find<T>(pred, c))
                return r;
        }
        return nullptr;
    }
    template <typename T> T *nth(int n)
    {
        int seen = 0;
        return find<T>([&](T &) { return seen++ == n; });
    }
    ParamControl *ctl(Parameter *p)
    {
        return find<ParamControl>([p](ParamControl &c) { return c.getParam() == p; });
    }
    ParamControl *fxCtl(int slot, const char *name)
    {
        for (auto &p : patch().fx[slot].p)
            if (p.ctrltype != ct_none && std::string(p.get_name()) == name)
                return ctl(&p);
        return nullptr;
    }
    ValueKnob *valueKnob(const juce::String &label)
    {
        return find<ValueKnob>([&](ValueKnob &k) { return k.label == label; });
    }
    FlatButton *button(const juce::String &text)
    {
        return find<FlatButton>([&](FlatButton &b) { return b.getText() == text; });
    }
    FlatButton *buttonWithTip(const juce::String &part)
    {
        return find<FlatButton>([&](FlatButton &b) { return b.getTooltip().containsIgnoreCase(part); });
    }
    /** Open popup menus, outermost first. They are children of the editor's design surface. */
    std::vector<juce::Component *> menus()
    {
        std::vector<juce::Component *> out;
        std::function<void(juce::Component *)> walk = [&](juce::Component *c) {
            for (auto *k : c->getChildren())
            {
                if (juce::String(typeid(*k).name()).contains("MenuWindow"))
                    out.push_back(k);
                else
                    walk(k);
            }
        };
        walk(ed);
        return out;
    }
    Pt centre(juce::Component *c, Pt fallback = {640.f, 400.f})
    {
        return c ? ed->getLocalPoint(c, c->getLocalBounds().toFloat().getCentre()) : fallback;
    }
    Pt at(juce::Component *c, float fx, float fy)
    {
        if (!c)
            return {640.f, 400.f};
        auto b = c->getLocalBounds().toFloat();
        return ed->getLocalPoint(c, Pt(b.getX() + fx * b.getWidth(), b.getY() + fy * b.getHeight()));
    }
    PtFn menuItem(int menuIndex, int itemIndex)
    {
        return [this, menuIndex, itemIndex]() -> Pt {
            auto m = menus();
            if (menuIndex >= (int)m.size())
                return pointer;
            auto *w = m[(size_t)menuIndex];
            // the items are the window's children that span a row; skip scroll helpers
            std::vector<juce::Component *> items;
            for (auto *c : w->getChildren())
                if (c->getHeight() >= 16 && c->getWidth() > 40)
                    items.push_back(c);
            if (items.empty())
                return centre(w);
            return centre(items[(size_t)juce::jlimit(0, (int)items.size() - 1, itemIndex)]);
        };
    }

    //================================================================ acting like a mouse
    juce::MouseEvent mouseEvent(juce::Component &c, Pt local, Pt downAt, bool buttonDown = true)
    {
        const auto when = juce::Time::getCurrentTime();
        return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), local,
                                buttonDown ? juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier)
                                           : juce::ModifierKeys(),
                                0.f, 0.f, 0.f, 0.f, 0.f, &c, &c, when, downAt, when, buttonDown ? 1 : 0, false);
    }
    /** What a window would do as the pointer moves: enter and leave controls, track menus. */
    void updateHover()
    {
        if (held)
            return;
        juce::Component *under = ed->getComponentAt(pointer.toInt());
        if (under != hovered.getComponent())
        {
            if (hovered)
            {
                const auto l = hovered->getLocalPoint(ed, pointer);
                hovered->mouseExit(mouseEvent(*hovered, l, l, false));
            }
            hovered = under;
            if (hovered)
            {
                const auto l = hovered->getLocalPoint(ed, pointer);
                hovered->mouseEnter(mouseEvent(*hovered, l, l, false));
            }
        }
        else if (hovered)
        {
            const auto l = hovered->getLocalPoint(ed, pointer);
            hovered->mouseMove(mouseEvent(*hovered, l, l, false));
        }
        // menus follow the pointer wherever it is, to highlight rows and open submenus
        for (auto *m : menus())
        {
            const auto l = m->getLocalPoint(ed, pointer);
            m->mouseMove(mouseEvent(*m, l, l, false));
        }
    }
    void click(juce::Component *c)
    {
        if (!c)
            return;
        const auto local = c->getLocalPoint(ed, pointer);
        c->mouseDown(mouseEvent(*c, local, local));
        c->mouseUp(mouseEvent(*c, local, local));
        clickFlashAt = now;
    }
    /** Press on a component and keep the button down; follow with dragTo and release. */
    void press(juce::Component *c)
    {
        held = c;
        if (!c)
            return;
        heldDown = c->getLocalPoint(ed, pointer);
        c->mouseDown(mouseEvent(*c, heldDown, heldDown));
        clickFlashAt = now;
    }
    void dragHeld()
    {
        if (held)
            held->mouseDrag(mouseEvent(*held, held->getLocalPoint(ed, pointer), heldDown));
    }
    void release()
    {
        if (held)
            held->mouseUp(mouseEvent(*held, held->getLocalPoint(ed, pointer), heldDown));
        held = nullptr;
    }

    //================================================================ the script's vocabulary
    struct Action
    {
        double t;
        std::function<void()> fn;
    };
    struct Tween
    {
        double t0, t1;
        std::function<void()> begin;
        std::function<void(float)> step; // eased 0...1
        std::function<void()> end;
        bool started{false}, finished{false};
    };
    struct CameraKey
    {
        double t;
        float cx, cy, zoom;
    };
    struct Caption
    {
        double t0, t1;
        juce::String title, sub;
    };
    struct NoteEvent
    {
        int64_t sample;
        juce::MidiMessage msg;
    };

    void when(double t, std::function<void()> fn) { actions.push_back({t, std::move(fn)}); }
    void camera(double t, float cx, float cy, float zoom) { cameraKeys.push_back({t, cx, cy, zoom}); }
    void caption(double t0, double t1, const juce::String &title, const juce::String &sub)
    {
        captions.push_back({t0, t1, title, sub});
    }

    /** Glide the pointer to wherever target() is when the move begins. */
    void moveTo(double t0, double t1, PtFn target)
    {
        auto from = std::make_shared<Pt>();
        auto to = std::make_shared<Pt>();
        tweens.push_back({t0, t1,
                          [this, from, to, target]() {
                              *from = pointer;
                              *to = target();
                          },
                          [this, from, to](float u) { pointer = *from + (*to - *from) * u; }, nullptr});
    }
    /** Hold the button down on a control and move the pointer along path(u), in pixels. */
    void drag(double t0, double t1, std::function<juce::Component *()> what, std::function<Pt(float)> path)
    {
        auto origin = std::make_shared<Pt>();
        tweens.push_back({t0, t1,
                          [this, origin, what]() {
                              *origin = pointer;
                              press(what());
                          },
                          [this, origin, path](float u) {
                              pointer = *origin + path(u);
                              dragHeld();
                          },
                          [this]() { release(); }});
    }
    void turn(double t0, double t1, std::function<juce::Component *()> what, float pixelsUp)
    {
        // approach, then turn: a knob responds to vertical travel
        moveTo(t0 - 0.7, t0 - 0.05, [this, what]() { return centre(what()); });
        drag(t0, t1, what, [pixelsUp](float u) { return Pt(0.f, -pixelsUp * u); });
    }
    void clickOn(double t, std::function<juce::Component *()> what)
    {
        moveTo(t - 0.7, t - 0.05, [this, what]() { return centre(what()); });
        when(t, [this, what]() { click(what()); });
    }
    void setChoice(Parameter &p, int v)
    {
        juce::PopupMenu::dismissAllActiveMenus();
        if (auto *c = ctl(&p))
            c->repaint();
        synth().setParameter01(synth().idForParameter(&p),
                               Parameter::intScaledToFloat(v, p.val_max.i, p.val_min.i), false, true);
        ed->context().structureChanged();
        clickFlashAt = now;
    }
    int patchNamed(const std::string &name, const std::string &categoryContains = {})
    {
        auto &st = synth().storage;
        for (int i = 0; i < (int)st.patch_list.size(); ++i)
        {
            const auto &p = st.patch_list[(size_t)i];
            if (p.name != name)
                continue;
            if (categoryContains.empty() ||
                st.patch_category[(size_t)p.category].name.find(categoryContains) != std::string::npos)
                return i;
        }
        std::cout << "  (patch not found: " << name << ")" << std::endl;
        return -1;
    }
    void loadPatch(double t, const std::string &name, const std::string &category = "Deasynth")
    {
        when(t, [this, name, category]() { ed->loadPatch(patchNamed(name, category)); });
    }

    // ---- music
    static constexpr double bpm = 108.0;
    static double beats(double b) { return b * 60.0 / bpm; }
    void note(double t, double dur, int key, int vel = 96)
    {
        notes.push_back({(int64_t)(t * sampleRate), juce::MidiMessage::noteOn(1, key, (juce::uint8)vel)});
        notes.push_back({(int64_t)((t + dur) * sampleRate), juce::MidiMessage::noteOff(1, key)});
    }
    void pattern(double t0, double t1, std::initializer_list<int> keys, double stepBeats, double gate, int vel = 96)
    {
        std::vector<int> k(keys);
        int i = 0;
        for (double t = t0; t < t1 - 1e-6; t += beats(stepBeats), ++i)
        {
            const int key = k[(size_t)i % k.size()];
            if (key > 0)
                note(t, std::min(beats(stepBeats) * gate, t1 - t), key, (i % 4 == 0) ? vel : vel - 14);
        }
    }
    void chord(double t, double dur, std::initializer_list<int> keys, int vel = 88)
    {
        double spread = 0.0;
        for (int k : keys)
        {
            note(t + spread, dur - spread, k, vel);
            spread += 0.012;
        }
    }

    //================================================================ the script
    void writeScript()
    {
        // ---------------------------------------------------------------- 0:00 title
        loadPatch(0.0, "Slow Pad");
        chord(0.3, 5.4, {38, 50, 57, 60, 64, 69}, 80);
        camera(0.0, 640, 400, 1.05f);
        camera(6.0, 640, 400, 1.2f);
        pointer = {900.f, 560.f};

        // ---------------------------------------------------------------- 0:06 oscillators
        caption(6.4, 17.6, "Three oscillators. Eleven engines.",
                "Analog shapes, wavetables, FM, strings and more, each with a live picture of its output.");
        when(6.0, [this]() { ed->initPatch(); });
        pattern(6.3, 17.9, {50, 57, 62, 65, 69, 65, 62, 57, 48, 55, 60, 64, 67, 64, 60, 55}, 0.5, 0.8, 100);
        camera(7.4, 470, 215, 1.85f);
        camera(14.6, 470, 215, 1.85f);
        camera(15.8, 860, 215, 1.85f);

        auto &o1 = sc().osc[0];
        auto &o2 = sc().osc[1];
        turn(8.0, 10.0, [&]() { return ctl(&o1.p[0]); }, -100.f); // saw to pulse
        clickOn(10.9, [&]() { return ctl(&o1.type); });           // open the engine menu
        moveTo(11.1, 11.5, menuItem(0, 1));
        moveTo(11.5, 12.0, menuItem(0, 2));
        when(12.3, [&]() { setChoice(o1.type, ot_wavetable); });
        turn(13.4, 15.2, [&]() { return ctl(&o1.p[0]); }, 150.f); // sweep the wavetable
        clickOn(16.2, [&]() { return ctl(&sc().mute_o2); });      // bring in oscillator 2
        moveTo(16.5, 17.2, [&]() { return centre(ctl(&o2.p[5])); });

        // ---------------------------------------------------------------- 0:18 filter
        caption(18.4, 29.6, "A transistor ladder you can grab.",
                "Drag the curve to sweep cutoff and resonance. Push Drive to overload it.");
        loadPatch(18.0, "Ladder Bass");
        pattern(18.3, 29.9, {38, 0, 38, 50, 0, 38, 41, 0, 36, 0, 36, 48, 0, 36, 43, 45}, 0.5, 0.75, 112);
        camera(19.0, 400, 560, 1.9f);
        camera(25.4, 400, 560, 1.9f);
        camera(26.4, 900, 420, 1.42f);

        moveTo(19.0, 19.8, [this]() { return at(nth<FilterDisplay>(0), 0.42f, 0.55f); });
        drag(20.0, 25.4, [this]() { return (juce::Component *)nth<FilterDisplay>(0); }, [](float u) {
            const float x = 62.f * std::sin(u * juce::MathConstants<float>::twoPi * 1.5f) + 36.f * u;
            const float y = -34.f * std::sin(u * juce::MathConstants<float>::pi);
            return Pt(x, y);
        });
        turn(26.6, 27.8, [this]() { return ctl(&sc().level_pfg); }, 46.f);
        turn(28.4, 29.6, [this]() { return ctl(&sc().feedback); }, 30.f);

        // ---------------------------------------------------------------- 0:30 envelopes
        caption(30.4, 39.6, "Envelopes you shape by hand.",
                "Pull the handles to set attack, decay, sustain and release.");
        loadPatch(30.0, "Glass Pluck");
        pattern(30.3, 39.9, {62, 69, 74, 77, 81, 77, 74, 69, 60, 67, 72, 76, 79, 76, 72, 67}, 0.5, 0.9, 104);
        camera(31.0, 1040, 560, 1.9f);
        camera(39.6, 1040, 560, 1.9f);

        auto envHandle = [this](int which, int handle) -> PtFn {
            return [this, which, handle]() -> Pt {
                auto *d = nth<EnvDisplay>(which == adsr_ampeg ? 0 : 1);
                if (!d)
                    return pointer;
                auto &eg = sc().adsr[which];
                auto area = d->getLocalBounds().toFloat().reduced(8.f, 9.f);
                auto n = [](const Parameter &p) {
                    return juce::jlimit(0.f, 1.f, (p.val.f - p.val_min.f) / (p.val_max.f - p.val_min.f));
                };
                const float W = area.getWidth();
                const float xDecay = area.getX() + (0.03f + 0.24f * n(eg.a)) * W;
                const float xSus = xDecay + (0.03f + 0.24f * n(eg.d)) * W;
                const float xEnd = xSus + 0.16f * W + (0.03f + 0.24f * n(eg.r)) * W;
                const float susY = area.getBottom() - juce::jlimit(0.f, 1.f, eg.s.val.f) * area.getHeight();
                Pt local = handle == 0 ? Pt(xDecay, area.getY())
                                       : (handle == 1 ? Pt(xSus, susY) : Pt(xEnd, area.getBottom()));
                return ed->getLocalPoint(d, local);
            };
        };
        moveTo(31.2, 32.0, envHandle(adsr_filteg, 1));
        drag(32.2, 35.0, [this]() { return (juce::Component *)nth<EnvDisplay>(1); },
             [](float u) { return Pt(40.f * u, -34.f * std::sin(u * juce::MathConstants<float>::pi * 0.75f)); });
        moveTo(35.4, 36.2, envHandle(adsr_ampeg, 2));
        drag(36.4, 39.0, [this]() { return (juce::Component *)nth<EnvDisplay>(0); },
             [](float u) { return Pt(46.f * u, 0.f); });

        // ---------------------------------------------------------------- 0:40 modulation
        caption(40.4, 46.2, "Modulate anything in two clicks.",
                "Arm a source, drag a knob. The ring shows how far it will move.");
        caption(46.8, 53.6, "Twelve LFOs. Draw your own.",
                "Six per voice and six per layer, with a 16-step sequencer you paint with the mouse.");
        loadPatch(40.0, "Vintage Poly");
        for (int bar = 0; bar < 7; ++bar)
        {
            static const int prog[4][4] = {{50, 57, 62, 65}, {46, 53, 58, 62}, {48, 55, 60, 64}, {45, 52, 57, 60}};
            const double t = 40.3 + beats(4.0) * bar;
            if (t < 53.6)
                chord(t, std::min(beats(4.0) * 0.96, 53.9 - t),
                      {prog[bar % 4][0] - 12, prog[bar % 4][0], prog[bar % 4][1], prog[bar % 4][2], prog[bar % 4][3]});
        }
        camera(40.8, 640, 380, 1.3f);
        camera(41.6, 420, 330, 1.55f);
        camera(45.8, 420, 330, 1.55f);
        camera(47.2, 420, 330, 1.6f);
        camera(53.6, 420, 330, 1.6f);

        clickOn(41.4, [this]() { return button("1"); }); // the LFO 1 chip
        moveTo(41.8, 42.5, [this]() { return centre(ctl(&sc().filterunit[0].cutoff)); });
        drag(42.7, 45.0, [this]() { return (juce::Component *)ctl(&sc().filterunit[0].cutoff); },
             [](float u) { return Pt(0.f, -30.f * u); });
        clickOn(45.9, [this]() { return button("1"); }); // disarm
        clickOn(46.8, [this]() { return button("MODULATION"); });
        clickOn(47.8, [this]() { return ctl(&sc().lfo[0].shape); });
        moveTo(48.0, 48.9, menuItem(0, 7));
        when(49.1, [this]() { setChoice(sc().lfo[0].shape, lt_stepseq); });
        moveTo(49.3, 49.9, [this]() { return at(nth<StepEditor>(0), 0.03f, 0.5f); });
        drag(50.1, 53.2, [this]() { return (juce::Component *)nth<StepEditor>(0); }, [this](float u) {
            auto *s = nth<StepEditor>(0);
            const float w = s ? (float)s->getWidth() * 0.94f : 600.f;
            const float h = s ? (float)s->getHeight() : 80.f;
            return Pt(w * u, -h * 0.4f * std::sin(u * juce::MathConstants<float>::twoPi * 2.f));
        });

        // ---------------------------------------------------------------- 0:54 effects
        caption(54.6, 63.6, "A full effects rack, built in.",
                "Sixteen slots across layer inserts, sends and the master bus.");
        for (int bar = 0; bar < 5; ++bar)
        {
            static const int prog[4][4] = {{50, 57, 62, 65}, {46, 53, 58, 62}, {48, 55, 60, 64}, {45, 52, 57, 60}};
            const double t = 54.2 + beats(4.0) * bar;
            if (t < 63.6)
                chord(t, std::min(beats(2.4), 63.9 - t),
                      {prog[bar % 4][0] - 12, prog[bar % 4][0], prog[bar % 4][1], prog[bar % 4][2], prog[bar % 4][3]});
        }
        camera(54.6, 640, 330, 1.42f);
        camera(63.6, 640, 330, 1.42f);
        clickOn(54.4, [this]() { return button("EFFECTS"); });
        clickOn(55.6, [this]() { return button("Empty"); }); // the first free slot
        clickOn(56.6, [this]() { return ctl(&patch().fx[fxslot_ains2].type); });
        moveTo(56.8, 57.8, menuItem(0, 11));
        when(58.0, [this]() { setChoice(patch().fx[fxslot_ains2].type, fxt_reverb2); });
        turn(59.4, 60.8, [this]() { return fxCtl(fxslot_ains2, "Mix"); }, 44.f);
        turn(61.6, 63.0, [this]() { return fxCtl(fxslot_ains2, "Decay Time"); }, 40.f);

        // ---------------------------------------------------------------- 1:04 patches
        caption(64.6, 75.6, "Thousands of patches. Keep the ones you love.",
                "Browse by category, search, step through with the arrow keys, and mark favorites.");
        for (double t = 64.4; t < 75.6; t += beats(4.0))
        {
            note(t, beats(0.9), 60, 100);
            note(t + beats(1.0), beats(0.9), 64, 92);
            note(t + beats(2.0), beats(0.9), 67, 96);
            note(t + beats(3.0), beats(0.8), 72, 104);
            note(t, beats(3.6), 48, 84);
        }
        camera(64.4, 640, 330, 1.42f);
        camera(65.2, 800, 300, 1.5f);
        camera(69.8, 800, 300, 1.5f);
        camera(70.8, 640, 420, 1.25f);
        camera(75.8, 640, 420, 1.25f);

        clickOn(64.2, [this]() { return button("SYNTH"); });
        clickOn(65.2, [this]() { return buttonWithTip("by category"); }); // the patch name
        // rows of the menu: 0 browser, 1 Favorites, then the factory categories from Basses
        moveTo(65.4, 65.9, menuItem(0, 2)); // Basses
        moveTo(66.3, 66.9, menuItem(1, 4));
        moveTo(66.9, 67.3, menuItem(0, 6)); // Keys
        moveTo(67.7, 68.3, menuItem(1, 3));
        when(68.6, [this]() {
            juce::PopupMenu::dismissAllActiveMenus();
            clickFlashAt = now;
            ed->loadPatch(patchNamed("Circus 1", "Keys"));
        });
        clickOn(69.6, [this]() { return buttonWithTip("favorites"); });      // the heart
        clickOn(70.6, [this]() { return buttonWithTip("patch browser"); }); // the bookshelf
        moveTo(70.8, 71.4, [this]() { return Pt(760.f, 300.f); });
        for (double t : {71.6, 72.7, 73.8, 74.9})
            when(t, [this]() {
                if (auto *b = find<PresetBrowser>(nullptr))
                    b->keyPressed(juce::KeyPress(juce::KeyPress::downKey));
            });
        when(75.8, [this]() { ed->showBrowser(false); });

        // ---------------------------------------------------------------- 1:16 character
        caption(76.4, 83.8, "Vintage and Warmth.",
                "Every voice drifts its own way, like old voice cards. Then drive the output stage.");
        loadPatch(76.0, "Vintage Poly");
        for (int bar = 0; bar < 4; ++bar)
        {
            static const int prog[4][4] = {{50, 57, 62, 65}, {46, 53, 58, 62}, {48, 55, 60, 64}, {50, 57, 62, 66}};
            const double t = 76.3 + beats(4.0) * bar;
            chord(t, bar == 3 ? 6.0 : beats(4.0) * 0.96,
                  {prog[bar % 4][0] - 12, prog[bar % 4][0], prog[bar % 4][1], prog[bar % 4][2], prog[bar % 4][3]});
        }
        camera(76.6, 640, 400, 1.25f);
        camera(77.6, 330, 700, 2.0f);
        camera(83.6, 330, 700, 2.0f);
        camera(85.2, 640, 400, 1.12f);
        camera(90.0, 640, 400, 1.2f);
        turn(78.0, 80.2, [this]() { return valueKnob("Vintage"); }, 110.f);
        turn(81.0, 83.2, [this]() { return valueKnob("Warmth"); }, 80.f);
        moveTo(83.6, 84.6, [this]() { return Pt(980.f, 620.f); });
    }

    //================================================================ one frame
    void timerCallback() override
    {
        if (busy)
            return;
        busy = true;

        now = (double)frame / fps;

        // 1. things that happen at this moment
        while (nextAction < actions.size() && actions[nextAction].t <= now + 1e-9)
            actions[nextAction++].fn();
        for (auto &tw : tweens)
        {
            if (tw.finished || now < tw.t0)
                continue;
            if (!tw.started)
            {
                tw.started = true;
                if (tw.begin)
                    tw.begin();
            }
            const float u = tw.t1 > tw.t0 ? (float)((now - tw.t0) / (tw.t1 - tw.t0)) : 1.f;
            if (tw.step)
                tw.step(ease(u));
            if (u >= 1.f)
            {
                tw.finished = true;
                if (tw.end)
                    tw.end();
            }
        }

        // the system pointer follows too (menus read it directly), then hover is brought up to date
        juce::Desktop::setMousePosition(ed->localPointToGlobal(pointer.toInt()));
        updateHover();

        // 2. exactly one frame of sound
        const int64_t until = (int64_t)((double)(frame + 1) / fps * sampleRate);
        juce::AudioBuffer<float> buf(2, audioBlock);
        while (samplePos < until)
        {
            const int n = (int)std::min<int64_t>(audioBlock, until - samplePos);
            juce::AudioBuffer<float> view(buf.getArrayOfWritePointers(), 2, n);
            view.clear();
            juce::MidiBuffer midi;
            while (nextNote < notes.size() && notes[nextNote].sample < samplePos + n)
            {
                midi.addEvent(notes[nextNote].msg, (int)std::max<int64_t>(0, notes[nextNote].sample - samplePos));
                ++nextNote;
            }
            proc.processBlock(view, midi);
            audioL.insert(audioL.end(), view.getReadPointer(0), view.getReadPointer(0) + n);
            audioR.insert(audioR.end(), view.getReadPointer(1), view.getReadPointer(1) + n);
            samplePos += n;
        }

        // 3. the picture (a preview run can skip to the part of interest)
        if (now >= previewFrom)
            writeFrame();

        if (getenv("DEASYNTH_DEMO_DEBUG") && frame % 6 == 0)
        {
            auto m = menus();
            std::cout << "  t=" << now << " menus=" << m.size();
            for (auto *w : m)
                std::cout << " [" << w->getBounds().toString() << " vis=" << w->isVisible()
                          << " kids=" << w->getNumChildComponents() << "]";
            std::cout << std::endl;
        }

        if (++frame >= (int)(std::min(seconds, previewTo) * fps))
        {
            stopTimer();
            finish();
            return;
        }
        if (frame % 150 == 0)
            std::cout << "  " << frame / fps << " s" << std::endl;
        busy = false;
    }

    void cameraAt(double t, float &cx, float &cy, float &zoom) const
    {
        const CameraKey *a = &cameraKeys.front(), *b = a;
        for (const auto &k : cameraKeys)
        {
            if (k.t <= t)
                a = &k;
            b = &k;
            if (k.t > t)
                break;
        }
        const float u = (b->t > a->t) ? ease((float)((t - a->t) / (b->t - a->t))) : 0.f;
        cx = a->cx + (b->cx - a->cx) * u;
        cy = a->cy + (b->cy - a->cy) * u;
        zoom = a->zoom + (b->zoom - a->zoom) * u;

        // Keep the frame on the window: never pan so far that empty backdrop shows at an
        // edge, unless the whole window fits, in which case it is simply centred.
        const float margin = 28.f / zoom;
        const float halfW = outW * 0.5f / zoom, above = 500.f / zoom, below = (outH - 500.f) / zoom;
        const float W = (float)DeasynthEditor::baseWidth, H = (float)DeasynthEditor::baseHeight;
        cx = (2.f * halfW >= W + 2.f * margin) ? W * 0.5f
                                               : juce::jlimit(halfW - margin, W - halfW + margin, cx);
        // the captions cover the bottom of the frame, so the window may run under them
        const float lowest = H - below + 330.f / zoom;
        cy = (above + below >= H + 2.f * margin) ? juce::jmin(cy, H * 0.5f + 40.f / zoom)
                                                 : juce::jlimit(above - margin, juce::jmax(above - margin, lowest), cy);
    }

    static float fade(double t, double t0, double t1, double edge = 0.45)
    {
        if (t < t0 || t > t1)
            return 0.f;
        return ease((float)std::min((t - t0) / edge, (t1 - t) / edge));
    }

    void writeFrame()
    {
        auto snap = ed->createComponentSnapshot(ed->getLocalBounds(), true, snapScale);

        juce::Image out(juce::Image::RGB, outW, outH, false);
        juce::Graphics g(out);
        g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);

        // backdrop
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xff15181d), 0.f, 0.f, juce::Colour(0xff07080a), 0.f,
                                               (float)outH, false));
        g.fillAll();
        g.setGradientFill(juce::ColourGradient(theme::osc.withAlpha(0.10f), outW * 0.5f, outH * 0.42f,
                                               theme::osc.withAlpha(0.f), outW * 0.5f, outH * 1.25f, true));
        g.fillAll();

        float cx, cy, zoom;
        cameraAt(now, cx, cy, zoom);
        const float ox = outW * 0.5f - cx * zoom, oy = 500.f - cy * zoom;
        auto toOut = [&](Pt p) { return Pt(ox + p.x * zoom, oy + p.y * zoom); };

        // the window, with a soft shadow and rounded corners
        juce::Rectangle<float> ui(ox, oy, DeasynthEditor::baseWidth * zoom, DeasynthEditor::baseHeight * zoom);
        for (int i = 5; i >= 1; --i)
        {
            g.setColour(juce::Colours::black.withAlpha(0.08f));
            g.fillRoundedRectangle(ui.expanded((float)i * 7.f).translated(0.f, 14.f), 16.f * zoom + (float)i * 7.f);
        }
        {
            juce::Graphics::ScopedSaveState s(g);
            juce::Path clip;
            clip.addRoundedRectangle(ui, 12.f * zoom);
            g.reduceClipRegion(clip);
            g.setOpacity(1.f); // images are drawn at the current colour's alpha
            g.drawImageTransformed(snap, juce::AffineTransform::scale(zoom / snapScale).translated(ox, oy));
        }
        g.setColour(juce::Colours::white.withAlpha(0.08f));
        g.drawRoundedRectangle(ui, 12.f * zoom, 1.5f);

        // the pointer, and a ring where it clicks
        {
            const auto p = toOut(pointer);
            const float since = (float)(now - clickFlashAt);
            if (since >= 0.f && since < 0.45f)
            {
                const float u = since / 0.45f;
                g.setColour(juce::Colours::white.withAlpha(0.55f * (1.f - u)));
                g.drawEllipse(juce::Rectangle<float>(24.f + 60.f * u, 24.f + 60.f * u).withCentre(p), 3.f);
            }
            const float k = 1.5f;
            juce::Path arrow;
            arrow.startNewSubPath(0.f, 0.f);
            arrow.lineTo(0.f, 17.f * k);
            arrow.lineTo(4.6f * k, 13.f * k);
            arrow.lineTo(8.f * k, 20.5f * k);
            arrow.lineTo(10.8f * k, 19.2f * k);
            arrow.lineTo(7.4f * k, 12.f * k);
            arrow.lineTo(13.f * k, 12.f * k);
            arrow.closeSubPath();
            arrow.applyTransform(juce::AffineTransform::translation(p.x, p.y));
            g.setColour(juce::Colours::black.withAlpha(0.35f));
            g.fillPath(arrow, juce::AffineTransform::translation(2.f, 3.f));
            g.setColour(juce::Colours::white);
            g.fillPath(arrow);
            g.setColour(juce::Colours::black.withAlpha(0.85f));
            g.strokePath(arrow, juce::PathStrokeType(1.6f));
        }

        // captions, on a scrim so they read over anything
        for (const auto &c : captions)
        {
            const float a = fade(now, c.t0, c.t1);
            if (a <= 0.f)
                continue;
            g.setGradientFill(juce::ColourGradient(juce::Colours::black.withAlpha(0.f), 0.f, outH - 330.f,
                                                   juce::Colours::black.withAlpha(0.86f * a), 0.f, outH - 60.f, false));
            g.fillRect(0, outH - 330, outW, 330);
            const float slide = (1.f - a) * 18.f;
            g.setColour(theme::osc.withAlpha(a));
            g.fillRoundedRectangle(96.f, outH - 176.f + slide, 6.f, 98.f, 3.f);
            g.setColour(theme::text.withAlpha(a));
            g.setFont(theme::font(54.f, true));
            g.drawText(c.title, 124, (int)(outH - 184.f + slide), outW - 240, 64, juce::Justification::centredLeft);
            g.setColour(theme::textDim.withAlpha(a));
            g.setFont(theme::font(28.f));
            g.drawText(c.sub, 126, (int)(outH - 118.f + slide), outW - 240, 40, juce::Justification::centredLeft);
        }

        // a quiet wordmark in the corner while the window is on screen
        {
            const float a = fade(now, 6.2, 84.0, 0.8) * 0.9f;
            if (a > 0.f)
            {
                g.setOpacity(a);
                // bottom right, level with the captions, where it never sits on a control
                theme::drawLogo(g, {outW - 286.f, outH - 124.f, 44.f, 48.f});
                g.setOpacity(1.f);
                g.setColour(theme::text.withAlpha(a));
                g.setFont(theme::font(22.f, true).withExtraKerningFactor(0.22f));
                g.drawText("DEASYNTH", outW - 232, outH - 124, 180, 48, juce::Justification::centredLeft);
            }
        }

        // title and end cards
        auto card = [&](float a, const juce::String &line1, const juce::String &line2, float grow) {
            if (a <= 0.f)
                return;
            g.setColour(juce::Colour(0xff090a0c).withAlpha(a * 0.94f));
            g.fillAll();
            g.setGradientFill(juce::ColourGradient(theme::osc.withAlpha(0.16f * a), outW * 0.5f, outH * 0.44f,
                                                   theme::osc.withAlpha(0.f), outW * 0.5f, outH * 1.1f, true));
            g.fillAll();
            const float size = 250.f * (0.94f + 0.06f * grow);
            g.setOpacity(a);
            theme::drawLogo(g, juce::Rectangle<float>(size, size * 1.1f).withCentre({outW * 0.5f, outH * 0.36f}));
            g.setOpacity(1.f);
            g.setColour(theme::text.withAlpha(a));
            g.setFont(theme::font(104.f, true).withExtraKerningFactor(0.26f));
            g.drawText("DEASYNTH", 0, (int)(outH * 0.56f), outW, 120, juce::Justification::centred);
            g.setColour(theme::osc.withAlpha(a));
            g.setFont(theme::font(34.f).withExtraKerningFactor(0.08f));
            g.drawText(line1, 0, (int)(outH * 0.56f) + 132, outW, 46, juce::Justification::centred);
            g.setColour(theme::textDim.withAlpha(a));
            g.setFont(theme::font(24.f));
            g.drawText(line2, 0, (int)(outH * 0.56f) + 190, outW, 36, juce::Justification::centred);
        };
        card(now < 4.6 ? 1.f : 1.f - ease((float)((now - 4.6) / 1.5)), "Analog soul. Modern engine.", "",
             ease((float)(now / 5.0)));
        card(ease((float)((now - 84.4) / 1.3)),
             juce::String::fromUTF8("Standalone   \xc2\xb7   Audio Unit   \xc2\xb7   VST3"),
             "Free software, built on the open source Surge XT engine.", ease((float)((now - 84.4) / 5.0)));

        // fade from and to black
        const float black = std::max(1.f - ease((float)(now / 0.7)), ease((float)((now - 89.1) / 0.85)));
        if (black > 0.f)
        {
            g.setColour(juce::Colours::black.withAlpha(black));
            g.fillAll();
        }

        auto file = outDir.getChildFile(juce::String::formatted("frame-%05d.jpg", frame));
        file.deleteFile();
        juce::FileOutputStream os(file);
        juce::JPEGImageFormat jpeg;
        jpeg.setQuality(0.93f);
        jpeg.writeImageToStream(out, os);
    }

    void finish()
    {
        juce::PopupMenu::dismissAllActiveMenus();

        // bring the soundtrack up to a sensible level, and fade its ends
        const size_t n = std::min(audioL.size(), (size_t)(seconds * sampleRate));
        float peak = 1e-6f;
        for (size_t i = 0; i < n; ++i)
            peak = std::max({peak, std::abs(audioL[i]), std::abs(audioR[i])});
        const float gain = 0.84f / peak;
        juce::AudioBuffer<float> wav(2, (int)n);
        for (size_t i = 0; i < n; ++i)
        {
            const double t = (double)i / sampleRate;
            const float env = (float)std::min({1.0, t / 0.4, (seconds - t) / 2.2});
            wav.setSample(0, (int)i, audioL[i] * gain * std::max(0.f, env));
            wav.setSample(1, (int)i, audioR[i] * gain * std::max(0.f, env));
        }
        auto file = outDir.getChildFile("soundtrack.wav");
        file.deleteFile();
        juce::WavAudioFormat format;
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream>(file);
        auto writer = format.createWriterFor(
            stream, juce::AudioFormatWriterOptions().withSampleRate(sampleRate).withNumChannels(2).withBitsPerSample(24));
        if (writer)
            writer->writeFromAudioSampleBuffer(wav, 0, wav.getNumSamples());
        writer.reset();

        std::cout << "Wrote " << frame << " frames and soundtrack.wav (peak before gain " << peak << ") to "
                  << outDir.getFullPathName() << std::endl;
        ok = peak > 0.01f;
        editorBase.reset();
        juce::MessageManager::getInstance()->stopDispatchLoop();
    }

    //================================================================ state
    juce::File outDir;
    DeasynthProcessor proc;
    std::unique_ptr<juce::AudioProcessorEditor> editorBase;
    DeasynthEditor *ed{nullptr};

    std::vector<Action> actions;
    std::vector<Tween> tweens;
    std::vector<CameraKey> cameraKeys;
    std::vector<Caption> captions;
    std::vector<NoteEvent> notes;
    size_t nextAction{0}, nextNote{0};

    Pt pointer{640.f, 400.f};
    juce::Component::SafePointer<juce::Component> held, hovered;
    Pt heldDown;
    double clickFlashAt{-10.0};

    std::vector<float> audioL, audioR;
    int64_t samplePos{0};
    int frame{0};
    double now{0.0};
    bool busy{false};
    bool ok{false};
    double previewFrom{0.0}, previewTo{1e9}; // seconds; for rendering just part of the film
};
} // namespace

int renderDeasynthDemo(const juce::String &directory, double from, double to)
{
    auto dir = juce::File::getCurrentWorkingDirectory().getChildFile(directory);
    std::cout << "Rendering the demo into " << dir.getFullPathName() << std::endl;

    Demo demo(dir);
    demo.previewFrom = from;
    demo.previewTo = to;
    if (!demo.proc.surge)
    {
        std::cout << "engine failed to start" << std::endl;
        return 1;
    }
    if (demo.synth().storage.patch_list.empty())
    {
        std::cout << "The factory library is not installed (scripts/deasynth/install-content.sh); "
                     "the demo needs its patches."
                  << std::endl;
        return 1;
    }
    juce::MessageManager::getInstance()->runDispatchLoop();
    return demo.ok ? 0 : 1;
}
