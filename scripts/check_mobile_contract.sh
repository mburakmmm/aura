#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
if command -v python3 >/dev/null 2>&1; then
  python3 "$root/scripts/check_mobile_contract.py" --self-test
  exec python3 "$root/scripts/check_mobile_contract.py" --root "$root"
fi
python "$root/scripts/check_mobile_contract.py" --self-test
exec python "$root/scripts/check_mobile_contract.py" --root "$root"
