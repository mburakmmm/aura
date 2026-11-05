#!/bin/sh
# Builds one Aura example through an Xcode project.
# Usage: scripts/build_ios_xcode.sh examples/notes/main.nox Notes [device|simulator|store]
# device writes a development archive, dSYM and IPA.
# store requires an App Store profile for the example bundle id.
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
# shellcheck source=scripts/nox_root.sh
. "$ROOT/scripts/nox_root.sh"
NOX="$(nox_root "$ROOT")"
SKIA_PIN="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["skia"])' "$ROOT/toolchain/manifest.json")"
SKIA="${SKIA_SRC:-/tmp/skia-$SKIA_PIN-src}"
SRC="${1:?nox source}"
NAME="${2:?app name}"
KIND="${3:-device}"
case "$KIND" in
  simulator)
    CONFIG=Debug
    SIGNING_KIND=""
    SKIA_OUT="$SKIA/out/ios-sim"
    DESTINATION="generic/platform=iOS Simulator"
    ;;
  device)
    CONFIG=Release
    SIGNING_KIND=development
    SKIA_OUT="$SKIA/out/ios-arm64"
    DESTINATION="generic/platform=iOS"
    ;;
  store)
    CONFIG=Store
    SIGNING_KIND=store
    SKIA_OUT="$SKIA/out/ios-arm64"
    DESTINATION="generic/platform=iOS"
    ;;
  *)
    echo "kind must be simulator, device or store" >&2
    exit 2
    ;;
esac
if [ ! -f "$SKIA_OUT/libskia.a" ]; then
  echo "skia yok: $SKIA_OUT/libskia.a" >&2
  exit 1
fi
OUT="$ROOT/build/ios/xcode/$NAME"
rm -rf "$OUT"
mkdir -p "$OUT"
if [ -n "$SIGNING_KIND" ]; then
  python3 "$ROOT/scripts/ios_project.py" signing --root "$ROOT" --name "$NAME" --kind "$SIGNING_KIND" --out "$OUT/signing.json"
  python3 "$ROOT/scripts/ios_project.py" generate --root "$ROOT" --name "$NAME" --out "$OUT" --configuration "$CONFIG" --signing "$OUT/signing.json"
else
  python3 "$ROOT/scripts/ios_project.py" generate --root "$ROOT" --name "$NAME" --out "$OUT" --configuration "$CONFIG"
fi
if [ "${SRC#/}" != "$SRC" ]; then
  APP_SOURCE="$SRC"
else
  APP_SOURCE="$ROOT/$SRC"
fi
if [ "$KIND" = "simulator" ]; then
  xcodebuild \
    -project "$OUT/$NAME.xcodeproj" \
    -scheme Aura \
    -configuration Debug \
    -destination "$DESTINATION" \
    -derivedDataPath "$OUT/DerivedData" \
    AURA_REPO="$ROOT" \
    NOX_ROOT="$NOX" \
    SKIA_SRC="$SKIA" \
    SKIA_OUT="$SKIA_OUT" \
    AURA_APP_SOURCE="$APP_SOURCE" \
    build
  echo "$OUT/DerivedData/Build/Products/Debug-iphonesimulator/$NAME.app"
  exit 0
fi
xcodebuild \
  -project "$OUT/$NAME.xcodeproj" \
  -scheme Aura \
  -configuration "$CONFIG" \
  -destination "$DESTINATION" \
  -archivePath "$OUT/$NAME.xcarchive" \
  -derivedDataPath "$OUT/DerivedData" \
  AURA_REPO="$ROOT" \
  NOX_ROOT="$NOX" \
  SKIA_SRC="$SKIA" \
  SKIA_OUT="$SKIA_OUT" \
  AURA_APP_SOURCE="$APP_SOURCE" \
  archive
xcodebuild \
  -exportArchive \
  -archivePath "$OUT/$NAME.xcarchive" \
  -exportPath "$OUT/ipa" \
  -exportOptionsPlist "$OUT/ExportOptions.plist"
find "$OUT/$NAME.xcarchive/dSYMs" -name '*.dSYM' -print
echo "$OUT/ipa/$NAME.ipa"
