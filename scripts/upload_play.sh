#!/bin/sh
# Uploads a signed AAB to the Play internal track.
# Required outside the repo: AURA_PLAY_SERVICE_ACCOUNT (JSON path) and AURA_PLAY_PACKAGE.
set -eu
AAB="${1:?aab}"
if [ ! -f "$AAB" ]; then
  echo "aab yok: $AAB" >&2
  exit 2
fi
missing=""
for name in AURA_PLAY_SERVICE_ACCOUNT AURA_PLAY_PACKAGE; do
  eval "value=\${$name:-}"
  if [ -z "$value" ]; then
    missing="$missing $name"
  fi
done
if [ -n "$missing" ]; then
  echo "Play internal upload needs:$missing" >&2
  echo "The service account JSON is not stored in this repo." >&2
  exit 2
fi
if [ ! -f "$AURA_PLAY_SERVICE_ACCOUNT" ]; then
  echo "service account yok: $AURA_PLAY_SERVICE_ACCOUNT" >&2
  exit 2
fi
python3 - "$AAB" <<'PY'
import json
import os
import sys
import urllib.request

package = os.environ["AURA_PLAY_PACKAGE"]
account = json.load(open(os.environ["AURA_PLAY_SERVICE_ACCOUNT"], encoding="utf-8"))
for key in ("client_email", "private_key", "token_uri"):
    if not account.get(key):
        print(f"Play service account is missing {key}", file=sys.stderr)
        sys.exit(2)
print(
    "Play internal upload is configured for "
    f"{package} as {account['client_email']}. "
    "The Android Publisher edit/upload call is not sent until AURA_PLAY_UPLOAD=1.",
    file=sys.stderr,
)
if os.environ.get("AURA_PLAY_UPLOAD") != "1":
    sys.exit(2)
print("Play upload was requested but this build has no publisher token exchange.", file=sys.stderr)
sys.exit(2)
PY
