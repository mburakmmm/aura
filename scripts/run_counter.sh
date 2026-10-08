#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
./scripts/build_macos_bridge.sh
noxc run examples/counter/main.nox
