/*
 * Deasynth - an analog-voiced hybrid synthesizer built on the Surge XT engine.
 *
 * Deasynth is released under the GNU General Public Licence v3 or later (GPL-3.0-or-later).
 * The license is found in the "LICENSE" file in the root of this repository, or at
 * https://www.gnu.org/licenses/gpl-3.0.en.html
 *
 * The synthesis engine is Surge XT, copyright the Surge Synth Team and Claes Johanson,
 * https://github.com/surge-synthesizer/surge
 */

#ifndef DEASYNTH_SRC_DEASYNTHPROCESSOR_H
#define DEASYNTH_SRC_DEASYNTHPROCESSOR_H

#include "SurgeSynthesizer.h"
#include "SurgeStorage.h"
#include "dsp/AnalogStage.h"

#include "juce_audio_processors/juce_audio_processors.h"
#include "juce_audio_utils/juce_audio_utils.h"

#include <array>
#include <atomic>
#include <unordered_map>

class DeasynthProcessor;

/** Presents one engine Parameter to the host. */
struct DeasynthEngineParam : juce::RangedAudioParameter
{
    DeasynthEngineParam(DeasynthProcessor *proc, Parameter *p);

    juce::String getName(int maxLen) const override;
    float getValue() const override;
    float getDefaultValue() const override { return defaultValue; }
    void setValue(float f) override;
    float getValueForText(const juce::String &text) const override;
    juce::String getText(float normalisedValue, int) const override;
    bool isMetaParameter() const override { return true; }
    const juce::NormalisableRange<float> &getNormalisableRange() const override { return range; }

    /** Called from the editor while the user drags a control, so the host records automation. */
    std::atomic<bool> inEditGesture{false};

    juce::NormalisableRange<float> range{0.f, 1.f, 0.001f};
    DeasynthProcessor *proc{nullptr};
    SurgeSynthesizer *s{nullptr};
    Parameter *p{nullptr};
    float defaultValue{0.f};
};

/** Presents one of the eight macros to the host. */
struct DeasynthMacroParam : juce::RangedAudioParameter
{
    DeasynthMacroParam(DeasynthProcessor *proc, int macro);

    juce::String getName(int maxLen) const override;
    float getValue() const override;
    float getDefaultValue() const override { return 0.f; }
    void setValue(float f) override;
    float getValueForText(const juce::String &text) const override
    {
        return juce::jlimit(0.f, 1.f, text.getFloatValue() * 0.01f);
    }
    juce::String getText(float v, int) const override
    {
        return juce::String(v * 100.f, 1) + " %";
    }
    const juce::NormalisableRange<float> &getNormalisableRange() const override { return range; }

    juce::NormalisableRange<float> range{0.f, 1.f, 0.001f};
    SurgeSynthesizer *s{nullptr};
    int macro{0};
};

class DeasynthProcessor : public juce::AudioProcessor,
                          public SurgeSynthesizer::PluginLayer,
                          public juce::MidiKeyboardState::Listener
{
  public:
    DeasynthProcessor();
    ~DeasynthProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void reset() override;
    bool isBusesLayoutSupported(const BusesLayout &layouts) const override;
    void processBlock(juce::AudioBuffer<float> &, juce::MidiBuffer &) override;
    void processBlockBypassed(juce::AudioBuffer<float> &, juce::MidiBuffer &) override;

    juce::AudioProcessorEditor *createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Deasynth"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    bool supportsMPE() const override { return true; }
    double getTailLengthSeconds() const override { return 2.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return "Default"; }
    void changeProgramName(int, const juce::String &) override {}

    void getStateInformation(juce::MemoryBlock &destData) override;
    void setStateInformation(const void *data, int sizeInBytes) override;

    // SurgeSynthesizer::PluginLayer
    void surgeParameterUpdated(const SurgeSynthesizer::ID &id, float value) override;
    void surgeMacroUpdated(long macroNum, float value) override;

    // juce::MidiKeyboardState::Listener (the on-screen keyboard)
    void handleNoteOn(juce::MidiKeyboardState *, int midiChannel, int note, float vel) override;
    void handleNoteOff(juce::MidiKeyboardState *, int midiChannel, int note, float vel) override;

    DeasynthEngineParam *hostParamFor(const Parameter *p) const;

    /** Go back to the sound Deasynth starts with. Call from the message thread. */
    void loadInitPatch();

    /** Editor to audio thread messages (on-screen keyboard and wheels). */
    struct GuiMidi
    {
        enum Type
        {
            NoteOn,
            NoteOff,
            PitchBend,
            ModWheel
        } type{NoteOn};
        int channel{0}, a{0}, b{0};
    };
    void pushGuiMidi(const GuiMidi &m);

    /** Deasynth's own character controls, 0...1. Automatable, and saved with the session. */
    juce::AudioParameterFloat *vintageParam{nullptr};
    juce::AudioParameterFloat *warmthParam{nullptr};

    std::unique_ptr<SurgeSynthesizer> surge;
    std::string fatalErrorMessage;
    juce::MidiKeyboardState keyboardState;
    std::atomic<float> standaloneTempo{120.f};

    /** A decimated mono copy of the output for the oscilloscope. Written on the audio thread. */
    static constexpr int scopeSize = 2048;
    std::array<std::atomic<float>, scopeSize> scopeBuffer{};
    std::atomic<int> scopeWritePos{0};
    std::atomic<float> outputPeak[2]{};

    std::vector<DeasynthMacroParam *> macroParams;

    /** Where the editor last was, so reopening the window comes back to the same place. */
    struct EditorState
    {
        int page{0};
        float zoom{1.f};
        int lfo{0};
        int fxSlot{fxslot_ains1};
    } editorState;

  private:
    void applyMidi(const juce::MidiMessage &m);
    void drainGuiMidi();
    void updateTransport();

    std::unordered_map<int, DeasynthEngineParam *> paramsBySynthId;

    static constexpr int guiMidiSize = 1024;
    std::array<GuiMidi, guiMidiSize> guiMidi;
    std::atomic<int> guiMidiWrite{0}, guiMidiRead{0};
    bool addingFromMidi{false};

    juce::MemoryBlock initState;
    deasynth::AnalogStage analogStage;
    int blockPos{0};
    int scopeDecimate{0};
    int32_t noteIdCounter{1};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DeasynthProcessor)
};

#endif
