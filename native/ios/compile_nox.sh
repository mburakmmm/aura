#!/bin/sh
# Compiles one Aura Nox program to an iOS object before the app links.
# Xcode exports NOX_ROOT, AURA_APP_SOURCE, PLATFORM_NAME and BUILT_PRODUCTS_DIR.
set -eu
if [ -z "${NOX_ROOT:-}" ] || [ -z "${AURA_APP_SOURCE:-}" ] || [ -z "${BUILT_PRODUCTS_DIR:-}" ]; then
  echo "NOX_ROOT, AURA_APP_SOURCE and BUILT_PRODUCTS_DIR are required" >&2
  exit 1
fi
case "${PLATFORM_NAME:-}" in
  iphonesimulator) slice=ios-simulator-arm64 ;;
  iphoneos) slice=ios-arm64 ;;
  *)
    echo "unsupported platform: ${PLATFORM_NAME:-unset}" >&2
    exit 1
    ;;
esac
compiler="$NOX_ROOT/zig-out/bin/noxc"
if [ ! -x "$compiler" ]; then
  echo "noxc yok: $compiler" >&2
  exit 1
fi
"$compiler" build --target "$slice" --emit-obj -o "$BUILT_PRODUCTS_DIR/app.o" "$AURA_APP_SOURCE"
