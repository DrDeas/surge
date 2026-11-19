/*
 * Deasynth - an analog-voiced hybrid synthesizer built on the Surge XT engine.
 *
 * Deasynth is released under the GNU General Public Licence v3 or later (GPL-3.0-or-later).
 * The license is found in the "LICENSE" file in the root of this repository, or at
 * https://www.gnu.org/licenses/gpl-3.0.en.html
 */

/*
 * Writes Deasynth's own starter patches (resources/deasynth/patches). Each one is a recipe
 * applied on top of the init patch, so the bank can be regenerated whenever the engine's
 * patch format moves on:
 *
 *   deasynth-selftest --write-patches resources/deasynth/patches
 *
 * Every patch is rendered after it is written and the run fails if one is silent or clips.
 */

#include "../DeasynthProcessor.h"
#include <iostream>

namespace
{
struct Gen
{
    DeasynthProcessor &proc;
    SurgeSynthesizer &s;

    explicit Gen(DeasynthProcessor &p) : proc(p), s(*p.surge) {}

    SurgePatch &patch() { return s.storage.getPatch(); }
    SurgeSceneStorage &sc() { return patch().scene[0]; }

    void begin()
    {
        s.audio_processing_active = false; // so the load below happens right here
        proc.loadInitPatch();
    }

    /** Set in the parameter's own units (semitones, dB, log2 seconds, 0...1). */
    void set(Parameter &p, float v)
    {
        s.setParameter01(s.idForParameter(&p), p.value_to_normalized(v));
    }
    void choose(Parameter &p, int v)
    {
        if (p.valtype == vt_bool)
            s.setParameter01(s.idForParameter(&p), v ? 1.f : 0.f);
        else
            s.setParameter01(s.idForParameter(&p),
                             Parameter::intScaledToFloat(v, p.val_max.i, p.val_min.i), false, true);
    }
    /** Depth is a fraction of the destination's full range, -1...1. */
    void mod(Parameter &dest, modsources src, float depth)
    {
        s.setModDepth01(dest.id, src, 0, 0, depth);
    }
    void macro(int i, const char *name, Parameter &dest, float depth, float value = 0.f)
    {
        auto &label = patch().CustomControllerLabel[i];
        memset(label, 0, CUSTOM_CONTROLLER_LABEL_SIZE);
        strncpy(label, name, CUSTOM_CONTROLLER_LABEL_SIZE - 1);
        mod(dest, (modsources)(ms_ctrl1 + i), depth);
        s.setMacroParameter01(i, value);
    }
    void fx(int slot, int type)
    {
        choose(patch().fx[slot].type, type);
        s.processAudioThreadOpsWhenAudioEngineUnavailable();
    }
    /** Set an effect parameter, found by the name the effect gives it, 0...1. */
    bool fxParam(int slot, const char *name, float v01)
    {
        for (auto &p : patch().fx[slot].p)
        {
            if (p.ctrltype != ct_none && std::string(p.get_name()) == name)
            {
                s.setParameter01(s.idForParameter(&p), v01);
                return true;
            }
        }
        std::cout << "    (no effect parameter called '" << name << "')" << std::endl;
        return false;
    }
    void osc(int i, int octave, float pitch, float shape, float level, bool on = true)
    {
        auto &o = sc().osc[i];
        choose(o.octave, octave);
        set(o.pitch, pitch);
        set(o.p[0], shape);
        Parameter *levels[3] = {&sc().level_o1, &sc().level_o2, &sc().level_o3};
        Parameter *mutes[3] = {&sc().mute_o1, &sc().mute_o2, &sc().mute_o3};
        set(*levels[i], level);
        choose(*mutes[i], on ? 0 : 1);
    }
    void filter(int type, int subtype, float cutoff, float reso, float envAmount, float keytrack)
    {
        auto &f = sc().filterunit[0];
        choose(f.type, type);
        choose(f.subtype, subtype);
        set(f.cutoff, cutoff);
        set(f.resonance, reso);
        set(f.envmod, envAmount);
        set(f.keytrack, keytrack);
    }
    void env(int which, float a, float d, float sus, float r)
    {
        auto &e = sc().adsr[which];
        set(e.a, a);
        set(e.d, d);
        set(e.s, sus);
        set(e.r, r);
    }

