#!/bin/sh
# Packages one Aura example with Gradle. The device ABI is arm64-v8a.
# Usage: scripts/build_android_gradle.sh examples/notes/main.nox Notes [debug|release]
# Release signing comes from AURA_ANDROID_STORE_FILE and the matching passwords.
# AURA_ANDROID_LOCAL_RELEASE=1 writes a local upload key under build/ and is not a Play key.
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
# shellcheck source=scripts/nox_root.sh
. "$ROOT/scripts/nox_root.sh"
NOX="$(nox_root "$ROOT")"
SKIA_PIN="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["skia"])' "$ROOT/toolchain/manifest.json")"
NDK_PIN="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["android"]["ndk"])' "$ROOT/toolchain/manifest.json")"
SKIA="${SKIA_SRC:-/tmp/skia-$SKIA_PIN-src}"
SRC="${1:?nox source}"
NAME="${2:?app name}"
KIND="${3:-debug}"
case "$KIND" in
  debug|release) ;;
  *)
    echo "kind must be debug or release" >&2
    exit 2
    ;;
esac
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
  echo "Android NDK $NDK_PIN has no LLVM prebuilt." >&2
  exit 1
fi
PREBUILT="$NDK/toolchains/llvm/prebuilt/$HOST_TAG"
CLANG="$PREBUILT/bin/clang"
CXX="$PREBUILT/bin/clang++"
READELF="$PREBUILT/bin/llvm-readelf"
STRIP="$PREBUILT/bin/llvm-strip"
TRIPLE="aarch64-linux-android24"
ABI="arm64-v8a"
NOXC="$NOX/zig-out/bin/noxc"
RT="$NOX/zig-out/lib/noxrt-android-arm64.o"
SKIA_OUT="$SKIA/out/android-arm64"
LIBSKIA="$SKIA_OUT/libskia.a"
CXX_LIB="$PREBUILT/sysroot/usr/lib/aarch64-linux-android/libc++_shared.so"
if [ ! -x "$CLANG" ] || [ ! -f "$LIBSKIA" ] || [ ! -f "$RT" ] || [ ! -x "$NOXC" ]; then
  echo "android toolchain, skia, noxrt or noxc is missing" >&2
  exit 1
fi
STAGE="$ROOT/build/android/gradle/$NAME/stage"
JNI="$STAGE/jni"
ASSETS="$STAGE/assets"
SYMBOLS="$ROOT/build/android/symbols/$NAME/$ABI"
rm -rf "$STAGE"
mkdir -p "$JNI/$ABI" "$ASSETS/tests/fixtures" "$ASSETS/native/plugins" "$ASSETS/native/fonts" "$STAGE/obj" "$SYMBOLS"
"$NOXC" build --target android-arm64 --emit-obj -o "$STAGE/obj/app.o" "$SRC"
"$CLANG" -c -fPIC -target "$TRIPLE" -I "$ROOT/native/mobile" -I "$ROOT/native/skia" -I "$ROOT/native/host" -I "$NOX/include" -o "$STAGE/obj/shell.o" "$ROOT/native/mobile/shell.c"
"$CLANG" -c -fPIC -target "$TRIPLE" -I "$ROOT/native/mobile" -o "$STAGE/obj/events.o" "$ROOT/native/mobile/events.c"
"$CLANG" -c -fPIC -target "$TRIPLE" -I "$ROOT/native/mobile" -o "$STAGE/obj/event_queue.o" "$ROOT/native/mobile/event_queue.c"
"$CLANG" -c -fPIC -target "$TRIPLE" -I "$ROOT/native/mobile" -I "$NOX/include" -o "$STAGE/obj/app_runtime.o" "$ROOT/native/mobile/app_runtime.c"
"$CLANG" -c -fPIC -target "$TRIPLE" -I "$ROOT/native/mobile" -o "$STAGE/obj/storage.o" "$ROOT/native/mobile/storage.c"
"$CLANG" -c -fPIC -target "$TRIPLE" -I "$ROOT/native/mobile" -o "$STAGE/obj/ax_platform.o" "$ROOT/native/mobile/ax_platform.c"
"$CLANG" -c -fPIC -target "$TRIPLE" -I "$ROOT/native/mobile" -o "$STAGE/obj/android_surface.o" "$ROOT/native/mobile/android_surface.c"
"$CLANG" -c -fPIC -target "$TRIPLE" -I "$ROOT/native/host" -o "$STAGE/obj/plugin.o" "$ROOT/native/host/plugin.c"
"$CLANG" -c -fPIC -target "$TRIPLE" -I "$ROOT/native/host" -o "$STAGE/obj/listing.o" "$ROOT/native/host/listing.c"
"$CLANG" -c -fPIC -target "$TRIPLE" -I "$ROOT/native/mobile" -I "$ROOT/native/host" -I "$NOX/include" -o "$STAGE/obj/host.o" "$ROOT/native/android/nox_host.c"
"$CXX" -std=c++17 -fPIC -c -target "$TRIPLE" -I "$SKIA" -I "$ROOT/native/skia" -I "$ROOT/native/mobile" -o "$STAGE/obj/paint.o" "$ROOT/native/skia/paint.cc"
SO="$JNI/$ABI/libnoxapp.so"
"$CLANG" -shared -fPIC -target "$TRIPLE" -Wl,-z,max-page-size=16384 -Wl,--no-undefined \
  -o "$SO" \
  "$STAGE/obj/app.o" "$STAGE/obj/shell.o" "$STAGE/obj/events.o" "$STAGE/obj/event_queue.o" "$STAGE/obj/app_runtime.o" "$STAGE/obj/storage.o" "$STAGE/obj/ax_platform.o" "$STAGE/obj/android_surface.o" "$STAGE/obj/plugin.o" "$STAGE/obj/listing.o" "$STAGE/obj/host.o" "$STAGE/obj/paint.o" "$RT" \
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
  -lm -ldl -llog -ljnigraphics -landroid -lc++_shared
