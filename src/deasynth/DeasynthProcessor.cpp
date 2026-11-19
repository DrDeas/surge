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

#include "DeasynthProcessor.h"
#include "gui/DeasynthEditor.h"

#include "sst/plugininfra/cpufeatures.h"
#include "PatchFileHeaderStructs.h"
#include "juce_audio_formats/juce_audio_formats.h"

namespace
{
constexpr uint32_t stateMagic = 0x44454153; // 'DEAS'
constexpr uint32_t stateVersion = 2;

// The engine uses this to read user wavetables and impulse responses in formats JUCE knows.
SurgeStorage::AudioFileInput loadAudioFile(std::string file)
{
    juce::AudioFormatManager manager;
    manager.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader{manager.createReaderFor(juce::File(file))};
    if (!reader)
        return {};

    juce::AudioBuffer<float> buf((int)reader->numChannels, (int)reader->lengthInSamples);
    if (!reader->read(&buf, 0, (int)reader->lengthInSamples, 0, true, true))
        return {};

    std::uint64_t sr = (std::uint64_t)reader->sampleRate;
    std::vector<std::vector<float>> channels(reader->numChannels);

    for (int i = 0; i < (int)reader->numChannels; i++)
    {
        channels[i].resize(reader->lengthInSamples);
        std::copy(buf.getReadPointer(i), buf.getReadPointer(i) + reader->lengthInSamples,
                  channels[i].begin());
    }

    return std::make_tuple(sr, std::move(channels));
}

/*
 * Factory content can ship inside the bundle (Contents/Resources/DeasynthData), which keeps
 * the app and the Audio Unit self-contained. If it is not there the engine falls back to
 * its usual search: /Library/Application Support/Deasynth, the per-user equivalent, or a
 * DeasynthData folder next to the binary.
 */
std::string findBundledDataPath()
{
    auto exe = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
    auto candidates = {
        exe.getParentDirectory().getSiblingFile("Resources").getChildFile("DeasynthData"),
        juce::File::getSpecialLocation(juce::File::currentApplicationFile)
            .getChildFile("Contents/Resources/DeasynthData")};

    for (const auto &c : candidates)
    {
        if (c.isDirectory())
            return c.getFullPathName().toStdString();
    }
    return {};
}

/*
 * The engine resets the current patch before it parses new data, so data that is not a
 * patch must be turned away here or it would wipe the sound that is loaded.
 */
bool looksLikeEnginePatch(const void *data, int size)
{
    if (!data || size < (int)sizeof(sst::io::patch_header))
        return false;
    sst::io::patch_header ph;
    memcpy(&ph, data, sizeof(ph));
    if (memcmp(ph.tag, "sub3", 4) != 0)
        return false;
    const auto xmlSize = juce::ByteOrder::swapIfBigEndian((juce::uint32)ph.xmlsize);
    return xmlSize > 0 && xmlSize <= (juce::uint32)(size - (int)sizeof(sst::io::patch_header));
}

void setProductIdentity()
{
    auto &id = SurgeStorage::productIdentity();
    id.name = "Deasynth";
    id.lowerName = "deasynth";
    id.vendor = "Eldrin";
    id.portableDataDir = "DeasynthData";
    id.portableUserDir = "DeasynthUserData";
    id.defaultsName = "Deasynth";
}

void setInt(SurgeSynthesizer &s, Parameter &p, int v)
{
    s.setParameter01(s.idForParameter(&p),
                     Parameter::intScaledToFloat(v, p.val_max.i, p.val_min.i), false, true);
}

void setFloat(SurgeSynthesizer &s, Parameter &p, float v)
{
    s.setParameter01(s.idForParameter(&p), p.value_to_normalized(v));
}

/*
 * The voice Deasynth starts from: one sawtooth through the vintage transistor ladder with
 * analog-curve envelopes and a touch of oscillator drift. Everything else is left at the
 * engine default so the panel reads as a clean slate.
 */
void applyDeasynthInit(SurgeSynthesizer &s)
{
    auto &patch = s.storage.getPatch();

    for (auto &scene : patch.scene)
    {
        setInt(s, scene.filterunit[0].type, sst::filters::FilterType::fut_vintageladder);
        setInt(s, scene.filterunit[0].subtype, 1);
        setFloat(s, scene.filterunit[0].cutoff, 14.f);
        setFloat(s, scene.filterunit[0].resonance, 0.18f);
        setFloat(s, scene.filterunit[0].envmod, 30.f);
        setFloat(s, scene.filterunit[0].keytrack, 0.5f);

        for (auto &eg : scene.adsr)
        {
            setInt(s, eg.mode, emt_analog);
        }
        setFloat(s, scene.adsr[adsr_ampeg].a, -8.f);
        setFloat(s, scene.adsr[adsr_ampeg].d, -1.f);
        setFloat(s, scene.adsr[adsr_ampeg].s, 0.85f);
        setFloat(s, scene.adsr[adsr_ampeg].r, -2.6f);
        setFloat(s, scene.adsr[adsr_filteg].a, -8.f);
        setFloat(s, scene.adsr[adsr_filteg].d, -1.4f);
        setFloat(s, scene.adsr[adsr_filteg].s, 0.3f);
        setFloat(s, scene.adsr[adsr_filteg].r, -2.2f);

        setFloat(s, scene.drift, 0.12f);
        // Serial 2 is a single path through both filters with the feedback loop available
        setInt(s, scene.filterblock_configuration, fc_serial2);
    }

    static const char *macroNames[n_customcontrollers] = {"Macro 1", "Macro 2", "Macro 3",
                                                          "Macro 4", "Macro 5", "Macro 6",
                                                          "Macro 7", "Macro 8"};
    for (int i = 0; i < n_customcontrollers; ++i)
    {
        if (patch.CustomControllerLabel[i][0] == '-' || patch.CustomControllerLabel[i][0] == 0)
        {
            strncpy(patch.CustomControllerLabel[i], macroNames[i],
                    CUSTOM_CONTROLLER_LABEL_SIZE - 1);
        }
    }

    patch.name = "Init Deasynth";
    patch.category = "Init";
    patch.author = "Deasynth";
    patch.isDirty = false;
}
} // namespace

