#!/bin/sh
# Builds one Aura example into a debug APK. The debug keystore is local
# and is not a Play signature. x64 is linked only; this script does not install.
# Usage: scripts/build_android_app.sh examples/proof/main.nox proof [arm64|x64]
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
# shellcheck source=scripts/nox_root.sh
. "$ROOT/scripts/nox_root.sh"
NOX="$(nox_root "$ROOT")"
SKIA_PIN="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["skia"])' "$ROOT/toolchain/manifest.json")"
NDK_PIN="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["android"]["ndk"])' "$ROOT/toolchain/manifest.json")"
SKIA="${SKIA_SRC:-/tmp/skia-$SKIA_PIN-src}"
SRC="${1:?nox source}"
NAME="${2:?apk name}"
SLICE="${3:-arm64}"
if [ -n "${ANDROID_SDK_ROOT:-}" ]; then
  SDK_ROOT="$ANDROID_SDK_ROOT"
elif [ -n "${ANDROID_HOME:-}" ]; then
  SDK_ROOT="$ANDROID_HOME"
elif [ -d "$HOME/Library/Android/sdk" ]; then
  SDK_ROOT="$HOME/Library/Android/sdk"
else
  echo "ANDROID_SDK_ROOT is unset. Point it at the Android SDK." >&2
  exit 1
fi
NDK="${ANDROID_NDK_HOME:-$SDK_ROOT/ndk/$NDK_PIN}"
HOST_TAG=""
for tag in darwin-arm64 darwin-x86_64 linux-x86_64 windows-x86_64; do
  if [ -x "$NDK/toolchains/llvm/prebuilt/$tag/bin/clang" ]; then
    HOST_TAG="$tag"
    break
  fi
done
if [ -z "$HOST_TAG" ]; then
  echo "Android NDK $NDK_PIN has no LLVM prebuilt. Install that NDK or set ANDROID_NDK_HOME." >&2
  exit 1
fi
CLANG="$NDK/toolchains/llvm/prebuilt/$HOST_TAG/bin/clang"
CXX="$NDK/toolchains/llvm/prebuilt/$HOST_TAG/bin/clang++"
BUILD_TOOLS="$(ls -d "$SDK_ROOT"/build-tools/* | sort | tail -n 1)"
NOXC="$NOX/zig-out/bin/noxc"
case "$SLICE" in
  arm64)
    TRIPLE="aarch64-linux-android24"
    ABI="arm64-v8a"
    NOX_TARGET="android-arm64"
    RT="$NOX/zig-out/lib/noxrt-android-arm64.o"
    SKIA_OUT="$SKIA/out/android-arm64"
    CXX_LIB="$NDK/toolchains/llvm/prebuilt/$HOST_TAG/sysroot/usr/lib/aarch64-linux-android/libc++_shared.so"
    ;;
  x64)
    TRIPLE="x86_64-linux-android24"
    ABI="x86_64"
    NOX_TARGET="android-x64"
    RT="$NOX/zig-out/lib/noxrt-android-x64.o"
    SKIA_OUT="$SKIA/out/android-x64"
    CXX_LIB="$NDK/toolchains/llvm/prebuilt/$HOST_TAG/sysroot/usr/lib/x86_64-linux-android/libc++_shared.so"
    ;;
  *)
    echo "slice must be arm64 or x64" >&2
    exit 1
    ;;
esac
LIBSKIA="$SKIA_OUT/libskia.a"
OUT="$ROOT/build/android/$SLICE/$NAME"
if [ ! -x "$CLANG" ] || [ ! -f "$LIBSKIA" ] || [ ! -f "$RT" ] || [ ! -x "$NOXC" ]; then
  echo "android toolchain, skia, noxrt or noxc is missing" >&2
  exit 1
