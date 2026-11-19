#!/bin/bash
#
# Build Deasynth on macOS: the standalone app, the Audio Unit (for Logic Pro, GarageBand
# and MainStage) and the VST3. Run it from anywhere inside the repository.
#
#   scripts/deasynth/build-mac.sh                 build, sign, self-test
#   scripts/deasynth/build-mac.sh --install       ...and install for the current user
#   scripts/deasynth/build-mac.sh --notarize      ...and notarize for distribution
#
# Options
#   --install        copy the plugins to ~/Library/Audio/Plug-Ins and the app to /Applications,
#                    install the factory patches and wavetables, and validate the Audio Unit
#   --notarize       submit the signed products to Apple and staple the tickets
#                    (needs a Developer ID certificate and a notarytool keychain profile)
#   --native         build for this Mac's architecture only (faster; default is universal)
#   --debug          build a Debug configuration
#   --clean          start from an empty build directory
#   --skip-auval     do not run Apple's Audio Unit validation
#
# Environment
#   DEASYNTH_SIGN_IDENTITY   signing identity. Default: the "Developer ID Application"
#                            certificate for team $DEASYNTH_TEAM_ID if the keychain has one,
#                            otherwise an "Apple Development" one, otherwise ad-hoc ("-"),
#                            which is enough to run on the Mac that built it.
#   DEASYNTH_TEAM_ID         Apple Developer Team ID. Default H7CJSVP2JC.
#   DEASYNTH_NOTARY_PROFILE  notarytool keychain profile name. Default "deasynth-notary".
#                            Create it once with:
#                              xcrun notarytool store-credentials deasynth-notary \
#                                  --apple-id <your Apple ID> --team-id H7CJSVP2JC
#                            notarytool then asks for an app-specific password and keeps it
#                            in your keychain. Nothing secret is stored in this repository.
#
set -euo pipefail

TEAM_ID="${DEASYNTH_TEAM_ID:-H7CJSVP2JC}"
NOTARY_PROFILE="${DEASYNTH_NOTARY_PROFILE:-deasynth-notary}"
CONFIG=Release
ARCHS="arm64;x86_64"
INSTALL=0
NOTARIZE=0
CLEAN=0
AUVAL=1

for arg in "$@"; do
  case "$arg" in
    --install) INSTALL=1 ;;
    --notarize) NOTARIZE=1 ;;
    --native) ARCHS="$(uname -m)" ;;
    --debug) CONFIG=Debug ;;
    --clean) CLEAN=1 ;;
    --skip-auval) AUVAL=0 ;;
    -h|--help) sed -n '2,32p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "Unknown option: $arg (try --help)" >&2; exit 2 ;;
  esac
done

if [[ "$(uname -s)" != "Darwin" ]]; then
  echo "This script builds the macOS products and must be run on a Mac." >&2
  exit 1
fi

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD="$ROOT/build-deasynth"
cd "$ROOT"

say() { printf '\n==> %s\n' "$*"; }

# Replace a bundle at a destination. Refuses anything that is not a named bundle inside a
# directory, so a mistake in a path can never remove more than the one bundle intended.
replace_bundle() {
  local src="$1" dest="$2"
  case "$dest" in
    */Deasynth.app|*/Deasynth.component|*/Deasynth.vst3) ;;
    *) echo "Refusing to replace unexpected path: $dest" >&2; exit 1 ;;
  esac
  if [[ -e "$dest" ]]; then
    rm -rf "${dest:?}"
  fi
  ditto "$src" "$dest"
}

command -v cmake >/dev/null || { echo "CMake is required: brew install cmake" >&2; exit 1; }
xcode-select -p >/dev/null 2>&1 || { echo "Xcode or its command line tools are required: xcode-select --install" >&2; exit 1; }

if [[ ! -f libs/tuning-library/README.md || ! -f libs/JUCE/CMakeLists.txt ]]; then
  say "Fetching submodules"
  git submodule update --init --recursive
fi

if [[ $CLEAN -eq 1 && -d "$ROOT/build-deasynth" ]]; then
  say "Removing $ROOT/build-deasynth"
  rm -rf "${ROOT:?}/build-deasynth"
fi

GENERATOR=()
command -v ninja >/dev/null && GENERATOR=(-G Ninja)

say "Configuring ($CONFIG, $ARCHS)"
cmake -S . -B "$BUILD" ${GENERATOR[@]+"${GENERATOR[@]}"} \
  -DCMAKE_BUILD_TYPE="$CONFIG" \
  -DCMAKE_OSX_ARCHITECTURES="$ARCHS" \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=10.15 \
  -DSURGE_BUILD_XT=OFF -DSURGE_BUILD_FX=OFF \
  -DSURGE_BUILD_TESTRUNNER=OFF -DSURGE_BUILD_CLAP=OFF \
  -DDEASYNTH_TEAM_ID="$TEAM_ID"

say "Building"
cmake --build "$BUILD" --config "$CONFIG" --parallel \
  --target deasynth_Standalone deasynth_AU deasynth_VST3 deasynth-selftest

say "Self-test"
"$BUILD/deasynth-selftest"