//==============================================================================
DeasynthEngineParam::DeasynthEngineParam(DeasynthProcessor *pr, Parameter *par)
    : juce::RangedAudioParameter(juce::ParameterID(par->get_storage_name(), 1),
                                 par->get_full_name(),
                                 juce::AudioProcessorParameterWithIDAttributes()),
      proc(pr), s(pr->surge.get()), p(par)
{
    defaultValue = p->get_default_value_f01();
}

juce::String DeasynthEngineParam::getName(int maxLen) const
{
    char txt[TXT_SIZE];
    if (p->ctrlgroup == cg_FX)
        s->getParameterNameExtendedByFXGroup(s->idForParameter(p), txt);
    else
        s->getParameterName(s->idForParameter(p), txt);
    return juce::String(txt).substring(0, maxLen);
}

float DeasynthEngineParam::getValue() const { return s->getParameter01(s->idForParameter(p)); }

void DeasynthEngineParam::setValue(float f)
{
    // While the editor drags a control it writes to the engine itself and only reports to the
    // host, so ignore the echo. Otherwise this is automation or a host UI and must be applied.
    if (!inEditGesture && f != getValue())
    {
        s->setParameter01(s->idForParameter(p), f, true);
    }
}

float DeasynthEngineParam::getValueForText(const juce::String &text) const
{
    pdata onto;
    std::string err;
    if (p->set_value_from_string_onto(text.toStdString(), onto, err))
    {
        if (p->valtype == vt_float)
            return p->value_to_normalized(onto.f);
        if (p->valtype == vt_int)
            return p->value_to_normalized((float)onto.i);
        if (p->valtype == vt_bool)
            return p->value_to_normalized((float)onto.b);
    }
    return 0.f;
}

juce::String DeasynthEngineParam::getText(float normalisedValue, int) const
{
    return p->get_display(true, normalisedValue);
}

DeasynthMacroParam::DeasynthMacroParam(DeasynthProcessor *pr, int m)
    : juce::RangedAudioParameter(juce::ParameterID("macro_" + std::to_string(m), 1),
                                 "Macro " + std::to_string(m + 1),
                                 juce::AudioProcessorParameterWithIDAttributes()),
      s(pr->surge.get()), macro(m)
{
}

