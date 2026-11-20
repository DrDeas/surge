/*
 * Deasynth - an analog-voiced hybrid synthesizer built on the Surge XT engine.
 *
 * Deasynth is released under the GNU General Public Licence v3 or later (GPL-3.0-or-later).
 * The license is found in the "LICENSE" file in the root of this repository, or at
 * https://www.gnu.org/licenses/gpl-3.0.en.html
 */

#include "Favorites.h"

namespace deasynth
{
namespace
{
juce::String forwardSlashes(const juce::String &s) { return s.replaceCharacter('\\', '/'); }
} // namespace

void Favorites::attach(SurgeStorage &storage)
{
    attach(juce::File(juce::String(storage.userDataPath.u8string())).getChildFile("Favorites.txt"),
           &storage);
}

void Favorites::attach(const juce::File &f, SurgeStorage *storage)
{
    file = f;
    if (storage)
        userPatches = forwardSlashes(juce::String(storage->userPatchesPath.u8string()));
    if (userPatches.isNotEmpty() && !userPatches.endsWithChar('/'))
        userPatches << '/';
    reload();
}

void Favorites::reload()
{
    keys.clear();
    if (!file.existsAsFile())
        return;
    juce::StringArray lines;
    file.readLines(lines);
    for (auto line : lines)
    {
        line = line.trim();
        if (line.isNotEmpty() && !line.startsWithChar('#'))
            keys.insert(line);
    }
}

juce::String Favorites::keyFor(const Patch &p) const
{
    const auto path = forwardSlashes(juce::String(p.path.u8string()));

    // inside one of the engine's libraries: everything after the library folder's name
    static const std::pair<const char *, const char *> libraries[] = {
        {"/patches_factory/", "factory/"}, {"/patches_3rdparty/", "3rdparty/"}};
    for (const auto &[folder, prefix] : libraries)
    {
        const int at = path.lastIndexOf(folder);
        if (at >= 0)
            return juce::String(prefix) + path.substring(at + (int)strlen(folder));
    }

    if (userPatches.isNotEmpty() && path.startsWith(userPatches))
        return "user/" + path.substring(userPatches.length());

    return {};
}

bool Favorites::contains(const Patch &p) const
{
    const auto key = keyFor(p);
    return key.isNotEmpty() && keys.count(key) > 0;
}

bool Favorites::set(const Patch &p, bool favourite)
{
    const auto key = keyFor(p);
    if (key.isEmpty() || file == juce::File())
        return false;

    // start from what is on disk, so a change made by another instance is not lost
    reload();
    const bool was = keys.count(key) > 0;
    if (was == favourite)
        return true;

    if (favourite)
        keys.insert(key);
    else
        keys.erase(key);

    if (!save())
    {
        if (was)
            keys.insert(key);
        else
            keys.erase(key);
        return false;
    }
    return true;
}

bool Favorites::save() const
{
    juce::String text = "# Deasynth favourite patches, one per line.\n";
    for (const auto &k : keys)
        text << k << "\n";
    if (!file.getParentDirectory().createDirectory().wasOk())
        return false;
    // written to a temporary file and swapped in, so a crash cannot leave half a list
    return file.replaceWithText(text, false, false, "\n");
}
} // namespace deasynth
