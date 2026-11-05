#!/bin/sh
# Builds Skia m151 as a CPU raster static library for iphoneos arm64.
# Output: ${SKIA_SRC:-/tmp/skia-m151-src}/out/ios-arm64/libskia.a
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PIN="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["skia"])' "$ROOT/toolchain/manifest.json")"
SRC="${SKIA_SRC:-/tmp/skia-$PIN-src}"
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
mkdir -p out/ios-arm64
cat > out/ios-arm64/args.gn <<'EOF'
is_official_build = true
is_debug = false
target_os = "ios"
target_cpu = "arm64"
ios_min_target = "17.0"
skia_enable_ganesh = false
skia_enable_graphite = false
skia_use_metal = false
skia_use_gl = false
skia_use_vulkan = false
skia_use_dawn = false
skia_use_angle = false
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
extra_cflags = ["-mios-version-min=17.0"]
EOF
bin/gn gen out/ios-arm64
ninja -C out/ios-arm64 skia
test -f out/ios-arm64/libskia.a
echo "$SRC/out/ios-arm64/libskia.a"