juce::String DeasynthMacroParam::getName(int maxLen) const
{
    juce::String res = "M" + juce::String(macro + 1) + ": ";
    res += s->storage.getPatch().CustomControllerLabel[macro];
    return res.substring(0, maxLen);
}

float DeasynthMacroParam::getValue() const { return s->getMacroParameterTarget01(macro); }

void DeasynthMacroParam::setValue(float f)
{
    if (f != getValue())
        s->setMacroParameter01(macro, f);
}

//==============================================================================
DeasynthProcessor::DeasynthProcessor()
    : juce::AudioProcessor(
          BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
    setProductIdentity();

    try
    {
        surge = std::make_unique<SurgeSynthesizer>(this, findBundledDataPath());
    }
    catch (const std::exception &e)
    {
        surge.reset();
        fatalErrorMessage = e.what();
        return;
    }

    surge->storage.load_audio_file = loadAudioFile;
    surge->hostProgram = juce::PluginHostType().getHostDescription();
    surge->juceWrapperType = getWrapperTypeDescription(wrapperType);
    surge->activateExtraOutputs = false;

    applyDeasynthInit(*surge);

    {
        void *data = nullptr; // the engine owns this
        const unsigned int size = surge->saveRaw(&data);
        initState.append(data, size);
    }

    auto root = std::make_unique<juce::AudioProcessorParameterGroup>("deasynth", "Deasynth", "|");

    auto character =
        std::make_unique<juce::AudioProcessorParameterGroup>("character", "Character", "|");
    auto pct = juce::AudioParameterFloatAttributes()
                   .withStringFromValueFunction(
                       [](float v, int) { return juce::String(v * 100.f, 0) + " %"; })
                   .withValueFromStringFunction(
                       [](const juce::String &t) { return t.getFloatValue() * 0.01f; });
    auto vint = std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("deasynth_vintage", 1), "Vintage",
        juce::NormalisableRange<float>(0.f, 1.f), 0.2f, pct);
    auto warm = std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("deasynth_warmth", 1), "Warmth",
        juce::NormalisableRange<float>(0.f, 1.f), 0.f, pct);
    vintageParam = vint.get();
    warmthParam = warm.get();
    character->addChild(std::move(vint));
    character->addChild(std::move(warm));
    root->addChild(std::move(character));

    auto macros = std::make_unique<juce::AudioProcessorParameterGroup>("macros", "Macros", "|");
    for (int m = 0; m < n_customcontrollers; ++m)
    {
        auto mp = std::make_unique<DeasynthMacroParam>(this, m);
        macroParams.push_back(mp.get());
        macros->addChild(std::move(mp));
    }
    root->addChild(std::move(macros));

    // Group the engine parameters the way the engine clumps them (global, scene A osc, ...)
    static const char *clumpNames[] = {"",            "Macros",        "Global & FX",
                                       "A Common",    "A Oscillators", "A Mixer",
                                       "A Filters",   "A Envelopes",   "A LFOs",
                                       "B Common",    "B Oscillators", "B Mixer",
                                       "B Filters",   "B Envelopes",   "B LFOs"};
    std::map<unsigned int, std::vector<std::unique_ptr<DeasynthEngineParam>>> byClump;

    for (auto par : surge->storage.getPatch().param_ptr)
    {
        if (!par)
            continue;
        parametermeta pm;
        surge->getParameterMeta(surge->idForParameter(par), pm);
        auto hp = std::make_unique<DeasynthEngineParam>(this, par);
        paramsBySynthId[par->id] = hp.get();
        byClump[pm.clump].push_back(std::move(hp));
    }

    for (auto &[clump, params] : byClump)
    {
        juce::String name = clump < std::size(clumpNames) ? clumpNames[clump] : "Other";
        auto g = std::make_unique<juce::AudioProcessorParameterGroup>(
            "grp_" + juce::String((int)clump), name, "|");
        for (auto &p : params)
            g->addChild(std::move(p));
        root->addChild(std::move(g));
    }

    addParameterGroup(std::move(root));

    keyboardState.addListener(this);
}

