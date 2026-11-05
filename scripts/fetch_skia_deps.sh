#!/bin/sh
set -eu
ROOT="/tmp/skia-m151-src"
DEST="$ROOT/third_party/externals"
mkdir -p "$DEST"

pin() {
  name="$1"
  url="$2"
  rev="$3"
  dir="$DEST/$name"
  if [ -d "$dir/.git" ]; then
    echo "var: $name"
    return 0
  fi
  rm -rf "$dir"
  mkdir -p "$dir"
  git -C "$dir" init -q
  git -C "$dir" remote add origin "$url"
  git -C "$dir" fetch --depth 1 origin "$rev"
  git -C "$dir" checkout --detach FETCH_HEAD
  echo "aldi: $name"
}

pin expat "https://chromium.googlesource.com/external/github.com/libexpat/libexpat.git" 6154446fccefbf3ca644894f598969113b0c7bcd
pin freetype "https://chromium.googlesource.com/chromium/src/third_party/freetype2.git" 264b5fbf5b912b39f98d038bf75d39be0a73f21b
pin harfbuzz "https://chromium.googlesource.com/external/github.com/harfbuzz/harfbuzz.git" 9cb1fee51069b206effb4736e443b038d230789d
pin icu "https://chromium.googlesource.com/chromium/deps/icu.git" d578f2e8b7bd5938e21cfb6bf15c079e0aa5b738
pin libjpeg-turbo "https://chromium.googlesource.com/chromium/deps/libjpeg_turbo.git" e14cbfaa85529d47f9f55b0f104a579c1061f9ad
pin libpng "https://skia.googlesource.com/third_party/libpng.git" d5515b5b8be3901aac04e5bd8bd5c89f287bcd33
pin libwebp "https://chromium.googlesource.com/webm/libwebp.git" 845d5476a866141ba35ac133f856fa62f0b7445f
pin zlib "https://chromium.googlesource.com/chromium/src/third_party/zlib" 646b7f569718921d7d4b5b8e22572ff6c76f2596
echo DEPS_OK
