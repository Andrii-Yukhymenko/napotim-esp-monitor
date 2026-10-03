#!/usr/bin/env bash
set -euo pipefail
project_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
test_binary="$(mktemp /tmp/napotim-update-status-test.XXXXXX)"
trap 'rm -f -- "$test_binary"' EXIT
g++ -std=c++17 -Wall -Wextra -Werror -I "$project_root/include" "$project_root/tests/update_status_test.cpp" -o "$test_binary"
"$test_binary"