DeasynthProcessor::~DeasynthProcessor() { keyboardState.removeListener(this); }

void DeasynthProcessor::loadInitPatch()
{
    if (!surge || initState.getSize() == 0)
        return;
    surge->enqueuePatchForLoad(initState.getData(), (int)initState.getSize());
    surge->processAudioThreadOpsWhenAudioEngineUnavailable();
}

DeasynthEngineParam *DeasynthProcessor::hostParamFor(const Parameter *p) const
{
    if (!p)
        return nullptr;
    auto it = paramsBySynthId.find(p->id);
    return it == paramsBySynthId.end() ? nullptr : it->second;
}

//==============================================================================
void DeasynthProcessor::prepareToPlay(double sr, int)
{
    if (!surge)
        return;
    surge->setSamplerate((float)sr);
    analogStage.setSampleRate(sr);
    blockPos = 0;
}

void DeasynthProcessor::reset()
{
    blockPos = 0;
    if (surge)
        surge->stopSound();
    analogStage.reset();
}

bool DeasynthProcessor::isBusesLayoutSupported(const BusesLayout &layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo() &&
           layouts.getMainInputChannelSet().isDisabled();
}

void DeasynthProcessor::updateTransport()
{
    auto playhead = getPlayHead();
    bool gotHostTime = false;

    if (playhead && wrapperType != wrapperType_Standalone)
    {
        if (auto pos = playhead->getPosition())
        {
            if (auto bpm = pos->getBpm())
            {
                surge->time_data.tempo = *bpm;
                gotHostTime = true;
            }
            const bool rolling = pos->getIsPlaying() || pos->getIsRecording();
            surge->time_data.isPlaying = pos->getIsPlaying();
            surge->transportRunning = rolling;
            if (rolling)
            {
                if (auto ppq = pos->getPpqPosition())
                    surge->time_data.ppqPos = *ppq;
            }
            if (auto ts = pos->getTimeSignature())
            {
                surge->time_data.timeSigNumerator = ts->numerator;
                surge->time_data.timeSigDenominator = ts->denominator;
            }
        }
    }

    if (!gotHostTime)
    {
        if (surge->storage.unstreamedTempo > -1.f)
            standaloneTempo = surge->storage.unstreamedTempo;
        surge->time_data.tempo = standaloneTempo;
        surge->time_data.timeSigNumerator = 4;
        surge->time_data.timeSigDenominator = 4;
        surge->time_data.isPlaying = (wrapperType == wrapperType_Standalone);
        surge->transportRunning = false;
    }

    surge->resetStateFromTimeData();
}

void DeasynthProcessor::pushGuiMidi(const GuiMidi &m)
{
    // single producer (message thread), single consumer (audio thread)
    const int w = guiMidiWrite.load(std::memory_order_relaxed);
    const int next = (w + 1) % guiMidiSize;
    if (next == guiMidiRead.load(std::memory_order_acquire))
        return; // full; drop
    guiMidi[(size_t)w] = m;
    guiMidiWrite.store(next, std::memory_order_release);
}

void DeasynthProcessor::drainGuiMidi()
{
    int r = guiMidiRead.load(std::memory_order_relaxed);
    const int w = guiMidiWrite.load(std::memory_order_acquire);
    while (r != w)
    {
        const auto &m = guiMidi[(size_t)r];
        switch (m.type)
        {
        case GuiMidi::NoteOn:
            surge->playNote((char)m.channel, (char)m.a, (char)m.b, 0, noteIdCounter++);
            break;
        case GuiMidi::NoteOff:
            surge->releaseNote((char)m.channel, (char)m.a, (char)m.b);
            break;
        case GuiMidi::PitchBend:
            surge->pitchBend((char)m.channel, m.a);
            break;
        case GuiMidi::ModWheel:
            surge->channelController((char)m.channel, 1, m.a);
            break;
        }
        r = (r + 1) % guiMidiSize;
    }
    guiMidiRead.store(r, std::memory_order_release);
}

void DeasynthProcessor::handleNoteOn(juce::MidiKeyboardState *, int midiChannel, int note,
                                     float vel)
{
    if (!addingFromMidi)
        pushGuiMidi({GuiMidi::NoteOn, midiChannel - 1, note, juce::jlimit(1, 127, (int)(127 * vel))});
}