PROBE="$JNI/$ABI/libaura_probe.so"
"$CLANG" -shared -fPIC -target "$TRIPLE" -Wl,-z,max-page-size=16384 -o "$PROBE" "$ROOT/native/plugins/probe.c"
cp "$CXX_LIB" "$JNI/$ABI/libc++_shared.so"
cp "$PROBE" "$ASSETS/native/plugins/libaura_probe.dylib"
cp "$ROOT/tests/fixtures/pixel.png" "$ROOT/tests/fixtures/pixel.jpg" "$ASSETS/tests/fixtures/"
cp "$ROOT/native/fonts/NotoSans-Regular.ttf" "$ASSETS/native/fonts/"
cp "$SO" "$PROBE" "$JNI/$ABI/libc++_shared.so" "$SYMBOLS/"
if [ "$KIND" = "release" ]; then
  "$STRIP" --strip-unneeded "$SO" "$PROBE" "$JNI/$ABI/libc++_shared.so"
  cp "$PROBE" "$ASSETS/native/plugins/libaura_probe.dylib"
fi
python3 "$ROOT/scripts/android_project.py" align --readelf "$READELF" \
  --lib "$SO" --lib "$PROBE" --lib "$JNI/$ABI/libc++_shared.so" --lib "$ASSETS/native/plugins/libaura_probe.dylib"
PROJECT="$ROOT/build/android/gradle/$NAME/project"
rm -rf "$PROJECT"
python3 "$ROOT/scripts/android_project.py" generate --root "$ROOT" --name "$NAME" --out "$PROJECT"
if [ "$KIND" = "release" ] && [ -z "${AURA_ANDROID_STORE_FILE:-}" ]; then
  if [ "${AURA_ANDROID_LOCAL_RELEASE:-}" != "1" ]; then
    echo "Release signing needs AURA_ANDROID_STORE_FILE, AURA_ANDROID_STORE_PASSWORD, AURA_ANDROID_KEY_ALIAS and AURA_ANDROID_KEY_PASSWORD." >&2
    echo "Those values stay outside the repo. AURA_ANDROID_LOCAL_RELEASE=1 creates a local upload key under build/ and is not a Play key." >&2
    exit 2
  fi
  : "${AURA_ANDROID_STORE_PASSWORD:?release password is required}"
  : "${AURA_ANDROID_KEY_PASSWORD:?release key password is required}"
  : "${AURA_ANDROID_KEY_ALIAS:?release key alias is required}"
  STORE="$ROOT/build/android/upload.keystore"
  if [ ! -f "$STORE" ]; then
    keytool -genkeypair -keystore "$STORE" -storepass:env AURA_ANDROID_STORE_PASSWORD -keypass:env AURA_ANDROID_KEY_PASSWORD -alias "$AURA_ANDROID_KEY_ALIAS" -keyalg RSA -keysize 2048 -validity 10000 -dname "CN=Aura Upload,O=Nox,C=TR"
  fi
  export AURA_ANDROID_STORE_FILE="$STORE"
fi
if [ -n "${AURA_JAVA_HOME:-}" ]; then
  export JAVA_HOME="$AURA_JAVA_HOME"
elif [ -x "/Applications/Android Studio.app/Contents/jbr/Contents/Home/bin/java" ]; then
  export JAVA_HOME="/Applications/Android Studio.app/Contents/jbr/Contents/Home"
fi
GRADLE_VERSION="$(python3 -c 'import re,sys; print(re.search(r"aura.gradle=(.*)", open(sys.argv[1]).read()).group(1))' "$PROJECT/gradle.properties")"
DIST="$ROOT/build/android/dist/gradle-$GRADLE_VERSION"
if [ ! -x "$DIST/bin/gradle" ]; then
  mkdir -p "$ROOT/build/android/dist"
  ARCHIVE="$ROOT/build/android/dist/gradle-$GRADLE_VERSION-bin.zip"
  curl -L --fail -o "$ARCHIVE" "https://services.gradle.org/distributions/gradle-$GRADLE_VERSION-bin.zip"
  unzip -q -o "$ARCHIVE" -d "$ROOT/build/android/dist"
fi
export ANDROID_HOME="$SDK_ROOT"
cd "$PROJECT"
if [ "$KIND" = "debug" ]; then
  "$DIST/bin/gradle" --no-daemon assembleDebug -Paura.repo="$ROOT" -Paura.jni="$JNI" -Paura.assets="$ASSETS"
  APK="$PROJECT/app/build/outputs/apk/debug/app-debug.apk"
else
  "$DIST/bin/gradle" --no-daemon bundleRelease -Paura.repo="$ROOT" -Paura.jni="$JNI" -Paura.assets="$ASSETS"
  AAB="$PROJECT/app/build/outputs/bundle/release/app-release.aab"
  cp "$AAB" "$ROOT/build/android/gradle/$NAME/$NAME.aab"
  if [ -f "$PROJECT/app/build/outputs/mapping/release/mapping.txt" ]; then
    cp "$PROJECT/app/build/outputs/mapping/release/mapping.txt" "$ROOT/build/android/symbols/$NAME/mapping.txt"
  fi
  echo "$ROOT/build/android/gradle/$NAME/$NAME.aab"
  exit 0
fi
cp "$APK" "$ROOT/build/android/gradle/$NAME/$NAME.apk"
echo "$ROOT/build/android/gradle/$NAME/$NAME.apk"
