/*
 * Deasynth - an analog-voiced hybrid synthesizer built on the Surge XT engine.
 *
 * Deasynth is released under the GNU General Public Licence v3 or later (GPL-3.0-or-later).
 * The license is found in the "LICENSE" file in the root of this repository, or at
 * https://www.gnu.org/licenses/gpl-3.0.en.html
 */

/*
 * A self-contained check of the parts of Deasynth that are its own: the processor wrapper,
 * session state, the voice variance and the output stage, and that the editor builds and
 * paints every page. Run with no arguments; exits non-zero on the first failure.
 *
 *   deasynth-selftest [directory to write page snapshots into]
 *   deasynth-selftest --write-patches <directory>     regenerate the starter patches
 *   deasynth-selftest --render-demo <directory>       render the demo film's frames and sound
 */

#include "../DeasynthProcessor.h"
#include "../gui/DeasynthEditor.h"

#include <cmath>
#include <iostream>

namespace
{
int failures = 0;

void check(bool ok, const std::string &what)
{
    std::cout << (ok ? "  ok    " : "  FAIL  ") << what << std::endl;
    if (!ok)
        ++failures;
}

struct Rendered
{
    float peak{0.f};
    double rms{0.0};
    bool finite{true};
};

Rendered render(DeasynthProcessor &p, int blocks, juce::MidiBuffer *firstBlockMidi = nullptr,
                int blockSize = 512)
{
    Rendered r;
    juce::AudioBuffer<float> buf(2, blockSize);
    double sum = 0.0;
    for (int b = 0; b < blocks; ++b)
    {
        buf.clear();
        juce::MidiBuffer midi;
        if (b == 0 && firstBlockMidi)
            midi = *firstBlockMidi;
        p.processBlock(buf, midi);
        for (int c = 0; c < 2; ++c)
        {
            for (int i = 0; i < blockSize; ++i)
            {
                const float v = buf.getSample(c, i);
                if (!std::isfinite(v))
                    r.finite = false;
                r.peak = std::max(r.peak, std::abs(v));
                sum += (double)v * v;
            }
        }
    }
    r.rms = std::sqrt(sum / (2.0 * blocks * blockSize));
    return r;
}

juce::MidiBuffer noteOn(int note, int vel = 100)
{
    juce::MidiBuffer m;
    m.addEvent(juce::MidiMessage::noteOn(1, note, (juce::uint8)vel), 0);
    return m;
}

juce::MidiBuffer noteOff(int note)
{
    juce::MidiBuffer m;
    m.addEvent(juce::MidiMessage::noteOff(1, note), 0);
    return m;
}

void testAudio()
{
    std::cout << "Audio path" << std::endl;
    DeasynthProcessor p;
    check(p.surge != nullptr, "engine starts (" + p.fatalErrorMessage + ")");
    if (!p.surge)
        return;
    p.prepareToPlay(48000.0, 512);

    auto silent = render(p, 8);
    check(silent.peak == 0.f, "silent with no notes held");

    auto on = noteOn(48);
    auto playing = render(p, 40, &on);
    check(playing.finite, "output is finite");
    check(playing.rms > 0.01, "a held note makes sound (rms " + std::to_string(playing.rms) + ")");
    check(playing.peak < 2.f, "level is sane (peak " + std::to_string(playing.peak) + ")");

    auto off = noteOff(48);
    render(p, 400, &off); // let the release and tail finish
    auto after = render(p, 8);
    check(after.peak < 1e-4f, "returns to silence after release");

    // odd host block sizes must not disturb the 32 sample engine blocks
    auto on2 = noteOn(60);
    auto odd = render(p, 60, &on2, 37);
    check(odd.finite && odd.rms > 0.01, "works with a 37 sample host buffer");
    auto off2 = noteOff(60);
    render(p, 600, &off2, 37);

    p.processBlockBypassed(*std::make_unique<juce::AudioBuffer<float>>(2, 512),
                           *std::make_unique<juce::MidiBuffer>());
    check(true, "bypass does not crash");
}

void testOscillators()
{
    std::cout << "Oscillator algorithms" << std::endl;
    DeasynthProcessor p;
    if (!p.surge)
        return;
    p.prepareToPlay(48000.0, 512);
    auto &type = p.surge->storage.getPatch().scene[0].osc[0].type;

    for (int t = 0; t < n_osc_types; ++t)
    {
        if (t == ot_audioinput)
            continue; // Deasynth has no audio input
        p.surge->setParameter01(p.surge->idForParameter(&type),
                                Parameter::intScaledToFloat(t, type.val_max.i, type.val_min.i), false,
                                true);
        render(p, 2); // the swap happens on the audio thread
        std::cout << "        " << osc_type_names[t] << "..." << std::flush;
        auto on = noteOn(52);
        auto r = render(p, 30, &on);
        auto off = noteOff(52);
        render(p, 120, &off);
        std::cout << "\r";
        check(type.val.i == t && r.finite && r.peak < 4.f,
              std::string(osc_type_names[t]) + " plays (rms " + std::to_string(r.rms) + ", peak " +
                  std::to_string(r.peak) + ", type " + std::to_string(type.val.i) +
                  (r.finite ? ")" : ", NOT FINITE)"));
    }
}

/*
 * Every source in the Modulate strip, routed to the filter cutoff, has to change the sound.
 * The filter starts nearly closed, so a source that works opens (or closes) it audibly; the
 * check is that the level moves by more than run-to-run variation could explain.
 */
void testModulationSources()
{
    std::cout << "Modulation sources" << std::endl;

    struct Source
    {
        int ms;
        const char *name;
    };
    static const Source sources[] = {
        {ms_velocity, "Velocity"},      {ms_keytrack, "Key tracking"},
        {ms_aftertouch, "Channel aftertouch"}, {ms_polyaftertouch, "Poly aftertouch"},
        {ms_modwheel, "Mod wheel"},     {ms_pitchbend, "Pitch bend"},
        {ms_filtereg, "Filter envelope"}, {ms_ampeg, "Amp envelope"},
        {ms_lfo1, "LFO 1"},   {ms_lfo2, "LFO 2"},   {ms_lfo3, "LFO 3"},
        {ms_lfo4, "LFO 4"},   {ms_lfo5, "LFO 5"},   {ms_lfo6, "LFO 6"},
        {ms_slfo1, "S-LFO 1"}, {ms_slfo2, "S-LFO 2"}, {ms_slfo3, "S-LFO 3"},
        {ms_slfo4, "S-LFO 4"}, {ms_slfo5, "S-LFO 5"}, {ms_slfo6, "S-LFO 6"},
        {ms_ctrl1, "Macro 1"}, {ms_ctrl2, "Macro 2"}, {ms_ctrl3, "Macro 3"},
        {ms_ctrl4, "Macro 4"}, {ms_ctrl5, "Macro 5"}, {ms_ctrl6, "Macro 6"},
        {ms_ctrl7, "Macro 7"}, {ms_ctrl8, "Macro 8"},
        {ms_random_bipolar, "Random"},  {ms_alternate_bipolar, "Alternate"},
    };

    // one fixed performance: three notes, hard, with every controller pushed
    auto perform = [](DeasynthProcessor &p) {
        juce::MidiBuffer m;
        for (int n : {72, 76, 79})
            m.addEvent(juce::MidiMessage::noteOn(1, n, (juce::uint8)127), 0);
        m.addEvent(juce::MidiMessage::controllerEvent(1, 1, 127), 1);
        m.addEvent(juce::MidiMessage::pitchWheel(1, 16383), 1);
        m.addEvent(juce::MidiMessage::channelPressureChange(1, 127), 1);
        for (int n : {72, 76, 79})
            m.addEvent(juce::MidiMessage::aftertouchChange(1, n, 127), 1);
        return render(p, 50, &m).rms;
    };

    auto setup = [](DeasynthProcessor &p) {
        p.prepareToPlay(48000.0, 512);
        *p.vintageParam = 0.f;
        auto &s = *p.surge;
        auto &sc = s.storage.getPatch().scene[0];
        auto setF = [&](Parameter &par, float v) {
            s.setParameter01(s.idForParameter(&par), par.value_to_normalized(v));
        };
        setF(sc.drift, 0.f);
        setF(sc.filterunit[0].cutoff, -36.f); // about 55 Hz: nearly shut for these notes
        setF(sc.filterunit[0].envmod, 0.f);
        setF(sc.filterunit[0].keytrack, 0.f);
        setF(sc.filterunit[0].resonance, 0.f);
        setF(sc.pbrange_up, 0.f); // so pitch bend only acts through the routing under test
        for (int m = 0; m < n_customcontrollers; ++m)
            s.setMacroParameter01(m, 1.f);
        render(p, 4);
    };

    double baseline[3];
    for (auto &b : baseline)
    {
        DeasynthProcessor p;
        if (!p.surge)
            return;
        setup(p);
        b = perform(p);
    }
    const double base = (baseline[0] + baseline[1] + baseline[2]) / 3.0;
    const double spread = std::max({std::abs(baseline[0] - base), std::abs(baseline[1] - base),
                                    std::abs(baseline[2] - base)});
    // with the filter shut the baseline is close to silence, so judge it in absolute terms
    check(base < 0.005 && spread < 0.002,
          "baseline, filter shut, is quiet and repeatable (rms " + std::to_string(base) + ", spread " +
              std::to_string(spread) + ")");

    for (const auto &src : sources)
    {
        DeasynthProcessor p;
        setup(p);
        auto &s = *p.surge;
        auto &cutoff = s.storage.getPatch().scene[0].filterunit[0].cutoff;
        const auto ms = (modsources)src.ms;

        const bool valid = s.isValidModulation(cutoff.id, ms);
        s.setModDepth01(cutoff.id, ms, 0, 0, 0.5f);
        const bool active = s.isActiveModulation(cutoff.id, ms, 0, 0);
        const bool depthOk = std::abs(s.getModDepth01(cutoff.id, ms, 0, 0) - 0.5f) < 1e-3f;
        s.updateUsedState();
        const bool shownUsed = s.modsourceused[src.ms];

        // Random and Alternate take a new value on each note, and either can come up near
        // zero or negative on a given note, so judge them over a few performances
        double biggest = 0.0;
        const int tries = (src.ms == ms_random_bipolar || src.ms == ms_alternate_bipolar) ? 4 : 1;
        for (int t = 0; t < tries; ++t)
        {
            const double r = perform(p);
            biggest = std::max(biggest, std::abs(r - base));
            juce::MidiBuffer off;
            off.addEvent(juce::MidiMessage::allNotesOff(1), 0);
            for (int n : {72, 76, 79})
                off.addEvent(juce::MidiMessage::noteOff(1, n), 0);
            render(p, 300, &off);
        }
        const bool audible = biggest > std::max(0.004, spread * 4.0);

        s.clearModulation(cutoff.id, ms, 0, 0, false);
        const bool cleared = !s.isActiveModulation(cutoff.id, ms, 0, 0);

        check(valid && active && depthOk && shownUsed && audible && cleared,
              std::string(src.name) + " modulates (level change " + std::to_string(biggest / base) +
                  "x baseline" + (valid ? "" : ", NOT VALID") + (active ? "" : ", NOT ACTIVE") +
                  (depthOk ? "" : ", DEPTH WRONG") + (shownUsed ? "" : ", NOT MARKED USED") +
                  (audible ? "" : ", NO AUDIBLE EFFECT") + (cleared ? "" : ", NOT CLEARED") + ")");
    }

    // what Random and Alternate actually put out, note by note
    {
        DeasynthProcessor p;
        setup(p);
        auto &scene = p.surge->storage.getPatch().scene[0];
        std::vector<float> alt, rnd;
        for (int i = 0; i < 6; ++i)
        {
            auto on = noteOn(60 + i);
            render(p, 1, &on);
            alt.push_back(scene.modsources[ms_alternate_bipolar]->get_output(0));
            rnd.push_back(scene.modsources[ms_random_bipolar]->get_output(0));
        }
        bool flips = true, inRange = true, varies = false;
        for (size_t i = 0; i < alt.size(); ++i)
        {
            flips = flips && std::abs(std::abs(alt[i]) - 1.f) < 1e-6f && (i == 0 || alt[i] == -alt[i - 1]);
            inRange = inRange && rnd[i] >= -1.f && rnd[i] <= 1.f;
            varies = varies || (i > 0 && rnd[i] != rnd[0]);
        }
        check(flips, "Alternate flips between -1 and +1 on each new note");
        check(inRange && varies, "Random gives each new note a different value within -1...+1");
    }
}

void testAnalogStage()
{
    std::cout << "Output stage (Warmth)" << std::endl;
    deasynth::AnalogStage stage;
    stage.setSampleRate(48000.0);

    // amount 0 must be bit transparent
    float L alignas(16)[BLOCK_SIZE], R alignas(16)[BLOCK_SIZE], ref[BLOCK_SIZE];
    double phase = 0.0;
    auto fill = [&](float amp) {
        for (int i = 0; i < BLOCK_SIZE; ++i)
        {
            L[i] = R[i] = ref[i] = amp * (float)std::sin(phase);
            phase += 2.0 * M_PI * 220.0 / 48000.0;
        }
    };
    bool transparent = true;
    for (int b = 0; b < 20; ++b)
    {
        fill(0.7f);
        stage.process(L, R, 0.f);
        for (int i = 0; i < BLOCK_SIZE; ++i)
            transparent = transparent && L[i] == ref[i] && R[i] == ref[i];
    }
    check(transparent, "amount 0 passes audio through untouched");

    // switching the stage in and out during a note must not click: the largest step between
    // neighbouring samples should stay close to that of the sine itself
    {
        deasynth::AnalogStage sw;
        sw.setSampleRate(48000.0);
        const float sineStep = 0.7f * (float)(2.0 * M_PI * 220.0 / 48000.0);
        float prev = 0.f, worst = 0.f;
        bool back = true;
        phase = 0.0;
        for (int b = 0; b < 400; ++b)
        {
            fill(0.7f);
            const float amt = (b >= 100 && b < 200) ? 0.02f : 0.f; // on for a while, then off
            sw.process(L, R, amt);
            for (int i = 0; i < BLOCK_SIZE; ++i)
            {
                if (b > 0 || i > 0)
                    worst = std::max(worst, std::abs(L[i] - prev));
                prev = L[i];
                if (b >= 300)
                    back = back && L[i] == ref[i];
            }
        }
        check(worst < sineStep * 1.5f, "engaging and releasing does not click (largest step " +
                                           std::to_string(worst / sineStep) + "x the signal's own)");
        check(back, "transparent again once released");
    }

    // full amount: bounded, finite, adds harmonics, leaves no DC
    stage.reset();
    double sum = 0.0, sumSq = 0.0, fund = 0.0, fundQ = 0.0, refSq = 0.0;
    float peak = 0.f;
    bool finite = true;
    int n = 0;
    double ph2 = 0.0;
    phase = 0.0;
    for (int b = 0; b < 600; ++b)
    {
        fill(0.7f);
        stage.process(L, R, 1.f);
        for (int i = 0; i < BLOCK_SIZE; ++i, ph2 += 2.0 * M_PI * 220.0 / 48000.0)
        {
            if (b < 200)
                continue; // settle
            finite = finite && std::isfinite(L[i]);
            peak = std::max(peak, std::abs(L[i]));
            sum += L[i];
            sumSq += (double)L[i] * L[i];
            refSq += (double)ref[i] * ref[i];
            fund += L[i] * std::sin(ph2);
            fundQ += L[i] * std::cos(ph2);
            ++n;
        }
    }
    const double total = sumSq / n;
    const double fundPower = 2.0 * ((fund / n) * (fund / n) + (fundQ / n) * (fundQ / n));
    const double thd = std::sqrt(std::max(0.0, total - fundPower) / total);
    check(finite, "full amount stays finite");
    check(peak < 1.5f, "full amount stays bounded (peak " + std::to_string(peak) + ")");
    check(thd > 0.05, "full amount adds harmonics (THD " + std::to_string(thd) + ")");
    check(std::abs(sum / n) < 0.01, "no DC offset at the output (" + std::to_string(sum / n) + ")");
    const double gainDb = 10.0 * std::log10(total / (refSq / n));
    check(std::abs(gainDb) < 6.0, "loudness stays within 6 dB (" + std::to_string(gainDb) + " dB)");
}

void testVintage()
{
    std::cout << "Voice variance (Vintage)" << std::endl;
    DeasynthProcessor p;
    if (!p.surge)
        return;
    p.prepareToPlay(48000.0, 512);

    auto spread = [&](float amount) {
        *p.vintageParam = amount;
        juce::MidiBuffer m;
        for (int i = 0; i < 8; ++i)
            m.addEvent(juce::MidiMessage::noteOn(1, 48 + i * 2, (juce::uint8)100), 0);
        render(p, 2, &m);
        float maxPitch = 0.f, maxCut = 0.f;
        int count = 0;
        bool allSame = true;
        float first = 0.f;
        for (auto *v : p.surge->voices[0])
        {
            if (count == 0)
                first = v->state.vintagePitch;
            allSame = allSame && v->state.vintagePitch == first;
            maxPitch = std::max(maxPitch, std::abs(v->state.vintagePitch));
            maxCut = std::max(maxCut, std::abs(v->state.vintageCutoff));
            ++count;
        }
        juce::MidiBuffer offs;
        for (int i = 0; i < 8; ++i)
            offs.addEvent(juce::MidiMessage::noteOff(1, 48 + i * 2), 0);
        render(p, 600, &offs);
        return std::make_tuple(count, maxPitch, maxCut, allSame);
    };

    auto [n0, pitch0, cut0, same0] = spread(0.f);
    check(n0 == 8, "eight voices allocated");
    check(pitch0 == 0.f && cut0 == 0.f, "amount 0 leaves every voice exact");

    auto [n1, pitch1, cut1, same1] = spread(1.f);
    check(n1 == 8 && !same1, "full amount gives each voice its own offset");
    check(pitch1 > 0.f && pitch1 <= 0.12f, "pitch spread within +/- 12 cents (" + std::to_string(pitch1 * 100.f) + ")");
    check(cut1 > 0.f && cut1 <= 4.f, "cutoff spread within +/- 4 semitones (" + std::to_string(cut1) + ")");
}

void testState()
{
    std::cout << "Session state" << std::endl;
    juce::MemoryBlock saved;
    float cutoff = 0.f;
    {
        DeasynthProcessor a;
        if (!a.surge)
            return;
        a.prepareToPlay(48000.0, 512);
        auto &scene = a.surge->storage.getPatch().scene[0];
        a.surge->setParameter01(a.surge->idForParameter(&scene.filterunit[0].cutoff), 0.31f);
        cutoff = scene.filterunit[0].cutoff.val.f;
        a.surge->setMacroParameter01(2, 0.66f);
        *a.vintageParam = 0.77f;
        *a.warmthParam = 0.42f;
        a.editorState.page = 2;
        render(a, 4);
        a.getStateInformation(saved);
    }
    check(saved.getSize() > 64, "state is written");

    {
        DeasynthProcessor b;
        b.prepareToPlay(48000.0, 512);
        b.setStateInformation(saved.getData(), (int)saved.getSize());
        render(b, 4);
        auto &scene = b.surge->storage.getPatch().scene[0];
        check(std::abs(scene.filterunit[0].cutoff.val.f - cutoff) < 1e-4f, "engine parameters restored");
        check(std::abs(b.surge->getMacroParameterTarget01(2) - 0.66f) < 1e-3f, "macros restored");
        check(std::abs(b.vintageParam->get() - 0.77f) < 1e-5f, "Vintage restored");
        check(std::abs(b.warmthParam->get() - 0.42f) < 1e-5f, "Warmth restored");
        check(b.editorState.page == 2, "editor page restored");
        check(b.surge->storage.getPatch().scene[0].filterunit[0].type.val.i ==
                  sst::filters::FilterType::fut_vintageladder,
              "filter model restored");

        // a second save of the restored instance must restore identically
        juce::MemoryBlock again;
        b.getStateInformation(again);
        DeasynthProcessor c;
        c.prepareToPlay(48000.0, 512);
        c.setStateInformation(again.getData(), (int)again.getSize());
        render(c, 4);
        check(std::abs(c.surge->storage.getPatch().scene[0].filterunit[0].cutoff.val.f - cutoff) < 1e-4f,
              "state survives a second round trip");
    }

    {
        // damaged data must be rejected without disturbing the instance
        DeasynthProcessor d;
        d.prepareToPlay(48000.0, 512);
        *d.vintageParam = 0.5f;
        const float before = d.surge->storage.getPatch().scene[0].filterunit[0].cutoff.val.f;
        d.setStateInformation(saved.getData(), 40); // header says more than is there
        d.setStateInformation(saved.getData(), 3);
        d.setStateInformation(nullptr, 0);
        juce::MemoryBlock junk(4096, true);
        for (size_t i = 0; i < junk.getSize(); ++i)
            junk[i] = (char)(i * 131 + 7);
        d.setStateInformation(junk.getData(), (int)junk.getSize());
        auto on = noteOn(50);
        auto r = render(d, 20, &on);
        check(r.finite && r.rms > 0.005, "still plays after being handed damaged state");
        check(d.vintageParam->get() == 0.5f, "truncated state leaves settings alone");
        check(d.surge->storage.getPatch().scene[0].filterunit[0].cutoff.val.f == before,
              "truncated state leaves the patch alone");
    }
}

void testFavorites()
{
    std::cout << "Favorites" << std::endl;
    auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                   .getChildFile("deasynth-selftest-" + juce::String(juce::Random::getSystemRandom().nextInt(1 << 30)));
    dir.createDirectory();
    auto file = dir.getChildFile("Favorites.txt");

    Patch factory, third, outside;
    factory.path = fs::path("/somewhere/Deasynth/patches_factory/Basses/Sub 1.fxp");
    third.path = fs::path("/somewhere/Deasynth/patches_3rdparty/A.Liv/Keys/Bell.fxp");
    outside.path = fs::path("/elsewhere/Random.fxp");

    {
        deasynth::Favorites f;
        f.attach(file);
        check(f.count() == 0 && !f.contains(factory), "starts empty");
        check(f.keyFor(factory) == "factory/Basses/Sub 1.fxp", "factory patches are identified within their library");
        check(f.keyFor(third) == "3rdparty/A.Liv/Keys/Bell.fxp", "third party patches likewise");
        check(f.set(factory, true) && f.set(third, true), "patches can be marked");
        check(f.contains(factory) && f.contains(third) && f.count() == 2, "marked patches are favorites");
        check(!f.set(outside, true) && !f.contains(outside), "a file outside every library is refused");
    }
    {
        deasynth::Favorites again;
        again.attach(file);
        check(again.contains(factory) && again.contains(third), "favorites survive a restart");

        // the same patch reached through a different install location is the same favorite
        Patch moved;
        moved.path = fs::path("/Library/Application Support/Deasynth/patches_factory/Basses/Sub 1.fxp");
        check(again.contains(moved), "favorites follow the library if it moves");

        // two instances: a change by one must not be undone by the other
        deasynth::Favorites other;
        other.attach(file);
        check(other.toggle(third) && !other.contains(third), "a patch can be unmarked");
        Patch extra;
        extra.path = fs::path("/x/patches_factory/Leads/Bright.fxp");
        check(again.set(extra, true), "a second instance can mark another");
        other.reload();
        check(other.contains(extra) && other.contains(factory) && !other.contains(third),
              "instances do not overwrite each other's changes");
    }
    dir.deleteRecursively();
}

void testParameters()
{
    std::cout << "Host parameters" << std::endl;
    DeasynthProcessor p;
    if (!p.surge)
        return;
    auto &params = p.getParameters();
    check(params.size() > 500, "engine parameters are exposed (" + std::to_string(params.size()) + ")");

    bool namesOk = true, rangeOk = true;
    std::set<juce::String> ids;
    bool unique = true;
    for (auto *ap : params)
    {
        namesOk = namesOk && ap->getName(64).isNotEmpty();
        const float v = ap->getValue();
        rangeOk = rangeOk && v >= 0.f && v <= 1.f && std::isfinite(v);
        if (auto *wid = dynamic_cast<juce::AudioProcessorParameterWithID *>(ap))
            unique = unique && ids.insert(wid->paramID).second;
    }
    check(namesOk, "every parameter has a name");
    check(rangeOk, "every parameter reports a value in 0...1");
    check(unique, "parameter identifiers are unique");

    auto &cut = p.surge->storage.getPatch().scene[0].filterunit[0].cutoff;
    auto *hp = p.hostParamFor(&cut);
    check(hp != nullptr, "engine parameters map to host parameters");
    if (hp)
    {
        hp->setValueNotifyingHost(0.8f);
        check(std::abs(cut.get_value_f01() - 0.8f) < 1e-3f, "host automation reaches the engine");
        check(hp->getText(0.8f, 32).isNotEmpty(), "values have display text");
    }
}

void testEditor(const juce::String &snapshotDir)
{
    std::cout << "Editor" << std::endl;
    DeasynthProcessor p;
    if (!p.surge)
        return;
    p.prepareToPlay(48000.0, 512);

    std::unique_ptr<juce::AudioProcessorEditor> base(p.createEditor());
    auto *ed = dynamic_cast<DeasynthEditor *>(base.get());
    check(ed != nullptr, "editor is created");
    if (!ed)
        return;
    check(ed->getWidth() == DeasynthEditor::baseWidth && ed->getHeight() == DeasynthEditor::baseHeight,
          "opens at its design size");

    if (snapshotDir.isNotEmpty())
    {
        // Put the instrument in a state worth looking at: a chord sounding, a few modulation
        // routings with one source armed, and a couple of effects loaded.
        auto &s = *p.surge;
        auto &sc = s.storage.getPatch().scene[0];
        auto choose = [&](Parameter &par, int v) {
            s.setParameter01(s.idForParameter(&par),
                             Parameter::intScaledToFloat(v, par.val_max.i, par.val_min.i), false, true);
        };
        choose(sc.mute_o2, 0);
        s.setParameter01(s.idForParameter(&sc.osc[1].pitch), sc.osc[1].pitch.value_to_normalized(0.08f));
        s.setParameter01(s.idForParameter(&sc.osc[1].p[0]), sc.osc[1].p[0].value_to_normalized(-1.f));
        s.setParameter01(s.idForParameter(&sc.filterunit[0].cutoff), sc.filterunit[0].cutoff.value_to_normalized(2.f));
        s.setParameter01(s.idForParameter(&sc.filterunit[0].resonance), 0.45f);
        s.setModDepth01(sc.filterunit[0].cutoff.id, ms_lfo1, 0, 0, 0.14f);
        s.setModDepth01(sc.osc[1].p[1].id, ms_lfo1, 0, 0, 0.25f);
        s.setModDepth01(sc.filterunit[0].cutoff.id, ms_ctrl1, 0, 0, 0.35f);
        s.setModDepth01(sc.filterunit[0].resonance.id, ms_velocity, 0, 0, 0.15f);
        s.setModDepth01(sc.level_o2.id, ms_modwheel, 0, 0, -0.3f);
        strncpy(s.storage.getPatch().CustomControllerLabel[0], "Brightness", CUSTOM_CONTROLLER_LABEL_SIZE - 1);
        s.setParameter01(s.idForParameter(&sc.lfo[0].rate), sc.lfo[0].rate.value_to_normalized(1.2f));
        choose(s.storage.getPatch().fx[fxslot_ains1].type, fxt_chorus4);
        choose(s.storage.getPatch().fx[fxslot_ains2].type, fxt_reverb2);
        s.storage.getPatch().name = "Evening Ladder";
        p.editorState.fxSlot = fxslot_ains2;
        *p.warmthParam = 0.3f;

        juce::MidiBuffer chord;
        for (int n : {45, 57, 60, 64})
            chord.addEvent(juce::MidiMessage::noteOn(1, n, (juce::uint8)96), 0);
        render(p, 60, &chord);

        base.reset();
        base.reset(p.createEditor());
        ed = dynamic_cast<DeasynthEditor *>(base.get());
        ed->context().armedSource = ms_lfo1;
    }

    static const char *names[] = {"synth", "modulation", "effects"};
    for (int pg = 0; pg < DeasynthEditor::numPages; ++pg)
    {
        ed->setPage(pg);
        if (snapshotDir.isNotEmpty())
        {
            render(p, 3); // fresh audio for the scope and meters
            ed->setPage(pg);
        }
        auto img = ed->createComponentSnapshot(ed->getLocalBounds(), true, 1.f);
        check(img.isValid(), std::string("page paints: ") + names[pg]);
        if (snapshotDir.isNotEmpty())
        {
            juce::File out = juce::File(snapshotDir).getChildFile(juce::String("deasynth-") + names[pg] + ".png");
            out.deleteFile();
            juce::FileOutputStream os(out);
            juce::PNGImageFormat().writeImageToStream(img, os);
        }
    }

    {
        // wheels moved on a MIDI controller must reach the engine's "last seen" values,
        // which is what the on-screen wheels follow
        juce::MidiBuffer wheels;
        wheels.addEvent(juce::MidiMessage::controllerEvent(1, 1, 100), 0);
        wheels.addEvent(juce::MidiMessage::pitchWheel(1, 8192 + 4096), 0);
        render(p, 1, &wheels);
        check(p.surge->modwheelCC.load() == 100, "mod wheel from MIDI is reported for the display");
        check(p.surge->pitchbendMIDIVal.load() == 4096, "pitch bend from MIDI is reported for the display");
        juce::MidiBuffer centre;
        centre.addEvent(juce::MidiMessage::controllerEvent(1, 1, 0), 0);
        centre.addEvent(juce::MidiMessage::pitchWheel(1, 8192), 0);
        render(p, 1, &centre);
    }

    ed->setScene(1);
    ed->setPage(0);
    check(ed->context().scene == 1, "switches to layer B");
    ed->showBrowser(true);
    ed->showSaveDialog();
    ed->showAbout(true);
    ed->showAbout(false);
    ed->initPatch();
    ed->setZoom(1.5f);
    check(ed->getWidth() == 1920, "resizes to 150%");
    auto img = ed->createComponentSnapshot(ed->getLocalBounds(), true, 1.f);
    check(img.isValid(), "paints at 150%");
    base.reset();
    check(true, "editor closes cleanly");
}
} // namespace

