#!/bin/sh
set -eu
case "$(uname -s)" in
  MINGW*|MSYS*|CYGWIN*)
    echo "mobile native sanitizer: skipped on Windows"
    exit 0
    ;;
esac
root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
# shellcheck source=scripts/nox_root.sh
. "$root/scripts/nox_root.sh"
nox_include="$(nox_root "$root")/include"
compiler=${AURA_SANITIZER_CC:-clang}
if ! command -v "$compiler" >/dev/null 2>&1; then
  echo "mobile native sanitizer: skipped; clang is unavailable"
  exit 0
fi
queue_binary="${TMPDIR:-/tmp}/aura-mobile-event-queue-asan-$$"
shell_binary="${TMPDIR:-/tmp}/aura-mobile-shell-asan-$$"
runtime_binary="${TMPDIR:-/tmp}/aura-mobile-runtime-asan-$$"
surface_binary="${TMPDIR:-/tmp}/aura-android-surface-asan-$$"
storage_binary="${TMPDIR:-/tmp}/aura-mobile-storage-asan-$$"
text_binary="${TMPDIR:-/tmp}/aura-mobile-text-units-asan-$$"
trap 'rm -f "$queue_binary" "$shell_binary" "$runtime_binary" "$surface_binary" "$storage_binary" "$text_binary"' EXIT HUP INT TERM
flags="-std=c11 -Wall -Wextra -Werror -pthread -fsanitize=address,undefined -fno-omit-frame-pointer"
# shellcheck disable=SC2086
"$compiler" $flags \
  "$root/native/mobile/events.c" \
  "$root/native/mobile/event_queue.c" \
  "$root/native/mobile/event_queue_test.c" \
  -o "$queue_binary"
# shellcheck disable=SC2086
"$compiler" $flags \
  -I "$root/native/mobile" \
  -I "$root/native/skia" \
  "$root/native/mobile/events.c" \
  "$root/native/mobile/event_queue.c" \
  "$root/native/mobile/shell.c" \
  "$root/native/mobile/ax_platform.c" \
  "$root/native/mobile/shell_test_stubs.c" \
  "$root/native/mobile/shell_queue_test.c" \
  -o "$shell_binary"
# shellcheck disable=SC2086
"$compiler" $flags -D_DEFAULT_SOURCE \
  -I "$root/native/mobile" \
  -I "$nox_include" \
  "$root/native/mobile/app_runtime.c" \
  "$root/native/mobile/storage.c" \
  "$root/native/mobile/app_runtime_test.c" \
  -o "$runtime_binary"
case "$(uname -s)" in
  Linux*) leak_option=1 ;;
  *) leak_option=0 ;;
esac
ASAN_OPTIONS="detect_leaks=$leak_option" UBSAN_OPTIONS=halt_on_error=1 "$queue_binary"
ASAN_OPTIONS="detect_leaks=$leak_option" UBSAN_OPTIONS=halt_on_error=1 "$shell_binary"
# shellcheck disable=SC2086
"$compiler" $flags \
  -I "$root/native/mobile" \
  "$root/native/mobile/events.c" \
  "$root/native/mobile/android_surface.c" \
  "$root/native/mobile/android_surface_test.c" \
  -o "$surface_binary"
ASAN_OPTIONS="detect_leaks=$leak_option" UBSAN_OPTIONS=halt_on_error=1 "$runtime_binary"
ASAN_OPTIONS="detect_leaks=$leak_option" UBSAN_OPTIONS=halt_on_error=1 "$surface_binary"
# shellcheck disable=SC2086
"$compiler" $flags \
  -I "$root/native/mobile" \
  "$root/native/mobile/storage.c" \
  "$root/native/mobile/storage_test.c" \
  -o "$storage_binary"
ASAN_OPTIONS="detect_leaks=$leak_option" UBSAN_OPTIONS=halt_on_error=1 "$storage_binary"
# shellcheck disable=SC2086
"$compiler" $flags \
  -I "$root/native/mobile" \
  "$root/native/mobile/text_units.c" \
  "$root/native/mobile/text_units_test.c" \
  -o "$text_binary"
ASAN_OPTIONS="detect_leaks=$leak_option" UBSAN_OPTIONS=halt_on_error=1 "$text_binary"
echo "mobile native sanitizer: ok"
