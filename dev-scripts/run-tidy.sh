#!/usr/bin/env bash
set -euo pipefail
# build/, src/ and report.txt are all repo-relative, so run from the root.
cd "$(dirname "$0")/.."
run-clang-tidy -p build -extra-arg="-isysroot$(xcrun --show-sdk-path)" "^$(pwd)/src/" > clang-tidy-report.txt
echo "wrote clang-tidy-report.txt ($(grep -c 'warning:' report.txt) warnings)"
