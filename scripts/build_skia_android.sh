#!/bin/sh
# Builds Skia m151 as a CPU raster static library for Android.
# Usage: scripts/build_skia_android.sh [arm64|x64]
# Output: ${SKIA_SRC:-/tmp/skia-<pin>-src}/out/android-<cpu>/libskia.a
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CPU="${1:-arm64}"
PIN="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["skia"])' "$ROOT/toolchain/manifest.json")"
NDK_PIN="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["android"]["ndk"])' "$ROOT/toolchain/manifest.json")"
MIN_SDK="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["android"]["min_sdk"])' "$ROOT/toolchain/manifest.json")"
SRC="${SKIA_SRC:-/tmp/skia-$PIN-src}"
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
if [ ! -d "$NDK" ]; then
  echo "Android NDK yok: $NDK" >&2
  exit 1
fi
case "$CPU" in
  arm64) ;;
  x64) ;;
  *)
    echo "cpu must be arm64 or x64" >&2
    exit 2
    ;;
esac
if [ ! -d "$SRC/.git" ]; then
  git clone --depth 1 --branch "chrome/$PIN" https://skia.googlesource.com/skia.git "$SRC"
fi
cd "$SRC"
if [ ! -x bin/gn ]; then
  python3 bin/fetch-gn
fi
if [ ! -f third_party/externals/libpng/png.h ]; then
  python3 tools/git-sync-deps
fi
mkdir -p "out/android-$CPU"
cat > "out/android-$CPU/args.gn" <<EOF
is_official_build = true
is_debug = false
target_os = "android"
target_cpu = "$CPU"
ndk = "$NDK"
ndk_api = $MIN_SDK
skia_enable_ganesh = false
skia_enable_graphite = false
skia_use_metal = false
skia_use_gl = false
skia_use_vulkan = false
skia_use_dawn = false
skia_use_angle = false
skia_use_egl = false
skia_use_fonthost_mac = false
skia_use_freetype = true
skia_use_fontconfig = false
skia_enable_fontmgr_custom_empty = true
skia_use_expat = false
skia_use_dng_sdk = false
skia_use_piex = false
skia_use_wuffs = false
skia_use_libjxl_decode = false
skia_use_libavif = false
skia_enable_pdf = false
skia_enable_skottie = false
skia_enable_svg = false
skia_use_icu = true
skia_use_harfbuzz = true
skia_use_system_libpng = false
skia_use_system_libjpeg_turbo = false
skia_use_system_libwebp = false
skia_use_system_zlib = false
skia_use_system_icu = false
skia_use_system_harfbuzz = false
skia_use_system_freetype2 = false
skia_use_partition_alloc = false
EOF
bin/gn gen "out/android-$CPU"
ninja -C "out/android-$CPU" skia
test -f "out/android-$CPU/libskia.a"
echo "$SRC/out/android-$CPU/libskia.a"
