/*
 * Deasynth - an analog-voiced hybrid synthesizer built on the Surge XT engine.
 *
 * Deasynth is released under the GNU General Public Licence v3 or later (GPL-3.0-or-later).
 * The license is found in the "LICENSE" file in the root of this repository, or at
 * https://www.gnu.org/licenses/gpl-3.0.en.html
 */

#ifndef DEASYNTH_SRC_GUI_PANELS_H
#define DEASYNTH_SRC_GUI_PANELS_H

#include "Widgets.h"
#include "Displays.h"

namespace deasynth
{
/*
 * A Section which owns its controls. rebind() points every control at the right engine
 * parameter for the scene being edited (called when the scene changes); refresh() is the
 * 30 Hz poll.
 */
class Panel : public Section, public Refreshable
{
  public:
    Panel(EditorContext &ctx, const juce::String &title, juce::Colour accent);

    virtual void rebind() {}
    void refresh(bool force) override;

  protected:
    template <typename T, typename... Args> T *add(Args &&...args)
    {
        auto c = std::make_unique<T>(std::forward<Args>(args)...);
        auto *raw = c.get();
        addAndMakeVisible(*raw);
        if (auto *r = dynamic_cast<Refreshable *>(raw))
            refreshables.push_back(r);
        owned.push_back(std::move(c));
        return raw;
    }
    ParamControl *knob(const juce::String &label = {},
                       ParamControl::Look look = ParamControl::Look::Auto);

    /** Lay controls left to right in equal cells across a row. */
    static void layoutRow(juce::Rectangle<int> row, std::initializer_list<juce::Component *> items);

    EditorContext &ctx;
    std::vector<std::unique_ptr<juce::Component>> owned;
    std::vector<Refreshable *> refreshables;
};

class OscPanel : public Panel
{
  public:
    OscPanel(EditorContext &ctx, int index);
    void rebind() override;
    void resized() override;

  private:
    int index;
    OscDisplay *display;
    ParamControl *type, *keytrack, *retrigger, *octave, *pitch;
    ParamControl *params[n_osc_params];
};

class MixerPanel : public Panel
{
  public:
    explicit MixerPanel(EditorContext &ctx);
    void rebind() override;
    void resized() override;

  private:
    ParamControl *level[6], *mute[6];
    ParamControl *noiseColour, *preGain, *fmRouting, *fmDepth, *drift;
};

class FilterPanel : public Panel
{
  public:
    FilterPanel(EditorContext &ctx, int unit);
    void rebind() override;
    void resized() override;
    void refresh(bool force) override;

  private:
    int unit;
    FilterDisplay *display;
    ParamControl *type, *subtype, *cutoff, *resonance, *envmod, *keytrack;
    ParamControl *offset{nullptr}, *link{nullptr};
};

class ShaperPanel : public Panel
{
  public:
    explicit ShaperPanel(EditorContext &ctx);
    void rebind() override;
    void resized() override;

  private:
    ParamControl *routing, *feedback, *balance, *highpass, *shape, *drive;
};

class EnvPanel : public Panel
{
  public:
    EnvPanel(EditorContext &ctx, int which);
    void rebind() override;
    void resized() override;

  private:
    int which;
    EnvDisplay *display;
    ParamControl *mode, *a, *d, *s, *r;
    ParamControl *extra[3];
};

class LfoPanel : public Panel
{
  public:
    explicit LfoPanel(EditorContext &ctx);
    void rebind() override;
    void resized() override;
    void refresh(bool force) override;
    void selectLfo(int i);
    int selected() const { return current; }

  private:
    int current{0};
    bool showingSteps{false};
    FlatButton *tabs[n_lfos];
    FlatButton *assign;
    LfoDisplay *display;
    StepEditor *steps;
    ParamControl *shape, *rate, *phase, *amount, *deform, *trigger, *unipolar;
    ParamControl *env[6];
};

class MacroPanel : public Panel
{
  public:
    explicit MacroPanel(EditorContext &ctx);
    void resized() override;
    void refresh(bool force) override;

  private:
    void rename(int i);
    ValueKnob *knobs[n_customcontrollers];
    FlatButton *assign[n_customcontrollers];
    std::unique_ptr<juce::TextEditor> renameBox;
};

/** Every active modulation routing of the current layer, editable in one list. */
class MatrixPanel : public Panel, private juce::ListBoxModel
{
  public:
    explicit MatrixPanel(EditorContext &ctx);
    void rebind() override { rebuild(); }
    void resized() override;
    void refresh(bool force) override;
    void paintOverChildren(juce::Graphics &g) override;

  private:
    struct Row
    {
        int source;
        int paramId;
    };
    class RowComponent;

    void rebuild();
    int getNumRows() override { return (int)rows.size(); }
    void paintListBoxItem(int, juce::Graphics &, int, int, bool) override {}
    juce::Component *refreshComponentForRow(int row, bool selected, juce::Component *existing) override;

    std::vector<Row> rows;
    juce::ListBox list;
};

class FxPanel : public Panel
{
  public:
    explicit FxPanel(EditorContext &ctx);
    void rebind() override;
    void resized() override;
    void refresh(bool force) override;
    void paint(juce::Graphics &g) override;

  private:
    void selectSlot(int slot);
    void toggleBypass(int slot);

    int current{fxslot_ains1};
    FlatButton *slots[n_fx_slots];
    ParamControl *chainBypass;
    ParamControl *sends[n_send_slots];
    ParamControl *type, *returnLevel;
    ParamControl *params[n_fx_params];
    juce::Rectangle<int> chainLabelArea[n_fx_chains];
    juce::Rectangle<int> editorArea, sendsLabelArea;
};

/** Play mode, glide, bend range and the two Deasynth character controls. */
class VoicePanel : public Panel
{
  public:
    explicit VoicePanel(EditorContext &ctx);
    void rebind() override;
    void resized() override;

  private:
    ParamControl *mode, *glide, *bendUp, *bendDown;
    ValueKnob *vintage, *warmth;
};

class OutputPanel : public Panel
{
  public:
    explicit OutputPanel(EditorContext &ctx);
    void rebind() override;
    void resized() override;

  private:
    ParamControl *volume, *pan, *width, *master;
};
} // namespace deasynth

#endif
