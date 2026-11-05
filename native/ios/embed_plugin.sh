#!/bin/sh
# Copies the signed probe into the app bundle. An unsigned device dylib is an error.
set -eu
if [ -z "${AURA_REPO:-}" ] || [ -z "${BUILT_PRODUCTS_DIR:-}" ] || [ -z "${CONTENTS_FOLDER_PATH:-}" ]; then
  echo "AURA_REPO, BUILT_PRODUCTS_DIR and CONTENTS_FOLDER_PATH are required" >&2
  exit 1
fi
app="$BUILT_PRODUCTS_DIR/$CONTENTS_FOLDER_PATH"
probe="$BUILT_PRODUCTS_DIR/libaura_probe.dylib"
if [ ! -f "$probe" ]; then
  echo "probe dylib yok: $probe" >&2
  exit 1
fi
mkdir -p "$app/native/plugins" "$app/native/fonts" "$app/tests/fixtures"
cp "$probe" "$app/native/plugins/libaura_probe.dylib"
cp "$AURA_REPO/native/fonts/NotoSans-Regular.ttf" "$app/native/fonts/NotoSans-Regular.ttf"
cp "$AURA_REPO/tests/fixtures/pixel.png" "$AURA_REPO/tests/fixtures/pixel.jpg" "$app/tests/fixtures/"
if [ "${PLATFORM_NAME:-}" = "iphoneos" ]; then
  identity="${EXPANDED_CODE_SIGN_IDENTITY:-}"
  if [ -z "$identity" ] || [ "$identity" = "-" ]; then
    echo "iphoneos plugin dylib requires a signing identity" >&2
    exit 1
  fi
  codesign --force --sign "$identity" "$app/native/plugins/libaura_probe.dylib"
fi
