#!/usr/bin/env bash
set -euo pipefail
# Every path below is repo-relative, so run from the root wherever we are called
# from — the pre-commit hook, or by hand from inside dev-scripts/.
cd "$(dirname "$0")/.."

# README.md is derived; the raw string in src/app/readme.cpp is the source of
# truth, so that the shipped binary and the repo readme cannot disagree.
# Clearing the flag on the closing delimiter before the bare `f`, and setting it
# on the opening delimiter after, keeps both delimiter lines out of the output.
tmp=$(mktemp)
trap 'rm -f "$tmp"' EXIT
awk '/^\)md";$/{f=0} f; /R"md\($/{f=1}' src/app/text_blocks.cpp > "$tmp"

# An empty result means the delimiters moved (e.g. R"md( renamed) and the awk
# silently matched nothing. Extracting to a temp first means a failure here
# leaves the existing README.md intact rather than truncated.
if [ ! -s "$tmp" ]; then
  echo "gen-readme: extracted nothing from src/app/readme.cpp — did the raw string delimiter change?" >&2
  exit 1
fi

# The banner lives here rather than in the literal: it is true of README.md but
# not of the readme `apb` prints, which has no upstream to be edited in.
# Redirected into README.md rather than moved onto it — mktemp is 0600, and a
# mode change would not show up in git, which records 100644 regardless.
{
  echo '<!-- GENERATED FILE — DO NOT EDIT.'
  echo '     Extracted from the README_MARKDOWN literal in src/app/readme.cpp.'
  echo '     Edit that literal instead, then run ./dev-scripts/gen-readme.sh (the'
  echo '     pre-commit hook does this for you when readme.cpp is staged). -->'
  echo
  cat "$tmp"
} > README.md

echo "wrote README.md ($(wc -l < README.md | tr -d ' ') lines)"
