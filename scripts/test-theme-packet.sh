#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
target="$(mktemp)"
trap 'rm -f "$target"' EXIT
g++ -std=c++11 -Wall -Wextra -Werror -Iinclude tests/theme_packet_test.cpp -o "$target"
"$target" "$@"
