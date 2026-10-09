#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
./scripts/test.sh
noxc run examples/counter/main.nox -- --self-check
noxc run examples/rectangle/main.nox -- --self-check
noxc run examples/todo/main.nox -- --self-check
noxc run examples/calculator/main.nox -- --self-check
noxc run examples/proof/main.nox -- --self-check
