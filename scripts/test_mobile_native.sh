#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
# shellcheck source=scripts/nox_root.sh
. "$root/scripts/nox_root.sh"
nox_include="$(nox_root "$root")/include"
compiler=${CC:-cc}
suffix=
case "$(uname -s)" in
  MINGW*|MSYS*|CYGWIN*) suffix=.exe ;;
esac
binary="${TMPDIR:-/tmp}/aura-mobile-events-$$${suffix}"
queue_binary="${TMPDIR:-/tmp}/aura-mobile-event-queue-$$${suffix}"
shell_binary="${TMPDIR:-/tmp}/aura-mobile-shell-queue-$$${suffix}"
runtime_binary="${TMPDIR:-/tmp}/aura-mobile-runtime-$$${suffix}"
surface_binary="${TMPDIR:-/tmp}/aura-android-surface-$$${suffix}"
storage_binary="${TMPDIR:-/tmp}/aura-mobile-storage-$$${suffix}"
text_binary="${TMPDIR:-/tmp}/aura-mobile-text-units-$$${suffix}"
trap 'rm -f "$binary" "$queue_binary" "$shell_binary" "$runtime_binary" "$surface_binary" "$storage_binary" "$text_binary"' EXIT HUP INT TERM
"$compiler" -std=c11 -Wall -Wextra -Werror \
  "$root/native/mobile/events.c" \
  "$root/native/mobile/events_test.c" \
  -o "$binary"
"$binary"
"$compiler" -std=c11 -Wall -Wextra -Werror -pthread \
  "$root/native/mobile/events.c" \
  "$root/native/mobile/event_queue.c" \
  "$root/native/mobile/event_queue_test.c" \
  -o "$queue_binary"
"$queue_binary"
"$compiler" -std=c11 -Wall -Wextra -Werror -pthread \
  -I "$root/native/mobile" \
  -I "$root/native/skia" \
  "$root/native/mobile/events.c" \
  "$root/native/mobile/event_queue.c" \
  "$root/native/mobile/shell.c" \
  "$root/native/mobile/ax_platform.c" \
  "$root/native/mobile/shell_test_stubs.c" \
  "$root/native/mobile/shell_queue_test.c" \
  -o "$shell_binary"
"$shell_binary"
"$compiler" -std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -pthread \
  -I "$root/native/mobile" \
  -I "$nox_include" \
  "$root/native/mobile/app_runtime.c" \
  "$root/native/mobile/storage.c" \
  "$root/native/mobile/app_runtime_test.c" \
  -o "$runtime_binary"
"$runtime_binary"
"$compiler" -std=c11 -Wall -Wextra -Werror \
  -I "$root/native/mobile" \
  "$root/native/mobile/events.c" \
  "$root/native/mobile/android_surface.c" \
  "$root/native/mobile/android_surface_test.c" \
  -o "$surface_binary"
"$surface_binary"
"$compiler" -std=c11 -Wall -Wextra -Werror \
  -I "$root/native/mobile" \
  "$root/native/mobile/storage.c" \
  "$root/native/mobile/storage_test.c" \
  -o "$storage_binary"
"$storage_binary"
"$compiler" -std=c11 -Wall -Wextra -Werror \
  -I "$root/native/mobile" \
  "$root/native/mobile/text_units.c" \
  "$root/native/mobile/text_units_test.c" \
  -o "$text_binary"
"$text_binary"
echo "mobile native contract: ok"