    bool finish(const juce::File &dir, const char *name, const char *comment, int note)
    {
        auto &p = patch();
        p.name = name;
        p.category = "Deasynth";
        p.author = "Deasynth";
        p.comment = comment;
        p.license = "CC0, free to use in any way";
        s.processAudioThreadOpsWhenAudioEngineUnavailable();

        auto file = dir.getChildFile(juce::String(name) + ".fxp");
        s.savePatchToPath(fs::path(file.getFullPathName().toStdString()), false);

        // audition: one and a half seconds held, then check level
        proc.prepareToPlay(48000.0, 512);
        juce::AudioBuffer<float> buf(2, 512);
        float peak = 0.f;
        double sum = 0.0;
        bool finite = true;
        for (int b = 0; b < 140; ++b)
        {
            buf.clear();
            juce::MidiBuffer midi;
            if (b == 0)
                midi.addEvent(juce::MidiMessage::noteOn(1, note, (juce::uint8)100), 0);
            proc.processBlock(buf, midi);
            for (int i = 0; i < 512; ++i)
            {
                const float v = buf.getSample(0, i);
                finite = finite && std::isfinite(v);
                peak = std::max(peak, std::abs(v));
                sum += (double)v * v;
            }
        }
        juce::MidiBuffer off;
        off.addEvent(juce::MidiMessage::allNotesOff(1), 0);
        off.addEvent(juce::MidiMessage::noteOff(1, note), 0);
        proc.processBlock(buf, off);
        const double rms = std::sqrt(sum / (140.0 * 512.0));
        const bool ok = finite && file.existsAsFile() && rms > 0.005 && peak < 1.0f;
        std::cout << (ok ? "  ok    " : "  FAIL  ") << name << "  (peak " << peak << ", rms " << rms
                  << ")" << std::endl;
        return ok;
    }
};
} // namespace

