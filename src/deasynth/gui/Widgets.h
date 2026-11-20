/*
 * Deasynth - an analog-voiced hybrid synthesizer built on the Surge XT engine.
 *
 * Deasynth is released under the GNU General Public Licence v3 or later (GPL-3.0-or-later).
 * The license is found in the "LICENSE" file in the root of this repository, or at
 * https://www.gnu.org/licenses/gpl-3.0.en.html
 */

#ifndef DEASYNTH_SRC_GUI_WIDGETS_H
#define DEASYNTH_SRC_GUI_WIDGETS_H

#include "Theme.h"
#include "SurgeSynthesizer.h"
#include <functional>

class DeasynthProcessor;
struct DeasynthEngineParam;

namespace deasynth
{
/*
 * Shared by every widget in the editor: the engine, which scene (layer) is being edited,
 * and which modulation source, if any, is currently armed for assignment.
 */
struct EditorContext
{
    DeasynthProcessor &proc;
    SurgeSynthesizer *synth{nullptr};

    int scene{0};
    int armedSource{ms_original}; // ms_original means nothing is armed

    /** Something changed that can alter what controls mean (osc type, FX type, patch...). */
    std::function<void()> structureChanged;
    /** One line of contextual help in the header. Empty clears it. */
    std::function<void(const juce::String &)> setHint;

    SurgePatch &patch() const { return synth->storage.getPatch(); }
    SurgeSceneStorage &sceneData() const { return patch().scene[scene]; }

    /**
     * Editing an engine parameter from something other than a ParamControl (dragging on a
     * display). Bracket a drag with beginEdit / endEdit so the host records one automation
     * gesture.
     */
    void beginEdit(Parameter *p) const;
    void setValue01(Parameter *p, float v) const;
    void endEdit(Parameter *p) const;

    bool isArmed() const { return armedSource != ms_original; }
    static juce::Colour colourForSource(int ms);
    juce::String nameForSource(int ms, bool shortName = false) const;
    void markDirty() const { patch().isDirty = true; }
};

/** Implemented by anything the editor's timer should poll for changes. */
struct Refreshable
{
    virtual ~Refreshable() = default;
    /** Called ~30x a second. Repaint only if something visible changed, or if forced. */
    virtual void refresh(bool force) = 0;
};

/*
 * The one control used for every engine parameter. It looks at what the parameter currently
 * is and draws itself to suit: a rotary knob for continuous values, a stepped selector for
 * integer choices, an illuminated switch for booleans. Oscillator and effect parameters
 * change type when the algorithm changes, and this follows along without being rebuilt.
 *
 * Interaction:
 *   drag / wheel            change value            (shift = fine)
 *   double click            reset to default
 *   right click             menu: type in a value, tempo sync, range, clear modulation...
 *   drag while a mod source is armed
 *                           set that source's modulation depth to this parameter
 */
class ParamControl : public juce::Component, public Refreshable, public juce::SettableTooltipClient
{
  public:
    enum class Look
    {
        Auto,   // decide from the parameter's value type
        Knob,   // always a knob (integer parameters step)
        Choice, // always a selector box
        Toggle  // always a switch
    };

    ParamControl(EditorContext &ctx, juce::Colour accent, Look look = Look::Auto);
    ~ParamControl() override;

    void setParam(Parameter *p);
    Parameter *getParam() const { return param; }
    void setLabel(const juce::String &l) { labelOverride = l; }
    void setAccent(juce::Colour c) { accent = c; }
    /** Selector boxes can drop the caption underneath and be just the box. */
    void setShowLabel(bool b) { showLabel = b; }
    /** For switches whose engine parameter means "off" when true (mute): light up when false. */
    void setInverted(bool b) { inverted = b; }
    /** Limit the entries a selector offers (e.g. hide choices that make no sense here). */
    std::function<bool(int)> choiceFilter;
    /** Called after the user changes the value. */
    std::function<void()> onChange;

    void refresh(bool force) override;

    void paint(juce::Graphics &g) override;
    void mouseDown(const juce::MouseEvent &e) override;
    void mouseDrag(const juce::MouseEvent &e) override;
    void mouseUp(const juce::MouseEvent &e) override;
    void mouseDoubleClick(const juce::MouseEvent &e) override;
    void mouseEnter(const juce::MouseEvent &e) override;
    void mouseExit(const juce::MouseEvent &e) override;
    void mouseWheelMove(const juce::MouseEvent &e, const juce::MouseWheelDetails &w) override;

  private:
    Look resolvedLook() const;
    bool usable() const;
    bool canModulateWithArmed() const;
    float value01() const;
    void setValue01(float v, bool forceInteger = false);
    void setIntValue(int i);
    void beginGesture();
    void endGesture();
    void showChoiceMenu();
    void showContextMenu();
    void showTypeIn();
    juce::String labelText() const;
    void paintKnob(juce::Graphics &g);
    void paintChoice(juce::Graphics &g);
    void paintToggle(juce::Graphics &g);

    EditorContext &ctx;
    juce::Colour accent;
    Look look;
    Parameter *param{nullptr};
    juce::String labelOverride;
    bool showLabel{true};
    bool inverted{false};

    // drag state
    bool dragging{false}, draggingMod{false}, hovering{false}, inGesture{false};
    float dragStartValue{0.f};
    float dragAccum{0.f};
    juce::Point<float> lastDragPos;
    float wheelAccum{0.f};

