/*
 * Deasynth - an analog-voiced hybrid synthesizer built on the Surge XT engine.
 *
 * Deasynth is released under the GNU General Public Licence v3 or later (GPL-3.0-or-later).
 * The license is found in the "LICENSE" file in the root of this repository, or at
 * https://www.gnu.org/licenses/gpl-3.0.en.html
 */

#include "Theme.h"
#include "DeasynthBinary.h"

namespace deasynth
{
namespace theme
{
juce::Typeface::Ptr regularTypeface()
{
    static juce::Typeface::Ptr tf = juce::Typeface::createSystemTypefaceFor(
        DeasynthBinary::LatoRegular_ttf, DeasynthBinary::LatoRegular_ttfSize);
    return tf;
}

juce::Typeface::Ptr boldTypeface()
{
    static juce::Typeface::Ptr tf = juce::Typeface::createSystemTypefaceFor(
        DeasynthBinary::LatoBold_ttf, DeasynthBinary::LatoBold_ttfSize);
    return tf;
}
} // namespace theme

DeasynthLookAndFeel::DeasynthLookAndFeel()
{
    setColour(juce::PopupMenu::backgroundColourId, theme::panelHi);
    setColour(juce::PopupMenu::textColourId, theme::text);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, theme::osc.withAlpha(0.22f));
    setColour(juce::PopupMenu::highlightedTextColourId, theme::text);
    setColour(juce::PopupMenu::headerTextColourId, theme::textDim);

    setColour(juce::TextEditor::backgroundColourId, theme::well);
    setColour(juce::TextEditor::textColourId, theme::text);
    setColour(juce::TextEditor::highlightColourId, theme::osc.withAlpha(0.35f));
    setColour(juce::TextEditor::highlightedTextColourId, theme::text);
    setColour(juce::TextEditor::outlineColourId, theme::outline);
    setColour(juce::TextEditor::focusedOutlineColourId, theme::osc);
    setColour(juce::CaretComponent::caretColourId, theme::osc);

    setColour(juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    setColour(juce::ScrollBar::thumbColourId, theme::track);

    setColour(juce::AlertWindow::backgroundColourId, theme::panel);
    setColour(juce::AlertWindow::textColourId, theme::text);
    setColour(juce::AlertWindow::outlineColourId, theme::outline);
    setColour(juce::TextButton::buttonColourId, theme::panelHi);
    setColour(juce::TextButton::textColourOffId, theme::text);
    setColour(juce::ResizableWindow::backgroundColourId, theme::backdrop);
    setColour(juce::Label::textColourId, theme::text);
    setColour(juce::TooltipWindow::backgroundColourId, theme::panelHi);
    setColour(juce::TooltipWindow::textColourId, theme::text);
    setColour(juce::TooltipWindow::outlineColourId, theme::outline);
}

juce::Typeface::Ptr DeasynthLookAndFeel::getTypefaceForFont(const juce::Font &f)
{
    return f.isBold() ? theme::boldTypeface() : theme::regularTypeface();
}

void DeasynthLookAndFeel::getIdealPopupMenuItemSize(const juce::String &text, bool isSeparator,
                                                    int standardMenuItemHeight, int &idealWidth,
                                                    int &idealHeight)
{
    juce::LookAndFeel_V4::getIdealPopupMenuItemSize(text, isSeparator, standardMenuItemHeight,
                                                    idealWidth, idealHeight);
    if (!isSeparator)
    {
        // room for the tick dot on the left and a submenu arrow on the right
        idealWidth += 30;
        idealHeight = std::max(idealHeight, 24);
    }
}

void DeasynthLookAndFeel::drawPopupMenuBackground(juce::Graphics &g, int width, int height)
{
    g.fillAll(theme::panelHi);
    g.setColour(theme::outline);
    g.drawRect(0, 0, width, height, 1);
}

void DeasynthLookAndFeel::drawPopupMenuItem(juce::Graphics &g, const juce::Rectangle<int> &area,
                                            bool isSeparator, bool isActive, bool isHighlighted,
                                            bool isTicked, bool hasSubMenu,
                                            const juce::String &text,
                                            const juce::String &shortcutKeyText,
                                            const juce::Drawable *, const juce::Colour *)
{
    if (isSeparator)
    {
        g.setColour(theme::outline);
        g.fillRect(area.reduced(8, 0).withSizeKeepingCentre(area.getWidth() - 16, 1));
        return;
    }

    auto r = area.reduced(3, 1);
    if (isHighlighted && isActive)
    {
        g.setColour(theme::osc.withAlpha(0.2f));
        g.fillRoundedRectangle(r.toFloat(), 4.f);
    }

    g.setColour(isTicked ? theme::osc : (isActive ? theme::text : theme::textFaint));
    g.setFont(theme::font(14.f, isTicked));

    auto textArea = r.reduced(10, 0);
    if (isTicked)
    {
        g.fillEllipse(juce::Rectangle<float>(5.f, 5.f).withCentre(
            {(float)r.getX() + 7.f, (float)r.getCentreY()}));
    }
    textArea.removeFromLeft(6);

    if (hasSubMenu)
    {
        auto arrow = textArea.removeFromRight(10).toFloat();
        juce::Path p;
        p.addTriangle(arrow.getX(), arrow.getCentreY() - 4.f, arrow.getX(),
                      arrow.getCentreY() + 4.f, arrow.getX() + 5.f, arrow.getCentreY());
        g.fillPath(p);
    }

    if (shortcutKeyText.isNotEmpty())
    {
        g.setColour(theme::textFaint);
        g.setFont(theme::font(12.f));
        g.drawText(shortcutKeyText, textArea, juce::Justification::centredRight);
        g.setColour(isActive ? theme::text : theme::textFaint);
        g.setFont(theme::font(14.f));
    }

    g.drawFittedText(text, textArea, juce::Justification::centredLeft, 1);
}

void DeasynthLookAndFeel::drawScrollbar(juce::Graphics &g, juce::ScrollBar &, int x, int y,
                                        int width, int height, bool vertical, int thumbStart,
                                        int thumbSize, bool over, bool down)
{
    juce::Rectangle<int> thumb = vertical
                                     ? juce::Rectangle<int>(x + 2, thumbStart, width - 4, thumbSize)
                                     : juce::Rectangle<int>(thumbStart, y + 2, thumbSize, height - 4);
    g.setColour((over || down) ? theme::textFaint : theme::track);
    g.fillRoundedRectangle(thumb.toFloat(), 3.f);
}

void DeasynthLookAndFeel::fillTextEditorBackground(juce::Graphics &g, int width, int height,
                                                   juce::TextEditor &)
{
    g.setColour(theme::well);
    g.fillRoundedRectangle(0.f, 0.f, (float)width, (float)height, 5.f);
}

void DeasynthLookAndFeel::drawTextEditorOutline(juce::Graphics &g, int width, int height,
                                                juce::TextEditor &ed)
{
    g.setColour(ed.hasKeyboardFocus(true) ? theme::osc : theme::outline);
    g.drawRoundedRectangle(0.5f, 0.5f, (float)width - 1.f, (float)height - 1.f, 5.f, 1.f);
}
} // namespace deasynth
