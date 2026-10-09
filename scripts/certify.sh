#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
ROOT="$(pwd)"
case "$(uname -s)" in
  MINGW*|MSYS*|CYGWIN*)
    export PATH="$ROOT/native:$PATH"
    ;;
esac
./scripts/test.sh
noxc run examples/counter/main.nox -- --self-check
noxc run examples/rectangle/main.nox -- --self-check
noxc run examples/todo/main.nox -- --self-check
noxc run examples/calculator/main.nox -- --self-check
noxc run examples/proof/main.nox -- --self-check
noxc run examples/notes/main.nox -- --self-check
noxc run examples/settings/main.nox -- --self-check
noxc run examples/chat/main.nox -- --self-check
noxc run examples/files/main.nox -- --self-check
