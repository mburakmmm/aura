#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
ROOT="$(pwd)"
ARCH="$(uname -m)"
OS="$(uname -s)"
SKIA_TAG="m151-a90155cff0"
case "$OS" in
  Darwin)
    case "$ARCH" in
      arm64) ASSET="Skia-macOS-Release-arm64.zip" ;;
      *) ASSET="Skia-macOS-Release-x64.zip" ;;
    esac
    ;;
  Linux)
    case "$ARCH" in
      x86_64) ASSET="Skia-Linux-Release-x64.zip" ;;
      i686|i386) ASSET="Skia-Linux-Release-x86.zip" ;;
      *) echo "desteklenmeyen linux mimarisi: $ARCH" >&2; exit 1 ;;
    esac
    ;;
  MINGW*|MSYS*|CYGWIN*)
    ASSET="Skia-Windows-Release-x64.zip"
    ;;
  *)
    echo "desteklenmeyen isletim sistemi: $OS" >&2
    exit 1
    ;;
esac
DEST="$ROOT/third_party/skia-m151"
if [ ! -d "$DEST/include/core" ]; then
  mkdir -p "$ROOT/third_party"
  ZIP="$ROOT/third_party/$ASSET"
  if [ ! -f "$ZIP" ]; then
    curl -L --fail -o "$ZIP" "https://github.com/aseprite/skia/releases/download/$SKIA_TAG/$ASSET"
  fi
  unzip -q -o "$ZIP" -d "$DEST"
fi
echo "skia hazir: $DEST"