int writeDeasynthPatches(const juce::String &directory)
{
    using FT = sst::filters::FilterType;
    juce::File dir = juce::File::getCurrentWorkingDirectory().getChildFile(directory);
    dir.createDirectory();

    DeasynthProcessor proc;
    if (!proc.surge)
    {
        std::cout << "engine failed to start: " << proc.fatalErrorMessage << std::endl;
        return 1;
    }
    *proc.vintageParam = 0.f;
    *proc.warmthParam = 0.f;
    Gen g(proc);
    int failures = 0;
    std::cout << "Writing patches to " << dir.getFullPathName() << std::endl;

    // ---------------------------------------------------------------- Ladder Bass
    g.begin();
    g.choose(g.sc().polymode, pm_mono_fp);
    g.osc(0, -1, 0.f, 0.f, 1.f);
    g.set(g.sc().osc[0].p[3], 0.25f); // sub mix
    g.osc(1, -1, 0.f, -1.f, 0.7f);    // pulse an octave down with the saw
    g.set(g.sc().osc[1].p[1], 0.42f);
    g.filter(FT::fut_vintageladder, 1, -20.f, 0.38f, 44.f, 0.6f);
    g.env(adsr_filteg, -8.f, -2.1f, 0.12f, -3.2f);
    g.env(adsr_ampeg, -8.f, -1.f, 0.9f, -3.6f);
    g.set(g.sc().level_pfg, 5.f);
    g.set(g.sc().portamento, -5.2f);
    g.set(g.sc().drift, 0.1f);
    g.macro(0, "Cutoff", g.sc().filterunit[0].cutoff, 0.45f);
    g.macro(1, "Resonance", g.sc().filterunit[0].resonance, 0.5f);
    g.macro(2, "Punch", g.sc().filterunit[0].envmod, 0.25f);
    failures += !g.finish(dir, "Ladder Bass",
                          "Saw and pulse into the transistor ladder with the mixer pushed. Play "
                          "legato for glide. Macro 1 opens the filter.",
                          36);

    // ---------------------------------------------------------------- Sync Lead
    g.begin();
    g.choose(g.sc().polymode, pm_mono_fp);
    g.osc(0, 0, 0.f, 0.f, 1.f);
    g.set(g.sc().osc[0].p[4], 12.f); // hard sync, an octave up
    g.mod(g.sc().osc[0].p[4], ms_filtereg, 0.3f);
    g.filter(FT::fut_vintageladder, 1, 16.f, 0.22f, 12.f, 0.7f);
    g.choose(g.sc().filterblock_configuration, fc_serial2);
    g.set(g.sc().feedback, 0.18f);
    g.env(adsr_filteg, -6.f, -0.6f, 0.25f, -2.f);
    g.env(adsr_ampeg, -7.f, -1.f, 0.95f, -2.8f);
    g.set(g.sc().portamento, -4.6f);
    g.choose(g.sc().lfo[0].shape, lt_sine);
    g.set(g.sc().lfo[0].rate, 2.5f);   // about 5.6 Hz
    g.set(g.sc().lfo[0].delay, -1.2f); // vibrato arrives late
    g.mod(g.sc().pitch, ms_lfo1, 0.0025f);
    g.mod(g.sc().pitch, ms_modwheel, 0.f);
    g.fx(fxslot_ains1, fxt_delay);
    g.fxParam(fxslot_ains1, "Mix", 0.22f);
    g.macro(0, "Cutoff", g.sc().filterunit[0].cutoff, 0.4f);
    g.macro(1, "Sync", g.sc().osc[0].p[4], 0.5f);
    g.macro(2, "Feedback", g.sc().feedback, 0.3f);
    failures += !g.finish(dir, "Sync Lead",
                          "Hard-synced saw swept by the filter envelope, with the filter fed back "
                          "into itself. Macro 2 moves the sync, Macro 3 the feedback.",
                          60);

    // ---------------------------------------------------------------- Vintage Poly
    g.begin();
    g.osc(0, 0, 0.f, 0.f, 0.85f);
    g.osc(1, 0, 0.09f, 0.f, 0.85f);
    g.set(g.sc().drift, 0.32f);
    g.filter(FT::fut_vintageladder, 1, 4.f, 0.2f, 26.f, 0.5f);
    g.env(adsr_filteg, -4.2f, 0.f, 0.4f, -0.6f);
    g.env(adsr_ampeg, -6.f, 0.f, 0.8f, -1.f);
    g.mod(g.sc().filterunit[0].cutoff, ms_velocity, 0.1f);
    g.fx(fxslot_ains1, fxt_chorus4);
    g.fxParam(fxslot_ains1, "Mix", 0.32f);
    g.macro(0, "Cutoff", g.sc().filterunit[0].cutoff, 0.4f);
    g.macro(1, "Resonance", g.sc().filterunit[0].resonance, 0.5f);
    g.macro(2, "Detune", g.sc().osc[1].pitch, 0.03f);
    failures += !g.finish(dir, "Vintage Poly",
                          "Two drifting saws through the ladder. Turn Vintage up to hear each "
                          "voice go its own way.",
                          55);

    // ---------------------------------------------------------------- Slow Pad
    g.begin();
    g.osc(0, 0, 0.f, 0.f, 0.8f);
    g.choose(g.sc().osc[0].p[6], 3); // three unison voices
    g.set(g.sc().osc[0].p[5], 0.18f);
    g.osc(1, -1, 0.f, -1.f, 0.55f);
    g.set(g.sc().osc[1].p[1], 0.3f);
    g.filter(FT::fut_obxd_4pole, 0, -4.f, 0.15f, 20.f, 0.4f);
    g.env(adsr_filteg, 0.2f, 1.f, 0.6f, 1.f);
    g.env(adsr_ampeg, -1.f, 0.f, 0.9f, 0.7f);
    g.choose(g.sc().lfo[n_lfos_voice].shape, lt_sine);
    g.set(g.sc().lfo[n_lfos_voice].rate, -2.2f); // about 0.2 Hz
    g.mod(g.sc().filterunit[0].cutoff, ms_slfo1, 0.06f);
    g.mod(g.sc().osc[1].p[1], ms_slfo1, 0.2f);
    g.fx(fxslot_ains1, fxt_chorus4);
    g.fxParam(fxslot_ains1, "Mix", 0.35f);
    g.fx(fxslot_ains2, fxt_reverb2);
    g.fxParam(fxslot_ains2, "Mix", 0.38f);
    g.macro(0, "Cutoff", g.sc().filterunit[0].cutoff, 0.4f);
    g.macro(1, "Motion", g.sc().lfo[n_lfos_voice].magnitude, 0.f, 0.f);
    g.mod(g.sc().filterunit[0].cutoff, ms_ctrl2, 0.f);
    failures += !g.finish(dir, "Slow Pad",
                          "Unison saws and a slowly breathing pulse through a four pole lowpass, "
                          "into chorus and reverb.",
                          52);

    // ---------------------------------------------------------------- Glass Pluck
    g.begin();
    g.osc(0, 0, 0.f, -1.f, 0.9f);
    g.set(g.sc().osc[0].p[1], 0.36f);
    g.osc(1, 1, 0.f, 0.f, 0.45f);
    g.filter(FT::fut_vintageladder, 1, -8.f, 0.3f, 50.f, 0.6f);
    g.env(adsr_filteg, -8.f, -2.5f, 0.f, -2.5f);
    g.env(adsr_ampeg, -8.f, -0.4f, 0.f, -1.2f);
    g.mod(g.sc().filterunit[0].cutoff, ms_velocity, 0.16f);
    g.fx(fxslot_ains1, fxt_delay);
    g.fxParam(fxslot_ains1, "Mix", 0.24f);
    g.macro(0, "Cutoff", g.sc().filterunit[0].cutoff, 0.4f);
    g.macro(1, "Decay", g.sc().adsr[adsr_filteg].d, 0.3f);
    failures += !g.finish(dir, "Glass Pluck",
                          "A snappy filter envelope on a narrow pulse. Velocity opens it up.", 64);

    // ---------------------------------------------------------------- Brass Section
    g.begin();
    g.osc(0, 0, 0.f, 0.f, 0.9f);
    g.osc(1, 0, -0.07f, 0.f, 0.8f);
    g.set(g.sc().drift, 0.25f);
    g.filter(FT::fut_vintageladder, 1, -10.f, 0.1f, 36.f, 0.5f);
    g.env(adsr_filteg, -4.3f, -1.4f, 0.55f, -2.f);
    g.env(adsr_ampeg, -5.5f, -1.f, 0.9f, -2.5f);
    g.mod(g.sc().filterunit[0].cutoff, ms_aftertouch, 0.12f);
    g.mod(g.sc().filterunit[0].cutoff, ms_velocity, 0.08f);
    g.macro(0, "Cutoff", g.sc().filterunit[0].cutoff, 0.4f);
    g.macro(1, "Swell", g.sc().adsr[adsr_filteg].a, 0.3f);
    failures += !g.finish(dir, "Brass Section",
                          "The filter envelope's attack does the tonguing. Lean into aftertouch "
                          "to brighten held notes.",
                          58);

    // ---------------------------------------------------------------- PWM Strings
    g.begin();
    g.osc(0, 0, 0.f, -1.f, 0.85f);
    g.osc(1, 0, 0.06f, -1.f, 0.85f);
    g.choose(g.sc().lfo[n_lfos_voice].shape, lt_tri);
    g.set(g.sc().lfo[n_lfos_voice].rate, -0.8f);
    g.choose(g.sc().lfo[n_lfos_voice + 1].shape, lt_tri);
    g.set(g.sc().lfo[n_lfos_voice + 1].rate, -1.3f);
    g.mod(g.sc().osc[0].p[1], ms_slfo1, 0.3f);
    g.mod(g.sc().osc[1].p[1], ms_slfo2, 0.3f);
    g.filter(FT::fut_lp24, 0, 14.f, 0.08f, 6.f, 0.5f);
    g.env(adsr_filteg, -1.5f, 0.f, 0.8f, 0.f);
    g.env(adsr_ampeg, -1.6f, 0.f, 0.95f, -0.3f);
    g.set(g.sc().lowcut, -24.f);
    g.fx(fxslot_ains1, fxt_chorus4);
    g.fxParam(fxslot_ains1, "Mix", 0.42f);
    g.macro(0, "Cutoff", g.sc().filterunit[0].cutoff, 0.4f);
    g.macro(1, "Attack", g.sc().adsr[adsr_ampeg].a, 0.35f);
    failures += !g.finish(dir, "PWM Strings",
                          "Two pulse oscillators with their widths swept at different rates.", 60);

    // ---------------------------------------------------------------- Diode Acid
    g.begin();
    g.choose(g.sc().polymode, pm_mono_st_fp);
    g.osc(0, -1, 0.f, 0.f, 1.f);
    g.filter(FT::fut_diode, 0, -24.f, 0.8f, 42.f, 0.3f);
    g.env(adsr_filteg, -8.f, -2.3f, 0.f, -2.3f);
    g.env(adsr_ampeg, -8.f, -0.5f, 0.6f, -3.5f);
    g.mod(g.sc().filterunit[0].envmod, ms_velocity, 0.2f);
    g.set(g.sc().portamento, -4.2f);
    g.choose(g.sc().wsunit.type, (int)sst::waveshapers::WaveshaperType::wst_asym);
    g.set(g.sc().wsunit.drive, 4.f);
    g.macro(0, "Cutoff", g.sc().filterunit[0].cutoff, 0.45f);
    g.macro(1, "Resonance", g.sc().filterunit[0].resonance, 0.2f);
    g.macro(2, "Env", g.sc().filterunit[0].envmod, 0.3f);
    failures += !g.finish(dir, "Diode Acid",
                          "A diode ladder close to self-oscillation. Overlap notes to slide, hit "
                          "harder to accent.",
                          36);

    std::cout << (failures == 0 ? "All patches written." : "Some patches failed.") << std::endl;
    return failures == 0 ? 0 : 1;
}