void DeasynthProcessor::handleNoteOff(juce::MidiKeyboardState *, int midiChannel, int note,
                                      float vel)
{
    if (!addingFromMidi)
        pushGuiMidi({GuiMidi::NoteOff, midiChannel - 1, note, (int)(127 * vel)});
}

void DeasynthProcessor::applyMidi(const juce::MidiMessage &m)
{
    const char ch = (char)(m.getChannel() - 1);

    {
        juce::ScopedValueSetter<bool> guard(addingFromMidi, true);
        keyboardState.processNextMidiEvent(m);
    }

    if (m.isNoteOn())
    {
        surge->playNote(ch, (char)m.getNoteNumber(), (char)m.getVelocity(), 0, -1);
    }
    else if (m.isNoteOff())
    {
        surge->releaseNote(ch, (char)m.getNoteNumber(), (char)m.getVelocity());
    }
    else if (m.isChannelPressure())
    {
        surge->channelAftertouch(ch, m.getChannelPressureValue());
    }
    else if (m.isAftertouch())
    {
        surge->polyAftertouch(ch, m.getNoteNumber(), m.getAfterTouchValue());
    }
    else if (m.isPitchWheel())
    {
        surge->pitchBend(ch, m.getPitchWheelValue() - 8192);
    }
    else if (m.isController())
    {
        surge->channelController(ch, m.getControllerNumber(), m.getControllerValue());
    }
    else if (m.isProgramChange())
    {
        surge->programChange(ch, m.getProgramChangeNumber());
    }
}

void DeasynthProcessor::processBlockBypassed(juce::AudioBuffer<float> &buffer,
                                             juce::MidiBuffer &midi)
{
    if (surge && surge->audio_processing_active)
    {
        surge->stopSound();
        surge->audio_processing_active = false;
    }
    midi.clear();
    buffer.clear();
}

void DeasynthProcessor::processBlock(juce::AudioBuffer<float> &buffer, juce::MidiBuffer &midi)
{
    auto fpuguard = sst::plugininfra::cpufeatures::FPUStateGuard();

    if (!surge || buffer.getNumChannels() < 2)
    {
        buffer.clear();
        return;
    }

    if (!surge->audio_processing_active)
    {
        // Just becoming active; there may be lingering notes from before we were deactivated
        surge->stopSound();
    }
    surge->audio_processing_active = true;
    surge->process_input = false;

    surge->storage.vintageAmount = vintageParam->get();
    const float warmth = warmthParam->get();

    updateTransport();
    drainGuiMidi();

    auto *outL = buffer.getWritePointer(0);
    auto *outR = buffer.getWritePointer(1);
    const int n = buffer.getNumSamples();

    auto midiIt = midi.cbegin();
    const auto midiEnd = midi.cend();

    float peakL = 0.f, peakR = 0.f;

    for (int i = 0; i < n; ++i)
    {
        while (midiIt != midiEnd && (*midiIt).samplePosition <= i)
        {
            applyMidi((*midiIt).getMessage());
            ++midiIt;
        }

        if (blockPos == 0)
        {
            surge->process();
            analogStage.process(surge->output[0], surge->output[1], warmth);
            surge->time_data.ppqPos +=
                (double)BLOCK_SIZE * surge->time_data.tempo / (60. * surge->storage.samplerate);
        }

        const float l = surge->output[0][blockPos];
        const float r = surge->output[1][blockPos];
        outL[i] = l;
        outR[i] = r;
        peakL = std::max(peakL, std::abs(l));
        peakR = std::max(peakR, std::abs(r));

        if (++scopeDecimate >= 2)
        {
            scopeDecimate = 0;
            const int w = scopeWritePos.load(std::memory_order_relaxed);
            scopeBuffer[(size_t)w].store(0.5f * (l + r), std::memory_order_relaxed);
            scopeWritePos.store((w + 1) % scopeSize, std::memory_order_relaxed);
        }

        blockPos = (blockPos + 1) & (BLOCK_SIZE - 1);
    }

    while (midiIt != midiEnd)
    {
        applyMidi((*midiIt).getMessage());
        ++midiIt;
    }

    for (int c = 2; c < buffer.getNumChannels(); ++c)
        buffer.clear(c, 0, n);

    outputPeak[0].store(std::max(peakL, outputPeak[0].load(std::memory_order_relaxed)),
                        std::memory_order_relaxed);
    outputPeak[1].store(std::max(peakR, outputPeak[1].load(std::memory_order_relaxed)),
                        std::memory_order_relaxed);

    // set by the engine when an oscillator or effect changes type, and by macro renames
    if (std::atomic_exchange(&parameterInfoUpdated, false))
    {
        updateHostDisplay(
            juce::AudioProcessorListener::ChangeDetails().withParameterInfoChanged(true));
    }

    if (std::atomic_exchange(&surge->patchChanged, false))
    {
        updateHostDisplay(juce::AudioProcessorListener::ChangeDetails()
                              .withParameterInfoChanged(true)
                              .withNonParameterStateChanged(true));
    }
}

