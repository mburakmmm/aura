#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
./scripts/check_mobile_contract.sh
./scripts/check_mobile_package.sh
./scripts/test_mobile_native.sh
./scripts/test_mobile_native_sanitized.sh
./scripts/build_host.sh
noxc test
