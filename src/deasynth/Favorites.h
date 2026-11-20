/*
 * Deasynth - an analog-voiced hybrid synthesizer built on the Surge XT engine.
 *
 * Deasynth is released under the GNU General Public Licence v3 or later (GPL-3.0-or-later).
 * The license is found in the "LICENSE" file in the root of this repository, or at
 * https://www.gnu.org/licenses/gpl-3.0.en.html
 */

#ifndef DEASYNTH_SRC_FAVORITES_H
#define DEASYNTH_SRC_FAVORITES_H

#include "SurgeStorage.h"
#include "juce_core/juce_core.h"
#include <set>

namespace deasynth
{
/*
 * The patches the user has marked as favourites.
 *
 * They are kept in a small text file in the user's Deasynth folder, one patch per line, so
 * they are shared by the standalone app and every plugin instance, survive reinstalling the
 * factory library, and can be read or edited by hand. A patch is identified by where it is
 * inside its library ("factory/Basses/Sub 1.fxp"), not by its full path, so the list still
 * matches if the libraries are moved or the same folder is used from another Mac.
 *
 * Message thread only.
 */
class Favorites
{
  public:
    /** Use the standard file in this engine's user folder, and read it. */
    void attach(SurgeStorage &storage);
    /** Use a specific file (tests), and read it. */
    void attach(const juce::File &file, SurgeStorage *storage = nullptr);

    /** Read the file again; another instance may have changed it. */
    void reload();

    bool contains(const Patch &p) const;
    /** Returns false if the change could not be written; the in-memory state is then undone. */
    bool set(const Patch &p, bool favourite);
    bool toggle(const Patch &p) { return set(p, !contains(p)); }
    int count() const { return (int)keys.size(); }
    juce::File getFile() const { return file; }

    /** The library-relative identity of a patch, or empty if it is in no known library. */
    juce::String keyFor(const Patch &p) const;

  private:
    bool save() const;

    juce::File file;
    juce::String userPatches; // the user patch folder, with forward slashes
    std::set<juce::String> keys;
};
} // namespace deasynth

#endif