fi
rm -rf "$OUT"
mkdir -p "$OUT/obj" "$OUT/assets/tests/fixtures" "$OUT/assets/native/plugins" "$OUT/assets/native/fonts" "$OUT/lib/$ABI"
"$NOXC" build --target "$NOX_TARGET" --emit-obj -o "$OUT/obj/app.o" "$SRC"
"$CLANG" -c -fPIC -target "$TRIPLE" -I "$ROOT/native/mobile" -I "$ROOT/native/skia" -I "$ROOT/native/host" -I "$NOX/include" -o "$OUT/obj/shell.o" "$ROOT/native/mobile/shell.c"
"$CLANG" -c -fPIC -target "$TRIPLE" -I "$ROOT/native/mobile" -o "$OUT/obj/events.o" "$ROOT/native/mobile/events.c"
"$CLANG" -c -fPIC -target "$TRIPLE" -I "$ROOT/native/mobile" -o "$OUT/obj/event_queue.o" "$ROOT/native/mobile/event_queue.c"
"$CLANG" -c -fPIC -target "$TRIPLE" -I "$ROOT/native/mobile" -I "$NOX/include" -o "$OUT/obj/app_runtime.o" "$ROOT/native/mobile/app_runtime.c"
"$CLANG" -c -fPIC -target "$TRIPLE" -I "$ROOT/native/mobile" -o "$OUT/obj/storage.o" "$ROOT/native/mobile/storage.c"
"$CLANG" -c -fPIC -target "$TRIPLE" -I "$ROOT/native/mobile" -o "$OUT/obj/ax_platform.o" "$ROOT/native/mobile/ax_platform.c"
"$CLANG" -c -fPIC -target "$TRIPLE" -I "$ROOT/native/mobile" -o "$OUT/obj/android_surface.o" "$ROOT/native/mobile/android_surface.c"
"$CLANG" -c -fPIC -target "$TRIPLE" -I "$ROOT/native/host" -o "$OUT/obj/plugin.o" "$ROOT/native/host/plugin.c"
"$CLANG" -c -fPIC -target "$TRIPLE" -I "$ROOT/native/host" -o "$OUT/obj/listing.o" "$ROOT/native/host/listing.c"
"$CLANG" -c -fPIC -target "$TRIPLE" -I "$ROOT/native/mobile" -I "$ROOT/native/host" -I "$NOX/include" -o "$OUT/obj/host.o" "$ROOT/native/android/nox_host.c"
"$CXX" -std=c++17 -fPIC -c -target "$TRIPLE" -I "$SKIA" -I "$ROOT/native/skia" -I "$ROOT/native/mobile" -o "$OUT/obj/paint.o" "$ROOT/native/skia/paint.cc"
"$CLANG" -shared -fPIC -target "$TRIPLE" -o "$OUT/assets/native/plugins/libaura_probe.dylib" "$ROOT/native/plugins/probe.c"
cp "$ROOT/tests/fixtures/pixel.png" "$ROOT/tests/fixtures/pixel.jpg" "$OUT/assets/tests/fixtures/"
cp "$ROOT/native/fonts/NotoSans-Regular.ttf" "$OUT/assets/native/fonts/"
SO="$OUT/lib/$ABI/libnoxapp.so"
set +e
"$CLANG" -shared -fPIC -target "$TRIPLE" -Wl,-z,max-page-size=16384 -Wl,--no-undefined \
  -o "$SO" \
  "$OUT/obj/app.o" "$OUT/obj/shell.o" "$OUT/obj/events.o" "$OUT/obj/event_queue.o" "$OUT/obj/app_runtime.o" "$OUT/obj/storage.o" "$OUT/obj/ax_platform.o" "$OUT/obj/android_surface.o" "$OUT/obj/plugin.o" "$OUT/obj/listing.o" "$OUT/obj/host.o" "$OUT/obj/paint.o" "$RT" \
  -Wl,--start-group \
  "$LIBSKIA" \
  "$SKIA_OUT/libpng.a" \
  "$SKIA_OUT/libjpeg.a" \
  "$SKIA_OUT/libwebp.a" \
  "$SKIA_OUT/libzlib.a" \
  "$SKIA_OUT/libfreetype2.a" \
  "$SKIA_OUT/libskcms.a" \
  "$SKIA_OUT/libcpu-features.a" \
  -Wl,--end-group \
  -lm -ldl -llog -ljnigraphics -landroid -lc++_shared >"$OUT/link.log" 2>&1
LINK_STATUS=$?
set -e
if [ "$LINK_STATUS" -ne 0 ]; then
  if [ "$SLICE" = "x64" ] && grep -q '__tls_get_addr' "$OUT/link.log" && [ "$(grep -c 'undefined' "$OUT/link.log")" -eq 1 ]; then
    "$CLANG" -shared -fPIC -target "$TRIPLE" -Wl,-z,max-page-size=16384 \
      -o "$SO" \
      "$OUT/obj/app.o" "$OUT/obj/shell.o" "$OUT/obj/events.o" "$OUT/obj/event_queue.o" "$OUT/obj/app_runtime.o" "$OUT/obj/storage.o" "$OUT/obj/ax_platform.o" "$OUT/obj/android_surface.o" "$OUT/obj/plugin.o" "$OUT/obj/listing.o" "$OUT/obj/host.o" "$OUT/obj/paint.o" "$RT" \
      -Wl,--start-group \
      "$LIBSKIA" \
      "$SKIA_OUT/libpng.a" \
      "$SKIA_OUT/libjpeg.a" \
      "$SKIA_OUT/libwebp.a" \
      "$SKIA_OUT/libzlib.a" \
      "$SKIA_OUT/libfreetype2.a" \
      "$SKIA_OUT/libskcms.a" \
      "$SKIA_OUT/libcpu-features.a" \
      "$SKIA_OUT/liballocator_base.a" \
      "$SKIA_OUT/liballocator_core.a" \
      "$SKIA_OUT/liballocator_shim.a" \
      "$SKIA_OUT/libraw_ptr.a" \
      -Wl,--end-group \
      -lm -ldl -llog -ljnigraphics -landroid -lc++_shared
  else
    cat "$OUT/link.log" >&2
    exit "$LINK_STATUS"
  fi
fi
cp "$CXX_LIB" "$OUT/lib/$ABI/"
mkdir -p "$OUT/classes"
javac -source 17 -target 17 -classpath "$SDK_ROOT/platforms/android-36/android.jar" -d "$OUT/classes" "$ROOT/native/android/AuraHost.java"
jar cf "$OUT/classes.jar" -C "$OUT/classes" .
"$BUILD_TOOLS/d8" --min-api 24 --output "$OUT" "$OUT/classes.jar"
"$BUILD_TOOLS/aapt2" link -I "$SDK_ROOT/platforms/android-36/android.jar" \
  --manifest "$ROOT/native/android/AndroidManifest.xml" \
  -o "$OUT/unsigned.apk"
(
  cd "$OUT"
  zip -q -u unsigned.apk classes.dex
  zip -q -r -u unsigned.apk lib assets
)
"$BUILD_TOOLS/zipalign" -p -f 16 "$OUT/unsigned.apk" "$OUT/aligned.apk"
KEY="$ROOT/build/android/debug.keystore"
if [ ! -f "$KEY" ]; then
  keytool -genkeypair -keystore "$KEY" -storepass android -keypass android -alias androiddebugkey -keyalg RSA -keysize 2048 -validity 10000 -dname "CN=Aura Debug,O=Nox,C=TR"
fi
"$BUILD_TOOLS/apksigner" sign --ks "$KEY" --ks-pass pass:android --key-pass pass:android --out "$OUT/$NAME.apk" "$OUT/aligned.apk"
echo "$OUT/$NAME.apk"
