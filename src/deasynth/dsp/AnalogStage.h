/*
 * Deasynth - an analog-voiced hybrid synthesizer built on the Surge XT engine.
 *
 * Deasynth is released under the GNU General Public Licence v3 or later (GPL-3.0-or-later).
 * The license is found in the "LICENSE" file in the root of this repository, or at
 * https://www.gnu.org/licenses/gpl-3.0.en.html
 */

#ifndef DEASYNTH_SRC_DSP_ANALOGSTAGE_H
#define DEASYNTH_SRC_DSP_ANALOGSTAGE_H

#include <algorithm>
#include <cmath>
#include "globals.h"
#include "sst/filters/HalfRateFilter.h"

namespace deasynth
{
/*
 * The output "Warmth" stage. This models the behaviour of an overdriven discrete output
 * amplifier of the kind found at the end of a classic monosynth signal path: a gently
 * asymmetric soft saturator (so it produces even as well as odd harmonics), a low shelf lift
 * ahead of the nonlinearity which is removed again after it (so lows drive the stage harder
 * and the highs stay clean), and a DC blocker.
 *
 * The nonlinearity runs at 2x the engine rate between a pair of half-band filters to keep
 * the harmonics it generates from aliasing. At amount == 0 the stage is bypassed entirely
 * and the engine output passes through bit-exact.
 *
 * It processes exactly one engine block (BLOCK_SIZE stereo samples) at a time.
 */
struct AnalogStage
{
    AnalogStage() : up(6, true), down(6, true) { reset(); }

    void setSampleRate(double sr)
    {
        sampleRate = sr;
        // one pole at ~180 Hz for the pre-emphasis shelf, ~8 Hz for the DC blocker
        shelfCoef = 1.f - std::exp(-2.0 * M_PI * 180.0 / (sr * 2.0));
        dcCoef = 1.f - std::exp(-2.0 * M_PI * 8.0 / sr);
        reset();
    }

    void reset()
    {
        up.reset();
        down.reset();
        for (int c = 0; c < 2; ++c)
        {
            shelfPre[c] = 0.f;
            shelfPost[c] = 0.f;
            dc[c] = 0.f;
        }
        smoothed = 0.f;
        active = false;
    }

    static inline float shape(float x)
    {
        // Pade approximant of tanh, accurate to within 2% for |x| < 3 and hard limited outside
        x = std::clamp(x, -3.f, 3.f);
        const float x2 = x * x;
        return x * (27.f + x2) / (27.f + 9.f * x2);
    }

    void process(float *L, float *R, float amount)
    {
        if (amount <= 1e-4f && smoothed <= 1e-4f)
        {
            if (active)
            {
                reset();
            }
            return;
        }
        active = true;

        float osL alignas(16)[BLOCK_SIZE_OS], osR alignas(16)[BLOCK_SIZE_OS];
        // upsample by zero stuffing (with gain compensation) then half-band filter
        for (int i = 0; i < BLOCK_SIZE; ++i)
        {
            osL[2 * i] = 2.f * L[i];
            osL[2 * i + 1] = 0.f;
            osR[2 * i] = 2.f * R[i];
            osR[2 * i + 1] = 0.f;
        }
        up.process_block(osL, osR, BLOCK_SIZE_OS);

        const float target = std::clamp(amount, 0.f, 1.f);
        const float step = (target - smoothed) / BLOCK_SIZE_OS;
        float *ch[2] = {osL, osR};

        for (int i = 0; i < BLOCK_SIZE_OS; ++i)
        {
            smoothed += step;
            const float a = smoothed;
            const float bias = 0.12f * a;
            const float biasOut = shape(bias);
            // 0 dB to +18 dB into the shaper. The output is trimmed so that a signal at the
            // reference level (-8 dBFS) comes out as loud as it went in: quieter material
            // is lifted and peaks are held back, which is the thickening this stage is for.
            const float drive = std::pow(10.f, a * 18.f / 20.f);
            constexpr float ref = 0.4f;
            const float makeup = ref / (shape(ref * drive + bias) - biasOut);
            const float shelfAmt = 0.9f * a;

            for (int c = 0; c < 2; ++c)
            {
                float x = ch[c][i];
                shelfPre[c] += shelfCoef * (x - shelfPre[c]);
                x += shelfAmt * shelfPre[c];

                float y = (shape(x * drive + bias) - biasOut) * makeup;

                shelfPost[c] += shelfCoef * (y - shelfPost[c]);
                y -= (shelfAmt / (1.f + shelfAmt)) * shelfPost[c];

                // crossfade over the first few percent of travel so engaging it is seamless
                const float wet = std::min(1.f, a * 12.f);
                ch[c][i] = ch[c][i] + wet * (y - ch[c][i]);
            }
        }

        down.process_block_D2(osL, osR, BLOCK_SIZE_OS);

        for (int i = 0; i < BLOCK_SIZE; ++i)
        {
            dc[0] += dcCoef * (osL[i] - dc[0]);
            dc[1] += dcCoef * (osR[i] - dc[1]);
            L[i] = osL[i] - dc[0];
            R[i] = osR[i] - dc[1];
        }
    }

    sst::filters::HalfRate::HalfRateFilter up, down;
    double sampleRate{48000.0};
    float shelfCoef{0.01f}, dcCoef{0.001f};
    float shelfPre[2], shelfPost[2], dc[2];
    float smoothed{0.f};
    bool active{false};
};
} // namespace deasynth

#endif
