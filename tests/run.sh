#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p .tests
flags=(-std=c11 -O2 -Wall -Wextra -Wconversion -Wshadow -Werror -ffp-contract=off -Iinclude)
if [[ "${SANITIZE:-0}" == 1 ]]; then
  flags+=(-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie -no-pie)
fi
"${CC:-gcc}" "${flags[@]}" tests/registry.c tests/sanitizer.c src/registry.c src/registries.c -o .tests/registry
.tests/registry
./tests/no-jar.sh
