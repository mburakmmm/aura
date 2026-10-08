#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
ROOT="$(pwd)"
./scripts/build_skia.sh
SKIA="$ROOT/third_party/skia-m151"
LIBDIR="$(find "$SKIA" -name libskia.a -o -name skia.lib | head -n 1)"
if [ -z "$LIBDIR" ]; then
  echo "libskia bulunamadi" >&2
  exit 1
fi
LIBDIR="$(dirname "$LIBDIR")"
OS="$(uname -s)"
OUT="$ROOT/native/libaura_host.dylib"
mkdir -p "$ROOT/native/plugins"
case "$OS" in
  Darwin)
    clang -fno-objc-arc -dynamiclib -o "$ROOT/native/plugins/libaura_probe.dylib" "$ROOT/native/plugins/probe.c"
    clang -fno-objc-arc -dynamiclib -o "$ROOT/native/plugins/libaura_probe_partial.dylib" "$ROOT/native/plugins/probe_partial.c"
    clang -fno-objc-arc -c -I "$ROOT/native/skia" -I "$ROOT/native/host" -o "$ROOT/native/macos/aura_macos.o" "$ROOT/native/macos/aura_macos.m"
    clang -c -I "$ROOT/native/host" -o "$ROOT/native/host/plugin.o" "$ROOT/native/host/plugin.c"
    clang++ -std=c++17 -c -I "$SKIA" -I "$ROOT/native/skia" -o "$ROOT/native/skia/paint.o" "$ROOT/native/skia/paint.cc"
    clang++ -fno-objc-arc -dynamiclib \
      -framework Cocoa -framework CoreGraphics -framework CoreVideo -framework CoreText -framework Metal -framework Foundation -framework QuartzCore \
      -install_name "$OUT" \
      -o "$OUT" \
      "$ROOT/native/macos/aura_macos.o" \
      "$ROOT/native/skia/paint.o" \
      "$ROOT/native/host/plugin.o" \
      "$LIBDIR/libskia.a" \
      "$LIBDIR/libpng.a" \
      "$LIBDIR/libjpeg.a" \
      "$LIBDIR/libwebp.a" \
      "$LIBDIR/libzlib.a" \
      "$LIBDIR/libfreetype2.a" \
      "$LIBDIR/libharfbuzz.a" \
      "$LIBDIR/libicu.a" \
      "$LIBDIR/libexpat.a" \
      "$LIBDIR/libskunicode_icu.a" \
      "$LIBDIR/libskunicode_core.a" \
      "$LIBDIR/libskcms.a" \
      "$LIBDIR/libskshaper.a"
    ;;
  Linux)
    clang -fPIC -shared -o "$ROOT/native/plugins/libaura_probe.so" "$ROOT/native/plugins/probe.c"
    clang -fPIC -shared -o "$ROOT/native/plugins/libaura_probe_partial.so" "$ROOT/native/plugins/probe_partial.c"
    PROTO="$(pkg-config --variable=pkgdatadir wayland-protocols 2>/dev/null || echo /usr/share/wayland-protocols)"
    wayland-scanner client-header "$PROTO/stable/xdg-shell/xdg-shell.xml" "$ROOT/native/linux/xdg-shell-client-protocol.h"
    wayland-scanner private-code "$PROTO/stable/xdg-shell/xdg-shell.xml" "$ROOT/native/linux/xdg-shell-protocol.c"
    wayland-scanner client-header "$PROTO/unstable/text-input/text-input-unstable-v3.xml" "$ROOT/native/linux/text-input-unstable-v3-client-protocol.h"
    wayland-scanner private-code "$PROTO/unstable/text-input/text-input-unstable-v3.xml" "$ROOT/native/linux/text-input-unstable-v3-protocol.c"
    clang++ -std=c++17 -fPIC -shared \
      -I "$SKIA" -I "$ROOT/native/skia" -I "$ROOT/native/host" -I "$ROOT/native/linux" \
      $(pkg-config --cflags wayland-client wayland-cursor xkbcommon x11 dbus-1) \
      -o "$OUT" \
      "$ROOT/native/linux/aura_linux.cc" \
      "$ROOT/native/linux/xdg-shell-protocol.c" \
      "$ROOT/native/linux/text-input-unstable-v3-protocol.c" \
      "$ROOT/native/skia/paint.cc" \
      "$ROOT/native/host/plugin.c" \
      "$ROOT/native/host/queue.c" \
      -Wl,--start-group \
      "$LIBDIR/libskia.a" \
      "$LIBDIR/libpng.a" \
      "$LIBDIR/libjpeg.a" \
      "$LIBDIR/libwebp.a" \
      "$LIBDIR/libzlib.a" \
      "$LIBDIR/libfreetype2.a" \
      "$LIBDIR/libharfbuzz.a" \
      "$LIBDIR/libicu.a" \
      "$LIBDIR/libexpat.a" \
      "$LIBDIR/libskunicode_icu.a" \
      "$LIBDIR/libskunicode_core.a" \
      "$LIBDIR/libskcms.a" \
      "$LIBDIR/libskshaper.a" \
      -Wl,--end-group \
      $(pkg-config --libs wayland-client wayland-cursor xkbcommon x11 dbus-1) \
      -lrt -ldl -lpthread -lm
    ;;
  MINGW*|MSYS*|CYGWIN*)
    clang -shared -o "$ROOT/native/plugins/libaura_probe.dll" "$ROOT/native/plugins/probe.c"
    clang -shared -o "$ROOT/native/plugins/libaura_probe_partial.dll" "$ROOT/native/plugins/probe_partial.c"
    clang++ -std=c++17 -shared \
      -I "$SKIA" -I "$ROOT/native/skia" -I "$ROOT/native/host" \
      -o "$OUT" \
      "$ROOT/native/windows/aura_windows.cc" \
      "$ROOT/native/skia/paint.cc" \
      "$ROOT/native/host/plugin.c" \
      "$ROOT/native/host/queue.c" \
      "$LIBDIR"/*.lib \
      -lole32 -loleaut32 -luuid -luser32 -lgdi32 -ldwmapi -lshell32 -ladvapi32 -luiautomationcore
    ;;
  *)
    echo "desteklenmeyen isletim sistemi: $OS" >&2
    exit 1
    ;;
esac
echo "kabuk hazir: $OUT"