//==============================================================================
juce::AudioProcessorEditor *DeasynthProcessor::createEditor()
{
    return new DeasynthEditor(*this);
}

void DeasynthProcessor::getStateInformation(juce::MemoryBlock &destData)
{
    if (!surge)
        return;

    surge->populateDawExtraState();

    void *data = nullptr; // the engine owns this
    const unsigned int engineSize = surge->saveRaw(&data);

    juce::MemoryOutputStream out(destData, false);
    out.writeInt((int)stateMagic);
    out.writeInt((int)stateVersion);
    out.writeFloat(vintageParam->get());
    out.writeFloat(warmthParam->get());
    out.writeInt(editorState.page);
    out.writeFloat(editorState.zoom);
    out.writeInt((int)engineSize);
    out.write(data, engineSize);
}

void DeasynthProcessor::setStateInformation(const void *data, int sizeInBytes)
{
    if (!surge || sizeInBytes <= 0)
        return;

    const void *engineData = data;
    int engineSize = sizeInBytes;

    juce::MemoryInputStream in(data, (size_t)sizeInBytes, false);
    if (sizeInBytes >= 28 && (uint32_t)in.readInt() == stateMagic)
    {
        const auto version = (uint32_t)in.readInt();
        const float vintage = in.readFloat();
        const float warmth = in.readFloat();
        int page = editorState.page;
        float zoom = editorState.zoom;
        if (version >= 2)
        {
            page = juce::jlimit(0, 2, in.readInt());
            const float z = in.readFloat();
            if (std::isfinite(z))
                zoom = juce::jlimit(0.5f, 2.f, z);
        }
        const int declared = in.readInt();
        const int remaining = (int)in.getNumBytesRemaining();

        if (declared <= 0 || declared > remaining)
            return; // truncated or corrupt; keep what we have

        engineData = (const char *)data + in.getPosition();
        engineSize = declared;
        if (!looksLikeEnginePatch(engineData, engineSize))
            return;

        editorState.page = page;
        editorState.zoom = zoom;
        if (std::isfinite(vintage))
            *vintageParam = juce::jlimit(0.f, 1.f, vintage);
        if (std::isfinite(warmth))
            *warmthParam = juce::jlimit(0.f, 1.f, warmth);
    }
    // else: no Deasynth header, so treat the whole block as a bare engine patch

    if (!looksLikeEnginePatch(engineData, engineSize))
        return;

    surge->enqueuePatchForLoad(engineData, engineSize);
    surge->processAudioThreadOpsWhenAudioEngineUnavailable();
}

void DeasynthProcessor::surgeParameterUpdated(const SurgeSynthesizer::ID &id, float value)
{
    auto it = paramsBySynthId.find(id.getSynthSideId());
    if (it != paramsBySynthId.end())
        it->second->setValueNotifyingHost(value);
}

void DeasynthProcessor::surgeMacroUpdated(long macroNum, float value)
{
    if (macroNum >= 0 && macroNum < (long)macroParams.size())
        macroParams[(size_t)macroNum]->setValueNotifyingHost(value);
}

//==============================================================================
juce::AudioProcessor *JUCE_CALLTYPE createPluginFilter() { return new DeasynthProcessor(); }
