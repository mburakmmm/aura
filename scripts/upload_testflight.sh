#!/bin/sh
# Uploads a signed IPA to TestFlight. The API key stays outside the repo.
# Required: AURA_IOS_API_KEY_ID, AURA_IOS_API_ISSUER, AURA_IOS_API_KEY (.p8 path).
set -eu
IPA="${1:?ipa}"
if [ ! -f "$IPA" ]; then
  echo "ipa yok: $IPA" >&2
  exit 2
fi
missing=""
for name in AURA_IOS_API_KEY_ID AURA_IOS_API_ISSUER AURA_IOS_API_KEY; do
  eval "value=\${$name:-}"
  if [ -z "$value" ]; then
    missing="$missing $name"
  fi
done
if [ -n "$missing" ]; then
  echo "TestFlight upload needs:$missing" >&2
  echo "The .p8 file is not stored in this repo." >&2
  exit 2
fi
if [ ! -f "$AURA_IOS_API_KEY" ]; then
  echo "api key yok: $AURA_IOS_API_KEY" >&2
  exit 2
fi
if xcrun --find iTMSTransporter >/dev/null 2>&1; then
  xcrun iTMSTransporter -m upload -assetFile "$IPA" -apiKey "$AURA_IOS_API_KEY_ID" -apiIssuer "$AURA_IOS_API_ISSUER" -apiKeyPath "$(dirname "$AURA_IOS_API_KEY")"
  exit 0
fi
xcrun altool --upload-app --type ios -f "$IPA" --apiKey "$AURA_IOS_API_KEY_ID" --apiIssuer "$AURA_IOS_API_ISSUER"
