#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
python=python3
if ! command -v python3 >/dev/null 2>&1; then
  python=python
fi
"$python" "$root/scripts/mobile_package.py" --self-test
"$python" "$root/scripts/app_icon.py" --self-test
"$python" "$root/scripts/aura_cmd.py" --self-test
"$python" "$root/scripts/ios_project.py" --self-test
"$python" "$root/scripts/android_project.py" --self-test
exec "$python" "$root/scripts/mobile_package.py" --check --root "$root"