int writeDeasynthPatches(const juce::String &directory); // presetgen.cpp
int renderDeasynthDemo(const juce::String &directory, double from, double to); // demovideo.cpp

#if JUCE_LINUX
#include <X11/Xlib.h>
/*
 * The demo renderer needs a real window, and on a bare X server (xvfb with no window manager
 * running) JUCE cannot create one: it looks up the standard window-manager atoms without
 * creating them, finds nothing, and the server rejects the window. A window manager would
 * have created them; here we do. The connection is left open so the server keeps them.
 */
static void createWindowManagerAtoms()
{
    if (auto *display = XOpenDisplay(nullptr))
    {
        for (auto *name :
             {"WM_PROTOCOLS", "WM_TAKE_FOCUS", "WM_DELETE_WINDOW", "WM_CHANGE_STATE", "WM_STATE",
              "_NET_WM_PID", "_NET_WM_PING", "_NET_WM_NAME", "_NET_WM_ICON", "_NET_WM_USER_TIME",
              "_NET_WM_WINDOW_TYPE", "_NET_WM_WINDOW_TYPE_NORMAL", "_NET_WM_WINDOW_TYPE_COMBO",
              "_NET_WM_STATE", "_NET_WM_STATE_HIDDEN", "_NET_WM_STATE_FULLSCREEN",
              "_NET_WM_STATE_ABOVE", "_NET_WM_MOVERESIZE", "_NET_WM_ALLOWED_ACTIONS",
              "_NET_FRAME_EXTENTS", "_NET_WORKAREA", "_NET_ACTIVE_WINDOW", "_NET_WM_WINDOW_OPACITY",
              "_MOTIF_WM_HINTS", "_WIN_HINTS", "KWM_WIN_DECORATION",
              "_KDE_NET_WM_WINDOW_TYPE_OVERRIDE", "UTF8_STRING"})
            XInternAtom(display, name, False);
        XSync(display, False);
    }
}
#endif

int main(int argc, char **argv)
{
#if JUCE_LINUX
    if (argc > 2 && juce::String(argv[1]) == "--render-demo")
        createWindowManagerAtoms();
#endif
    juce::ScopedJuceInitialiser_GUI gui;

    if (argc > 2 && juce::String(argv[1]) == "--write-patches")
        return writeDeasynthPatches(argv[2]);
    if (argc > 2 && juce::String(argv[1]) == "--render-demo")
        return renderDeasynthDemo(argv[2], argc > 3 ? atof(argv[3]) : 0.0, argc > 4 ? atof(argv[4]) : 1e9);

    testAudio();
    testOscillators();
    testModulationSources();
    testAnalogStage();
    testVintage();
    testState();
    testFavorites();
    testParameters();
    testEditor(argc > 1 ? juce::String(argv[1]) : juce::String());

    std::cout << (failures == 0 ? "\nAll checks passed." : "\nFAILED: " + std::to_string(failures) + " check(s).")
              << std::endl;
    return failures == 0 ? 0 : 1;
}
