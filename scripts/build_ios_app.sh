#!/bin/sh
# Builds one Aura example into an iOS .app. GPU stays off.
# Usage: scripts/build_ios_app.sh examples/proof/main.nox Proof [simulator|device]
# device links ios-arm64 and does not install or launch.
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
# shellcheck source=scripts/nox_root.sh
. "$ROOT/scripts/nox_root.sh"
NOX="$(nox_root "$ROOT")"
SKIA_PIN="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["skia"])' "$ROOT/toolchain/manifest.json")"
SKIA="${SKIA_SRC:-/tmp/skia-$SKIA_PIN-src}"
SRC="${1:?nox source}"
NAME="${2:?app name}"
SLICE="${3:-simulator}"
OUT="$ROOT/build/ios/$SLICE/$NAME"
case "$SLICE" in
  simulator)
    SDK="$(xcrun --sdk iphonesimulator --show-sdk-path)"
    TARGET="arm64-apple-ios17.0-simulator"
    NOX_TARGET="ios-simulator-arm64"
    RT="$NOX/zig-out/lib/noxrt-ios-simulator-arm64.o"
    SKIA_OUT="$SKIA/out/ios-sim"
    ;;
  device)
    SDK="$(xcrun --sdk iphoneos --show-sdk-path)"
    TARGET="arm64-apple-ios17.0"
    NOX_TARGET="ios-arm64"
    RT="$NOX/zig-out/lib/noxrt-ios-arm64.o"
    SKIA_OUT="$SKIA/out/ios-arm64"
    ;;
  *)
    echo "slice must be simulator or device" >&2
    exit 1
    ;;
esac
NOXC="$NOX/zig-out/bin/noxc"
LIBSKIA="$SKIA_OUT/libskia.a"
if [ ! -x "$NOXC" ]; then
  echo "noxc yok: $NOXC" >&2
  exit 1
fi
if [ ! -f "$RT" ]; then
  echo "noxrt yok: $RT (nox deposunda zig build mobile-rt)" >&2
  exit 1
fi
if [ ! -f "$LIBSKIA" ]; then
  echo "simulator skia yok: $LIBSKIA" >&2
  exit 1
fi
mkdir -p "$OUT/obj" "$OUT/$NAME.app/tests/fixtures" "$OUT/$NAME.app/native/plugins" "$OUT/$NAME.app/native/fonts"
"$NOXC" build --target "$NOX_TARGET" --emit-obj -o "$OUT/obj/app.o" "$SRC"
clang -c -target "$TARGET" -isysroot "$SDK" -I "$ROOT/native/mobile" -I "$ROOT/native/skia" -I "$ROOT/native/host" -I "$NOX/include" -o "$OUT/obj/shell.o" "$ROOT/native/mobile/shell.c"
clang -c -target "$TARGET" -isysroot "$SDK" -I "$ROOT/native/mobile" -o "$OUT/obj/events.o" "$ROOT/native/mobile/events.c"
clang -c -target "$TARGET" -isysroot "$SDK" -I "$ROOT/native/mobile" -o "$OUT/obj/event_queue.o" "$ROOT/native/mobile/event_queue.c"
clang -c -target "$TARGET" -isysroot "$SDK" -I "$ROOT/native/mobile" -I "$NOX/include" -o "$OUT/obj/app_runtime.o" "$ROOT/native/mobile/app_runtime.c"
clang -c -target "$TARGET" -isysroot "$SDK" -I "$ROOT/native/mobile" -o "$OUT/obj/storage.o" "$ROOT/native/mobile/storage.c"
clang -c -target "$TARGET" -isysroot "$SDK" -I "$ROOT/native/mobile" -o "$OUT/obj/ax_platform.o" "$ROOT/native/mobile/ax_platform.c"
clang -c -target "$TARGET" -isysroot "$SDK" -I "$ROOT/native/mobile" -o "$OUT/obj/text_units.o" "$ROOT/native/mobile/text_units.c"
clang -c -target "$TARGET" -isysroot "$SDK" -I "$ROOT/native/host" -o "$OUT/obj/plugin.o" "$ROOT/native/host/plugin.c"
clang -c -target "$TARGET" -isysroot "$SDK" -I "$ROOT/native/host" -o "$OUT/obj/listing.o" "$ROOT/native/host/listing.c"
clang -fno-objc-arc -c -target "$TARGET" -isysroot "$SDK" -I "$ROOT/native/mobile" -I "$NOX/include" -o "$OUT/obj/scene.o" "$ROOT/native/ios/scene.m"
clang++ -std=c++17 -c -target "$TARGET" -isysroot "$SDK" -I "$SKIA" -I "$ROOT/native/skia" -I "$ROOT/native/mobile" -o "$OUT/obj/paint.o" "$ROOT/native/skia/paint.cc"
clang -dynamiclib -target "$TARGET" -isysroot "$SDK" -o "$OUT/$NAME.app/native/plugins/libaura_probe.dylib" "$ROOT/native/plugins/probe.c"
if [ -n "${AURA_IOS_SIGN_IDENTITY:-}" ]; then
  codesign --force --sign "$AURA_IOS_SIGN_IDENTITY" "$OUT/$NAME.app/native/plugins/libaura_probe.dylib"
