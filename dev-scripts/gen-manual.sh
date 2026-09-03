#!/usr/bin/env bash
set -euo pipefail

# run from repo root
cd "$(dirname "$0")/.."

# MANUAL.md is derived from the built binary.
if [ ! -f build/CMakeCache.txt ]; then
  cmake -S . -B build >&2
fi
cmake --build build -j >&2

build/apb --dump-manual MANUAL.md

echo "wrote MANUAL.md ($(wc -l < MANUAL.md | tr -d ' ') lines)"