ART="$BUILD/src/deasynth/deasynth_artefacts"
[[ -d "$ART/$CONFIG" ]] && ART="$ART/$CONFIG"
APP="$ART/Standalone/Deasynth.app"
AU="$ART/AU/Deasynth.component"
VST3="$ART/VST3/Deasynth.vst3"
for p in "$APP" "$AU" "$VST3"; do
  [[ -e "$p" ]] || { echo "Expected build product is missing: $p" >&2; exit 1; }
done

# ---- signing
IDENTITY="${DEASYNTH_SIGN_IDENTITY:-}"
if [[ -z "$IDENTITY" ]]; then
  IDS="$(security find-identity -v -p codesigning 2>/dev/null || true)"
  IDENTITY="$(printf '%s\n' "$IDS" | sed -n "s/.*\"\(Developer ID Application: .*($TEAM_ID)\)\".*/\1/p" | head -1)"
  [[ -z "$IDENTITY" ]] && IDENTITY="$(printf '%s\n' "$IDS" | sed -n "s/.*\"\(Apple Development: .*\)\".*/\1/p" | head -1)"
  [[ -z "$IDENTITY" ]] && IDENTITY="-"
fi

ENTITLEMENTS="$ROOT/resources/deasynth/Deasynth.entitlements"
if [[ "$IDENTITY" == "-" ]]; then
  say "Signing ad-hoc (no signing certificate for team $TEAM_ID in the keychain)"
  SIGN_FLAGS=(--force)
else
  say "Signing as: $IDENTITY"
  SIGN_FLAGS=(--force --timestamp --options runtime)
fi

codesign "${SIGN_FLAGS[@]}" --sign "$IDENTITY" "$AU"
codesign "${SIGN_FLAGS[@]}" --sign "$IDENTITY" "$VST3"
codesign "${SIGN_FLAGS[@]}" --entitlements "$ENTITLEMENTS" --sign "$IDENTITY" "$APP"
for p in "$APP" "$AU" "$VST3"; do
  codesign --verify --strict "$p"
done

# ---- notarization
if [[ $NOTARIZE -eq 1 ]]; then
  if [[ "$IDENTITY" != Developer\ ID\ Application* ]]; then
    echo "Notarization needs a 'Developer ID Application' certificate; signed with '$IDENTITY' instead." >&2
    exit 1
  fi
  DIST="$BUILD/dist"
  mkdir -p "$DIST"
  for p in "$APP" "$AU" "$VST3"; do
    name="$(basename "$p")"
    say "Notarizing $name"
    # ditto overwrites an existing archive, so nothing needs deleting between the two passes
    ditto -c -k --keepParent "$p" "$DIST/$name.zip"
    xcrun notarytool submit "$DIST/$name.zip" --keychain-profile "$NOTARY_PROFILE" --wait
    xcrun stapler staple "$p"
    ditto -c -k --keepParent "$p" "$DIST/$name.zip"   # the stapled copy, for sharing
  done
  say "Notarized, stapled copies are in $DIST"
fi

# ---- install
if [[ $INSTALL -eq 1 ]]; then
  say "Installing for $(id -un)"
  mkdir -p "$HOME/Library/Audio/Plug-Ins/Components" "$HOME/Library/Audio/Plug-Ins/VST3"
  replace_bundle "$AU" "$HOME/Library/Audio/Plug-Ins/Components/Deasynth.component"
  replace_bundle "$VST3" "$HOME/Library/Audio/Plug-Ins/VST3/Deasynth.vst3"
  APPDEST="/Applications"
  [[ -w "$APPDEST" ]] || APPDEST="$HOME/Applications"
  mkdir -p "$APPDEST"
  replace_bundle "$APP" "$APPDEST/Deasynth.app"
  "$ROOT/scripts/deasynth/install-content.sh"

  # make macOS re-read the Audio Unit so Logic sees the new build
  killall -9 AudioComponentRegistrar 2>/dev/null || true

  if [[ $AUVAL -eq 1 ]]; then
    say "Validating the Audio Unit (auval -v aumu Deas Eldr)"
    if auval -v aumu Deas Eldr > "$BUILD/auval.log" 2>&1; then
      tail -3 "$BUILD/auval.log"
    else
      tail -40 "$BUILD/auval.log"
      echo "Audio Unit validation failed; the full log is $BUILD/auval.log" >&2
      exit 1
    fi
  fi

  say "Installed"
  echo "  App         $APPDEST/Deasynth.app"
  echo "  Audio Unit  ~/Library/Audio/Plug-Ins/Components/Deasynth.component"
  echo "  VST3        ~/Library/Audio/Plug-Ins/VST3/Deasynth.vst3"
  echo
  echo "In Logic Pro: create a Software Instrument track, then Instrument slot >"
  echo "AU Instruments > Eldrin > Deasynth. If it is not listed, restart Logic, or open"
  echo "Logic Pro > Settings > Plug-in Manager and choose Reset & Rescan Selection."
else
  say "Built"
  echo "  $APP"
  echo "  $AU"
  echo "  $VST3"
  echo
  echo "Run again with --install to install them and the factory content."
fi