fi
cp "$ROOT/tests/fixtures/pixel.png" "$ROOT/tests/fixtures/pixel.jpg" "$OUT/$NAME.app/tests/fixtures/"
cp "$ROOT/native/fonts/NotoSans-Regular.ttf" "$OUT/$NAME.app/native/fonts/"
cat > "$OUT/$NAME.app/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleExecutable</key><string>$NAME</string>
  <key>CFBundleIdentifier</key><string>com.noxlang.aura.$NAME</string>
  <key>CFBundleName</key><string>$NAME</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleVersion</key><string>1</string>
  <key>CFBundleShortVersionString</key><string>1.0</string>
  <key>MinimumOSVersion</key><string>17.0</string>
  <key>LSRequiresIPhoneOS</key><true/>
  <key>UIDeviceFamily</key>
  <array><integer>1</integer><integer>2</integer></array>
  <key>UISupportedInterfaceOrientations</key>
  <array>
    <string>UIInterfaceOrientationPortrait</string>
    <string>UIInterfaceOrientationLandscapeLeft</string>
    <string>UIInterfaceOrientationLandscapeRight</string>
  </array>
  <key>UILaunchScreen</key><dict/>
  <key>NSUserActivityTypes</key>
  <array><string>com.noxlang.aura.scene</string></array>
  <key>UIApplicationSceneManifest</key>
  <dict>
    <key>UIApplicationSupportsMultipleScenes</key><false/>
    <key>UISceneConfigurations</key>
    <dict>
      <key>UIWindowSceneSessionRoleApplication</key>
      <array>
        <dict>
          <key>UISceneConfigurationName</key><string>Default</string>
          <key>UISceneDelegateClassName</key><string>AuraSceneDelegate</string>
        </dict>
      </array>
    </dict>
  </dict>
</dict>
</plist>
EOF
clang++ -target "$TARGET" -isysroot "$SDK" -fno-objc-arc \
  -o "$OUT/$NAME.app/$NAME" \
  "$OUT/obj/app.o" "$OUT/obj/shell.o" "$OUT/obj/events.o" "$OUT/obj/event_queue.o" "$OUT/obj/app_runtime.o" "$OUT/obj/storage.o" "$OUT/obj/ax_platform.o" "$OUT/obj/text_units.o" "$OUT/obj/plugin.o" "$OUT/obj/listing.o" "$OUT/obj/scene.o" "$OUT/obj/paint.o" "$RT" \
  "$LIBSKIA" \
  "$SKIA_OUT/libpng.a" \
  "$SKIA_OUT/libjpeg.a" \
  "$SKIA_OUT/libwebp.a" \
  "$SKIA_OUT/libzlib.a" \
  "$SKIA_OUT/libfreetype2.a" \
  "$SKIA_OUT/libskcms.a" \
  -framework UIKit -framework Foundation -framework CoreGraphics -framework QuartzCore -framework CoreText -framework Security -framework CFNetwork \
  -lc++
echo "$OUT/$NAME.app"