    // what was last painted, to detect change
    int cachedRaw{0};
    int cachedType{-1};
    float cachedMod{0.f};
    bool cachedModUsed{false}, cachedDeact{false}, cachedArmedValid{false};
    juce::String cachedName;

    std::unique_ptr<juce::TextEditor> typeIn;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ParamControl)
};

/** A knob for a plain 0...1 value that is not an engine Parameter (macros, character). */
class ValueKnob : public juce::Component, public Refreshable, public juce::SettableTooltipClient
{
  public:
    ValueKnob(juce::Colour accent, const juce::String &label);
    ~ValueKnob() override
    {
        if (dragging && gesture)
            gesture(false);
    }

    std::function<float()> getValue;
    std::function<void(float)> setValue;
    std::function<void(bool)> gesture; // true = begin, false = end
    std::function<juce::String(float)> format;
    std::function<void(juce::PopupMenu &)> extendMenu;
    float defaultValue{0.f};
    juce::String label;

    void refresh(bool force) override;
    void paint(juce::Graphics &g) override;
    void mouseDown(const juce::MouseEvent &e) override;
    void mouseDrag(const juce::MouseEvent &e) override;
    void mouseUp(const juce::MouseEvent &e) override;
    void mouseDoubleClick(const juce::MouseEvent &e) override;
    void mouseEnter(const juce::MouseEvent &) override
    {
        hovering = true;
        repaint();
    }
    void mouseExit(const juce::MouseEvent &) override
    {
        hovering = false;
        repaint();
    }
    void mouseWheelMove(const juce::MouseEvent &e, const juce::MouseWheelDetails &w) override;

  private:
    juce::Colour accent;
    bool dragging{false}, hovering{false};
    juce::Point<float> lastDragPos;
    float cached{-1.f};
    juce::String cachedLabel;
};

/** A flat, rounded button. Can be a toggle, can carry an accent, can draw a small glyph. */
class FlatButton : public juce::Component, public juce::SettableTooltipClient
{
  public:
    enum class Glyph
    {
        None,
        Prev,
        Next,
        Menu,
        Save,
        Close,
        Dropdown,
        Library
    };

    explicit FlatButton(const juce::String &text = {}, juce::Colour accent = theme::osc);

    std::function<void()> onClick;
    std::function<void()> onRightClick;

    void setText(const juce::String &t)
    {
        if (t != text)
        {
            text = t;
            repaint();
        }
    }
    const juce::String &getText() const { return text; }
    void setOn(bool b)
    {
        if (b != on)
        {
            on = b;
            repaint();
        }
    }
    bool isOn() const { return on; }
    void setAccent(juce::Colour c)
    {
        accent = c;
        repaint();
    }
    void setGlyph(Glyph g) { glyph = g; }
    void setQuiet(bool q) { quiet = q; }
    void setMarker(bool m)
    {
        if (m != marker)
        {
            marker = m;
            repaint();
        }
    }
    void setJustification(juce::Justification j) { just = j; }
    void setFontSize(float s) { fontSize = s; }

    void paint(juce::Graphics &g) override;
    void mouseDown(const juce::MouseEvent &e) override;
    void mouseUp(const juce::MouseEvent &e) override;
    void mouseEnter(const juce::MouseEvent &) override
    {
        hovering = true;
        repaint();
    }
    void mouseExit(const juce::MouseEvent &) override
    {
        hovering = false;
        repaint();
    }

  private:
    juce::String text;
    juce::Colour accent;
    Glyph glyph{Glyph::None};
    juce::Justification just{juce::Justification::centred};
    float fontSize{12.f};
    bool on{false}, hovering{false}, down{false}, quiet{false}, marker{false};
};

/** A titled panel. Children are laid out by the owner inside getContentBounds(). */
class Section : public juce::Component
{
  public:
    Section(const juce::String &title, juce::Colour accent);

    void setTitle(const juce::String &t)
    {
        title = t;
        repaint();
    }
    void setAccent(juce::Colour c)
    {
        accent = c;
        repaint();
    }
    juce::Colour getAccent() const { return accent; }
    /** The strip to the right of the title, for selectors and switches. */
    juce::Rectangle<int> getHeaderBounds() const;
    juce::Rectangle<int> getContentBounds() const;

    void paint(juce::Graphics &g) override;

    static constexpr int headerHeight = 30;

  private:
    juce::String title;
    juce::Colour accent;
};

/**
 * Where popup menus should live: the editor's scaled design surface, so that menus follow
 * the window zoom and use the editor's look and feel. Falls back to the top level component.
 */
juce::Component *menuParentFor(juce::Component &c);

// drawing helpers shared between controls
namespace draw
{
/** startAngle and endAngle of the knob travel, radians clockwise from 12 o'clock. */
constexpr float knobStart = -2.356194f; // -135 degrees
constexpr float knobEnd = 2.356194f;

void knob(juce::Graphics &g, juce::Rectangle<float> area, float value01, bool bipolar,
          juce::Colour accent, bool enabled, bool hot);
void modRing(juce::Graphics &g, juce::Rectangle<float> area, float value01, float depth,
             juce::Colour colour);
void displayWell(juce::Graphics &g, juce::Rectangle<float> r);
void captionBelow(juce::Graphics &g, juce::Rectangle<int> area, const juce::String &text,
                  juce::Colour c);
/** Shorten a parameter name so it reads at caption size ("Unison Detune" -> "Uni Detune"). */
juce::String shortLabel(const juce::String &name);
} // namespace draw
} // namespace deasynth

#endif
