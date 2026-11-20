#!/bin/bash
#
# Render the Deasynth demo film (90 seconds, 1920 x 1080, with sound) to an MP4.
#
#   scripts/deasynth/render-demo.sh [build directory] [output file]
#
# Defaults: build directory "build" (Linux) or "build-deasynth" (macOS build script), output
# "deasynth-demo.mp4" in the repository root.
#
# The film is made by the real plugin: build/deasynth-selftest --render-demo opens the editor,
# operates it from a script and writes the frames and the instrument's own audio; this script
# then encodes them. See src/deasynth/tests/demovideo.cpp for the script itself.
#
# Needs: the deasynth-selftest target built, the factory library installed
# (scripts/deasynth/install-content.sh), ffmpeg, and on Linux xvfb-run.
#
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD="${1:-}"
if [[ -z "$BUILD" ]]; then
  BUILD="$ROOT/build"
  [[ -x "$BUILD/deasynth-selftest" ]] || BUILD="$ROOT/build-deasynth"
fi
OUT="${2:-$ROOT/deasynth-demo.mp4}"
TOOL="$BUILD/deasynth-selftest"

[[ -x "$TOOL" ]] || { echo "Build the deasynth-selftest target first (looked in $BUILD)." >&2; exit 1; }
command -v ffmpeg >/dev/null || { echo "ffmpeg is required (brew install ffmpeg / apt install ffmpeg)." >&2; exit 1; }

# frames go in a fresh temporary directory, which is left behind for inspection
WORK="$(mktemp -d "${TMPDIR:-/tmp}/deasynth-demo.XXXXXX")"
echo "Rendering frames into $WORK (a few minutes)"

if [[ "$(uname -s)" == "Darwin" ]]; then
  "$TOOL" --render-demo "$WORK"
else
  command -v xvfb-run >/dev/null || { echo "xvfb-run is required on Linux (apt install xvfb)." >&2; exit 1; }
  xvfb-run -a -s "-screen 0 1400x900x24 -noreset" "$TOOL" --render-demo "$WORK"
fi

echo "Encoding $OUT"
ffmpeg -hide_banner -loglevel error -y \
  -framerate 30 -i "$WORK/frame-%05d.jpg" -i "$WORK/soundtrack.wav" \
  -c:v libx264 -preset slow -crf 17 -pix_fmt yuv420p -movflags +faststart \
  -c:a aac -b:a 256k -shortest "$OUT"

echo "Done: $OUT"
echo "Frames and soundtrack are in $WORK"
