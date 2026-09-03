#!/usr/bin/env bash
set -euo pipefail
# Every path below is repo-relative, so run from the root wherever we are called
# from — the pre-commit hook, or by hand from inside dev-scripts/.
cd "$(dirname "$0")/.."

# MANUAL.md is derived from the built binary — it embeds the live command
# registry's Command Reference table, so unlike the old README generation
# this can't be done by just parsing source text; apb has to actually build
# and run. get_manual() already embeds its own "generated file" banner, so
# --dump-manual's output can be written straight to MANUAL.md.
if [ ! -f build/CMakeCache.txt ]; then
  cmake -S . -B build >&2
fi
cmake --build build -j >&2

build/apb --dump-manual MANUAL.md

echo "wrote MANUAL.md ($(wc -l < MANUAL.md | tr -d ' ') lines)"
