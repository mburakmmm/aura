#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
./scripts/build_host.sh
noxc run benchmarks/suite.nox
