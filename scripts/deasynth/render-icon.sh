#!/bin/bash
#
# Render the app icon (resources/deasynth/DeasynthIcon.png, 1024 x 1024) from the logo
# (resources/deasynth/DeasynthLogo.svg). Run this after changing the logo, then rebuild.
#
# Needs one of: rsvg-convert (brew install librsvg), Inkscape, or ImageMagick.
#
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SVG="$ROOT/resources/deasynth/DeasynthLogo.svg"
PNG="$ROOT/resources/deasynth/DeasynthIcon.png"

if command -v rsvg-convert >/dev/null; then
  rsvg-convert -w 1024 -h 1024 "$SVG" -o "$PNG"
elif command -v inkscape >/dev/null; then
  inkscape "$SVG" --export-type=png --export-width=1024 --export-height=1024 --export-filename="$PNG"
elif command -v magick >/dev/null; then
  magick -background none -density 192 "$SVG" -resize 1024x1024 "$PNG"
elif command -v convert >/dev/null; then
  convert -background none -density 192 "$SVG" -resize 1024x1024 "$PNG"
else
  echo "Install rsvg-convert (brew install librsvg), Inkscape or ImageMagick to render the icon." >&2
  exit 1
fi
echo "Wrote $PNG"
