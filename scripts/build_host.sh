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
    clang -fPIC -shared -o "$ROOT/native/plugins/libaura_probe.dylib" "$ROOT/native/plugins/probe.c"
    clang -fPIC -shared -o "$ROOT/native/plugins/libaura_probe_partial.dylib" "$ROOT/native/plugins/probe_partial.c"
    PROTO="$(pkg-config --variable=pkgdatadir wayland-protocols 2>/dev/null || echo /usr/share/wayland-protocols)"
    wayland-scanner client-header "$PROTO/stable/xdg-shell/xdg-shell.xml" "$ROOT/native/linux/xdg-shell-client-protocol.h"
    wayland-scanner private-code "$PROTO/stable/xdg-shell/xdg-shell.xml" "$ROOT/native/linux/xdg-shell-protocol.c"
    wayland-scanner client-header "$PROTO/unstable/text-input/text-input-unstable-v3.xml" "$ROOT/native/linux/text-input-unstable-v3-client-protocol.h"
    wayland-scanner private-code "$PROTO/unstable/text-input/text-input-unstable-v3.xml" "$ROOT/native/linux/text-input-unstable-v3-protocol.c"
    WL_CFLAGS="$(pkg-config --cflags wayland-client wayland-cursor xkbcommon x11 dbus-1)"
    clang -fPIC -c -I "$ROOT/native/host" -o "$ROOT/native/host/plugin.o" "$ROOT/native/host/plugin.c"
    clang -fPIC -c -I "$ROOT/native/host" -o "$ROOT/native/host/queue.o" "$ROOT/native/host/queue.c"
    # shellcheck disable=SC2086
    clang -fPIC -c $WL_CFLAGS -I "$ROOT/native/linux" -o "$ROOT/native/linux/xdg-shell-protocol.o" "$ROOT/native/linux/xdg-shell-protocol.c"
    # shellcheck disable=SC2086
    clang -fPIC -c $WL_CFLAGS -I "$ROOT/native/linux" -o "$ROOT/native/linux/text-input-unstable-v3-protocol.o" "$ROOT/native/linux/text-input-unstable-v3-protocol.c"
    clang++ -std=c++17 -fPIC -c \
      -I "$SKIA" -I "$ROOT/native/skia" -I "$ROOT/native/host" -I "$ROOT/native/linux" \
      $WL_CFLAGS \
      -o "$ROOT/native/linux/aura_linux.o" "$ROOT/native/linux/aura_linux.cc"
    clang++ -std=c++17 -fPIC -c -I "$SKIA" -I "$ROOT/native/skia" -o "$ROOT/native/skia/paint.o" "$ROOT/native/skia/paint.cc"
    # shellcheck disable=SC2086
    clang++ -fPIC -shared \
      -o "$OUT" \
      "$ROOT/native/linux/aura_linux.o" \
      "$ROOT/native/linux/xdg-shell-protocol.o" \
      "$ROOT/native/linux/text-input-unstable-v3-protocol.o" \
      "$ROOT/native/skia/paint.o" \
      "$ROOT/native/host/plugin.o" \
      "$ROOT/native/host/queue.o" \
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
      $(pkg-config --libs wayland-client wayland-cursor xkbcommon x11 dbus-1 fontconfig) \
      -lrt -ldl -lpthread -lm
    ;;
  MINGW*|MSYS*|CYGWIN*)
    win_path() {
      if command -v cygpath >/dev/null 2>&1; then
        cygpath -m "$1"
      else
        printf '%s\n' "$1"
      fi
    }
    WROOT="$(win_path "$ROOT")"
    WSKIA="$(win_path "$SKIA")"
    WLIBDIR="$(win_path "$LIBDIR")"
    WOUT="$(win_path "$OUT")"
    WDEF="$(win_path "$ROOT/native/windows/aura_host.def")"
    mkdir -p "$ROOT/native/build"
    WBUILD="$(win_path "$ROOT/native/build")/"
    if ! command -v cl >/dev/null 2>&1; then
      echo "Windows kabugu MSVC cl ister" >&2
      exit 1
    fi
    cl /nologo /LD "/Fe$(win_path "$ROOT/native/plugins/libaura_probe.dylib")" "/Fo$WBUILD" "$WROOT/native/plugins/probe.c"
    cl /nologo /LD "/Fe$(win_path "$ROOT/native/plugins/libaura_probe_partial.dylib")" "/Fo$WBUILD" "$WROOT/native/plugins/probe_partial.c"
    resolve_lib() {
      base="$1"
      if [ -f "$LIBDIR/${base}.lib" ]; then
        printf '%s.lib' "$base"
        return 0
      fi
      if [ -f "$LIBDIR/lib${base}.lib" ]; then
        printf 'lib%s.lib' "$base"
        return 0
      fi
      echo "skia kutuphanesi yok: $base" >&2
      ls -la "$LIBDIR" >&2 || true
      return 1
    }
    cl /nologo /std:c++20 /EHsc /MD /LD /utf-8 \
      "/I$WSKIA" "/I$WROOT/native/skia" "/I$WROOT/native/host" \
      "/Fo$WBUILD" "/Fe$WOUT" \
      "$WROOT/native/windows/aura_windows.cc" \
      "$WROOT/native/skia/paint.cc" \
      "$WROOT/native/host/plugin.c" \
      "$WROOT/native/host/queue.c" \
      /link "/DEF:$WDEF" "/LIBPATH:$WLIBDIR" \
      "$(resolve_lib skia)" \
      "$(resolve_lib png)" \
      "$(resolve_lib jpeg)" \
      "$(resolve_lib webp)" \
      "$(resolve_lib zlib)" \
      "$(resolve_lib freetype2)" \
      "$(resolve_lib harfbuzz)" \
      "$(resolve_lib icu)" \
      "$(resolve_lib expat)" \
      "$(resolve_lib skunicode_icu)" \
      "$(resolve_lib skunicode_core)" \
      "$(resolve_lib skcms)" \
      "$(resolve_lib skshaper)" \
      ole32.lib oleaut32.lib uuid.lib user32.lib gdi32.lib dwmapi.lib shell32.lib advapi32.lib \
      uiautomationcore.lib dwrite.lib windowscodecs.lib shlwapi.lib
    ;;
  *)
    echo "desteklenmeyen isletim sistemi: $OS" >&2
    exit 1
    ;;
esac
echo "kabuk hazir: $OUT"
