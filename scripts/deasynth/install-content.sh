#!/bin/bash
#
# Install Deasynth's factory patches, wavetables and effect presets for the current user.
# Deasynth runs without them, but the patch browser and wavetable menus will be empty.
#
#   macOS   ~/Library/Application Support/Deasynth
#   Linux   ~/.local/share/Deasynth      (or $XDG_DATA_HOME/Deasynth)
#
#   scripts/deasynth/install-content.sh          copy the content (about 500 MB)
#   scripts/deasynth/install-content.sh --link   link to this checkout instead of copying
#
# Existing files in the destination are updated in place and nothing is deleted, so it is
# safe to run again after pulling new content.
#
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SRC="$ROOT/resources/data"
OWN="$ROOT/resources/deasynth/patches"
LINK=0
[[ "${1:-}" == "--link" ]] && LINK=1

case "$(uname -s)" in
  Darwin) DEST="$HOME/Library/Application Support/Deasynth" ;;
  *) DEST="${XDG_DATA_HOME:-$HOME/.local/share}/Deasynth" ;;
esac

[[ -d "$SRC/patches_factory" ]] || { echo "Factory content not found at $SRC" >&2; exit 1; }

mkdir -p "$(dirname "$DEST")"

if [[ $LINK -eq 1 ]]; then
  if [[ -e "$DEST" && ! -L "$DEST" ]]; then
    echo "$DEST already exists as a real folder. Move it aside first if you want a link." >&2
    exit 1
  fi
  ln -sfn "$SRC" "$DEST"
  echo "Linked $DEST -> $SRC"
  echo "Note: Deasynth's own patches (resources/deasynth/patches) are only added by a copying install."
  exit 0
fi

if [[ -L "$DEST" ]]; then
  echo "$DEST is a link (from an earlier --link install). Remove the link first to install a copy." >&2
  exit 1
fi

mkdir -p "$DEST"
if command -v rsync >/dev/null; then
  rsync -a "$SRC/" "$DEST/"
else
  cp -R "$SRC/." "$DEST/"
fi

# Deasynth's own patches sit alongside the engine's factory library
if compgen -G "$OWN/*.fxp" >/dev/null; then
  mkdir -p "$DEST/patches_factory/Deasynth"
  cp -f "$OWN"/*.fxp "$DEST/patches_factory/Deasynth/"
fi

echo "Installed factory content to $DEST"
