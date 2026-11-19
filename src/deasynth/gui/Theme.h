/*
 * Deasynth - an analog-voiced hybrid synthesizer built on the Surge XT engine.
 *
 * Deasynth is released under the GNU General Public Licence v3 or later (GPL-3.0-or-later).
 * The license is found in the "LICENSE" file in the root of this repository, or at
 * https://www.gnu.org/licenses/gpl-3.0.en.html
 */

#ifndef DEASYNTH_SRC_GUI_THEME_H
#define DEASYNTH_SRC_GUI_THEME_H

#include "juce_gui_basics/juce_gui_basics.h"

namespace deasynth
{
/*
 * One palette for the whole instrument. Each section of the signal path owns one accent
 * colour, and everything that belongs to that section (value arcs, display traces, the
 * modulation rings of its sources) is drawn in it, so colour always tells you where you are.
 */
namespace theme
{
// surfaces, darkest to lightest
const juce::Colour backdrop{0xff0f1114};
const juce::Colour chassis{0xff16191e};
const juce::Colour panel{0xff1d2127};
const juce::Colour panelHi{0xff252a32};
const juce::Colour well{0xff0c0e11}; // display backgrounds
const juce::Colour outline{0xff2f353f};
const juce::Colour track{0xff363d48}; // unlit knob arcs

// text
const juce::Colour text{0xffe9e6df};
const juce::Colour textDim{0xff9aa1ab};
const juce::Colour textFaint{0xff626a75};

// section accents
const juce::Colour osc{0xfff2a73b};    // amber
const juce::Colour mixer{0xffe9d28a};  // pale brass
const juce::Colour filter{0xffee6c4d}; // ember
const juce::Colour shaper{0xffd9588c}; // rose
const juce::Colour ampEnv{0xff7bd389}; // green
const juce::Colour filtEnv{0xff4fc4b0}; // teal
const juce::Colour lfo{0xff6cb4ee};    // sky
const juce::Colour sceneLfo{0xff9d8df1}; // violet
const juce::Colour macro{0xfff28f6b};  // coral
const juce::Colour fx{0xff5fd0df};     // cyan
const juce::Colour voice{0xffc9ccd3};  // steel
const juce::Colour danger{0xffe5484d};

juce::Typeface::Ptr regularTypeface();
juce::Typeface::Ptr boldTypeface();

inline juce::Font font(float size, bool bold = false)
{
    return juce::Font(juce::FontOptions(bold ? boldTypeface() : regularTypeface()))
        .withHeight(size);
}

/** Tracked-out capitals, used for section titles and control labels. */
inline juce::Font labelFont(float size = 10.5f, bool bold = true)
{
    return font(size, bold).withExtraKerningFactor(0.06f);
}
} // namespace theme

struct DeasynthLookAndFeel : juce::LookAndFeel_V4
{
    DeasynthLookAndFeel();

    juce::Typeface::Ptr getTypefaceForFont(const juce::Font &) override;
    juce::Font getPopupMenuFont() override { return theme::font(14.f); }
    void drawPopupMenuBackground(juce::Graphics &, int width, int height) override;
    void drawPopupMenuItem(juce::Graphics &, const juce::Rectangle<int> &area, bool isSeparator,
                           bool isActive, bool isHighlighted, bool isTicked, bool hasSubMenu,
                           const juce::String &text, const juce::String &shortcutKeyText,
                           const juce::Drawable *icon, const juce::Colour *textColour) override;
    void drawScrollbar(juce::Graphics &, juce::ScrollBar &, int x, int y, int width, int height,
                       bool isScrollbarVertical, int thumbStartPosition, int thumbSize,
                       bool isMouseOver, bool isMouseDown) override;
    void fillTextEditorBackground(juce::Graphics &, int width, int height,
                                  juce::TextEditor &) override;
    void drawTextEditorOutline(juce::Graphics &, int width, int height,
                               juce::TextEditor &) override;
};
} // namespace deasynth

#endif
