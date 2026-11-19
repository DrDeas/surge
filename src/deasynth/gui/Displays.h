/*
 * Deasynth - an analog-voiced hybrid synthesizer built on the Surge XT engine.
 *
 * Deasynth is released under the GNU General Public Licence v3 or later (GPL-3.0-or-later).
 * The license is found in the "LICENSE" file in the root of this repository, or at
 * https://www.gnu.org/licenses/gpl-3.0.en.html
 */

#ifndef DEASYNTH_SRC_GUI_DISPLAYS_H
#define DEASYNTH_SRC_GUI_DISPLAYS_H

#include "Widgets.h"
#include "Oscillator.h"

namespace sst::filters
{
class FilterPlotter;
}

namespace deasynth
{
/*
 * Every display follows the same rule: what it draws is computed by the engine's own DSP
 * (a real oscillator, the real filter, a real LFO), never by a lookalike approximation, so
 * what you see is what you hear. Each one fingerprints the parameters it depends on and
 * only recomputes when that fingerprint changes.
 */

/** One cycle-ish of the oscillator's actual output. Click to pick a wavetable if it uses one. */
class OscDisplay : public juce::Component, public Refreshable
{
  public:
    OscDisplay(EditorContext &ctx, int oscIndex);

    void refresh(bool force) override;
    void paint(juce::Graphics &g) override;
    void mouseDown(const juce::MouseEvent &e) override;
    void mouseEnter(const juce::MouseEvent &) override;
    void mouseExit(const juce::MouseEvent &) override;

  private:
    OscillatorStorage &osc() const { return ctx.sceneData().osc[index]; }
    void recompute();
    void showWavetableMenu();

    EditorContext &ctx;
    int index;
    juce::Path wave;
    bool haveWave{false}, hovering{false};
    uint64_t fingerprint{0};
    juce::String caption;

    unsigned char oscbuffer alignas(16)[oscillator_buffer_size];
    std::vector<pdata> tp;
};

/**
 * Magnitude response of one filter unit, measured from the real filter.
 * Drag on it: left and right moves the cutoff, up and down the resonance.
 */
class FilterDisplay : public juce::Component, public Refreshable
{
  public:
    FilterDisplay(EditorContext &ctx, int unit);
    ~FilterDisplay() override;

    void refresh(bool force) override;
    void paint(juce::Graphics &g) override;
    void resized() override { fingerprint = 0; }
    void mouseDown(const juce::MouseEvent &e) override;
    void mouseDrag(const juce::MouseEvent &e) override;
    void mouseUp(const juce::MouseEvent &e) override;
    void mouseEnter(const juce::MouseEvent &e) override;
    void mouseExit(const juce::MouseEvent &e) override;

  private:
    void recompute();
    void endDrag();
    // the parameters the drag began on; the layer being edited can change under a drag
    Parameter *dragCutoff{nullptr}, *dragResonance{nullptr};
    juce::Point<float> lastDrag;

    EditorContext &ctx;
    int unit;
    std::unique_ptr<sst::filters::FilterPlotter> plotter;
    juce::Path curve;
    bool off{true};
    uint64_t fingerprint{0};
};

/**
 * The ADSR contour. Its three handles can be dragged: the peak sets the attack time, the
 * corner after it sets decay time and sustain level, the last one sets the release time.
 */
class EnvDisplay : public juce::Component, public Refreshable
{
  public:
    EnvDisplay(EditorContext &ctx, int which, juce::Colour accent);
    ~EnvDisplay() override { endDrag(); }

    void refresh(bool force) override;
    void paint(juce::Graphics &g) override;
    void mouseMove(const juce::MouseEvent &e) override;
    void mouseDown(const juce::MouseEvent &e) override;
    void mouseDrag(const juce::MouseEvent &e) override;
    void mouseUp(const juce::MouseEvent &e) override;
    void mouseExit(const juce::MouseEvent &e) override;

  private:
    struct Geometry
    {
        juce::Rectangle<float> area;
        float xDecay, xSus, xRel, xEnd, sustainY;
    };
    Geometry geometry() const;
    int handleAt(juce::Point<float> p) const;

    // each timed stage takes between stageMin and stageMin + stageSpan of the width
    static constexpr float stageMin = 0.03f, stageSpan = 0.24f, sustainWidth = 0.16f;
    void endDrag();
    int hot{-1}, grabbed{-1};
    Parameter *dragTime{nullptr}, *dragSustain{nullptr};
    juce::Point<float> lastDrag;
    EditorContext &ctx;
    int which;
    juce::Colour accent;
    uint64_t fingerprint{0};
};

/** The selected LFO's output over time, including its own envelope. */
class LfoDisplay : public juce::Component, public Refreshable
{
  public:
    explicit LfoDisplay(EditorContext &ctx);

    void setLfo(int i)
    {
        lfo = i;
        fingerprint = 0;
    }
    void refresh(bool force) override;
    void paint(juce::Graphics &g) override;
    void resized() override { fingerprint = 0; }

  private:
    void recompute();

    EditorContext &ctx;
    int lfo{0};
    juce::Path wave;
    float windowSeconds{1.f};
    uint64_t fingerprint{0};
    std::vector<pdata> tp;
};

/** Bars for the 16-step sequencer shape. Drag to draw. */
class StepEditor : public juce::Component, public Refreshable
{
  public:
    explicit StepEditor(EditorContext &ctx);

    void setLfo(int i) { lfo = i; }
    void refresh(bool force) override;
    void paint(juce::Graphics &g) override;
    void mouseDown(const juce::MouseEvent &e) override { edit(e); }
    void mouseDrag(const juce::MouseEvent &e) override { edit(e); }
    void mouseDoubleClick(const juce::MouseEvent &e) override;

  private:
    StepSequencerStorage &steps() const { return ctx.patch().stepsequences[ctx.scene][lfo]; }
    void edit(const juce::MouseEvent &e);

    EditorContext &ctx;
    int lfo{0};
    uint64_t fingerprint{0};
};

/** Output oscilloscope with a stereo peak meter beside it. */
class ScopeDisplay : public juce::Component, public Refreshable
{
  public:
    explicit ScopeDisplay(EditorContext &ctx);

    void refresh(bool force) override;
    void paint(juce::Graphics &g) override;

  private:
    EditorContext &ctx;
    juce::Path trace;
    float peak[2]{0.f, 0.f};
    float hold[2]{0.f, 0.f};
    bool wasSilent{false};
};
} // namespace deasynth

#endif
