/*
 * Deasynth - an analog-voiced hybrid synthesizer built on the Surge XT engine.
 *
 * Deasynth is released under the GNU General Public Licence v3 or later (GPL-3.0-or-later).
 * The license is found in the "LICENSE" file in the root of this repository, or at
 * https://www.gnu.org/licenses/gpl-3.0.en.html
 */

#ifndef DEASYNTH_SRC_GUI_DEASYNTHEDITOR_H
#define DEASYNTH_SRC_GUI_DEASYNTHEDITOR_H

#include "../DeasynthProcessor.h"
#include "Panels.h"

namespace deasynth
{
class ModBar;
class PresetBrowser;
class SaveDialog;
class AboutPanel;
class NoticePanel;
class Wheel;
} // namespace deasynth

class DeasynthEditor : public juce::AudioProcessorEditor,
                       private juce::Timer,
                       private SurgeStorage::ErrorListener
{
  public:
    explicit DeasynthEditor(DeasynthProcessor &);
    ~DeasynthEditor() override;

    void paint(juce::Graphics &) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress &key) override;

    static constexpr int baseWidth = 1280;
    static constexpr int baseHeight = 800;

    enum Page
    {
        Synth = 0,
        Mod,
        Fx,
        numPages
    };

    void setPage(int p);
    void setScene(int s);
    void setZoom(float z);
    void loadPatch(int id);
    void stepPatch(bool forward);
    void showBrowser(bool show);
    void showSaveDialog();
    void showAbout(bool show);
    void showMainMenu();
    void initPatch();
    deasynth::EditorContext &context() { return ctx; }

  private:
    struct Root; // the fixed-size design surface; scaled as a whole to the window

    void timerCallback() override;
    // SurgeStorage::ErrorListener. Called from any thread; shown on the next timer tick.
    void onSurgeError(const std::string &msg, const std::string &title,
                      const SurgeStorage::ErrorType &type) override;
    void refreshAll(bool force);
    void layoutRoot();
    void paintRoot(juce::Graphics &g);
    juce::String currentPatchName() const;

    DeasynthProcessor &processor;
    deasynth::DeasynthLookAndFeel lnf;
    deasynth::EditorContext ctx;
    juce::TooltipWindow tooltips{this, 700};

    std::unique_ptr<Root> root;

    // header
    std::unique_ptr<deasynth::FlatButton> pageTabs[numPages];
    std::unique_ptr<deasynth::FlatButton> sceneButtons[2];
    std::unique_ptr<deasynth::FlatButton> presetName, prevPreset, nextPreset, savePreset, menuButton;
    std::unique_ptr<deasynth::ScopeDisplay> scope;
    juce::Rectangle<int> hintArea, statusArea;
    juce::String hint, stickyHint;

    std::unique_ptr<deasynth::ModBar> modBar;

    // pages
    std::vector<std::unique_ptr<deasynth::Panel>> pagePanels[numPages];
    std::vector<std::unique_ptr<deasynth::Panel>> bottomPanels;
    deasynth::LfoPanel *lfoPanel{nullptr};

    // bottom
    std::unique_ptr<juce::MidiKeyboardComponent> keyboard;
    std::unique_ptr<deasynth::Wheel> pitchWheel, modWheel;

    // overlays
    std::unique_ptr<deasynth::PresetBrowser> browser;
    std::unique_ptr<deasynth::SaveDialog> saveDialog;
    std::unique_ptr<deasynth::AboutPanel> about;
    std::unique_ptr<deasynth::NoticePanel> notice;

    std::mutex errorMutex;
    std::vector<std::pair<juce::String, juce::String>> pendingErrors; // title, message

    int page{Synth};
    int watchdogTick{0};
    bool pendingStructure{true};
    juce::String shownPatchName;
    bool shownDirty{false};
    int shownVoices{-1};
    int shownScene{-1};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DeasynthEditor)
};

#endif
