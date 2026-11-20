/*
 * Deasynth - an analog-voiced hybrid synthesizer built on the Surge XT engine.
 *
 * Deasynth is released under the GNU General Public Licence v3 or later (GPL-3.0-or-later).
 * The license is found in the "LICENSE" file in the root of this repository, or at
 * https://www.gnu.org/licenses/gpl-3.0.en.html
 */

#ifndef DEASYNTH_SRC_GUI_OVERLAYS_H
#define DEASYNTH_SRC_GUI_OVERLAYS_H

#include "Widgets.h"

class DeasynthEditor;

namespace deasynth
{
/** The strip of modulation sources. Click one to arm it, then drag knobs to assign it. */
class ModBar : public juce::Component, public Refreshable
{
  public:
    explicit ModBar(EditorContext &ctx);

    std::function<void(int lfoIndex)> onLfoChosen;

    void refresh(bool force) override;
    void paint(juce::Graphics &g) override;
    void resized() override;

  private:
    struct Chip
    {
        int source;
        std::unique_ptr<FlatButton> button;
    };
    struct Group
    {
        juce::String label;
        juce::Colour colour;
        std::vector<int> sources;
        int chipWidth;
        juce::Rectangle<int> labelArea;
    };

    void arm(int source);
    void sourceMenu(int source);

    EditorContext &ctx;
    std::vector<Chip> chips;
    std::vector<Group> groups;
    juce::Rectangle<int> messageArea;
};

/** Pitch bend and mod wheels beside the on-screen keyboard. */
class Wheel : public juce::Component
{
  public:
    Wheel(bool springsBack, const juce::String &label);

    std::function<void(float)> onChange; // -1...1 for a sprung wheel, 0...1 otherwise

    void setValue(float v);
    void paint(juce::Graphics &g) override;
    void mouseDown(const juce::MouseEvent &e) override { mouseDrag(e); }
    void mouseDrag(const juce::MouseEvent &e) override;
    void mouseUp(const juce::MouseEvent &e) override;

  private:
    bool sprung;
    juce::String label;
    float value{0.f};
};

/** Factory, third party and user patches, by category or by search. */
class PresetBrowser : public juce::Component, private juce::TextEditor::Listener
{
  public:
    PresetBrowser(DeasynthEditor &editor, EditorContext &ctx);
    ~PresetBrowser() override;

    void rebuild();
    void paint(juce::Graphics &g) override;
    void resized() override;
    void visibilityChanged() override;
    bool keyPressed(const juce::KeyPress &key) override;
    void mouseDown(const juce::MouseEvent &e) override;

  private:
    struct CategoryModel;
    struct PatchModel;
    struct CategoryEntry
    {
        int category; // index into patch_category, -1 = everything, -2 = section header
        juce::String label;
        int count;
    };

    /** The search box keeps the keyboard, and hands the keys that move the list back to us. */
    struct SearchBox : juce::TextEditor
    {
        std::function<bool(const juce::KeyPress &)> onNavigationKey;
        bool keyPressed(const juce::KeyPress &key) override
        {
            if (onNavigationKey && onNavigationKey(key))
                return true;
            return juce::TextEditor::keyPressed(key);
        }
    };

    void textEditorTextChanged(juce::TextEditor &) override { rebuildPatches(); }
    void rebuildPatches();
    void selectCategory(int row);
    /** Load the patch in a row of the list and show it selected. */
    void choosePatchRow(int row);
    /** Move the selection by a number of rows (arrow keys, page keys). */
    void stepSelection(int delta);
    bool handleNavigationKey(const juce::KeyPress &key);
    int selectedPatchRow() const;

    DeasynthEditor &editor;
    EditorContext &ctx;
    SearchBox search;
    FlatButton close;
    juce::ListBox categoryList, patchList;
    // The patch the browser considers selected. Tracked here rather than read from the
    // engine, which loads on the audio thread and so reports the new patch a moment later.
    int selectedPatchId{-1};
    std::unique_ptr<CategoryModel> categoryModel;
    std::unique_ptr<PatchModel> patchModel;
    std::vector<CategoryEntry> categories;
    std::vector<int> patches; // indices into patch_list
    int selectedCategoryRow{0};
    juce::Rectangle<int> card;
};

/** Name, category and author for saving the current sound as a user patch. */
class SaveDialog : public juce::Component
{
  public:
    SaveDialog(DeasynthEditor &editor, EditorContext &ctx);

    void paint(juce::Graphics &g) override;
    void resized() override;
    void visibilityChanged() override;
    bool keyPressed(const juce::KeyPress &key) override;

  private:
    void commit();

    DeasynthEditor &editor;
    EditorContext &ctx;
    juce::TextEditor name, category, author;
    FlatButton save, cancel;
    juce::Rectangle<int> card;
    juce::String error;
};

/** A message from the engine (a patch that would not load or save, a missing wavetable). */
class NoticePanel : public juce::Component
{
  public:
    void show(const juce::String &title, const juce::String &message);
    void paint(juce::Graphics &g) override;
    void mouseDown(const juce::MouseEvent &) override { setVisible(false); }
    bool keyPressed(const juce::KeyPress &) override
    {
        setVisible(false);
        return true;
    }

  private:
    juce::String title, message;
};

class AboutPanel : public juce::Component
{
  public:
    explicit AboutPanel(DeasynthEditor &editor);
    void paint(juce::Graphics &g) override;
    void mouseDown(const juce::MouseEvent &) override;

  private:
    DeasynthEditor &editor;
};
} // namespace deasynth

#endif
