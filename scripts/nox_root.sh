#!/bin/sh
# Resolves the Nox source tree. NOX_ROOT wins. Otherwise the sibling
# checkout that contains include/nox_embed.h is used. No personal path
# is written into this script.

nox_root() {
  repo=$1
  if [ -n "${NOX_ROOT:-}" ]; then
    if [ ! -f "$NOX_ROOT/include/nox_embed.h" ]; then
      echo "NOX_ROOT does not contain include/nox_embed.h" >&2
      return 1
    fi
    printf '%s\n' "$NOX_ROOT"
    return 0
  fi
  sibling=$(
    if CDPATH= cd -- "$repo/../nox-lang" 2>/dev/null; then
      pwd
    fi
  )
  if [ -n "$sibling" ] && [ -f "$sibling/include/nox_embed.h" ]; then
    printf '%s\n' "$sibling"
    return 0
  fi
  echo "NOX_ROOT is unset. Set it to the Nox source tree that contains include/nox_embed.h." >&2
  return 1
}
