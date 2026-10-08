#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p .tests
./tests/build-features.sh
flags=(-O2 -Wall -Wextra -ffp-contract=off -ffunction-sections -fdata-sections -Iinclude)
if [[ "${SANITIZE:-0}" == 1 ]]; then
  flags+=(-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie -no-pie)
fi
sources=()
for source in src/*.c; do
  [[ "$source" == src/main.c ]] || sources+=("$source")
done
for world_edit in 0 1; do
  for tree_chopper in 0 1; do
    features=(-DLAPIS_WORLD_EDIT="$world_edit" -DLAPIS_TREE_CHOPPER="$tree_chopper")
    "${CC:-gcc}" "${flags[@]}" "${features[@]}" -Wconversion -Wshadow -Werror \
      -fsyntax-only src/world_edit.c src/tree_chopper.c tests/optional_features.c
    "${CC:-gcc}" "${flags[@]}" "${features[@]}" tests/optional_features.c tests/sanitizer.c \
      "${sources[@]}" -Wl,--gc-sections -lm -o .tests/optional-features
    # Runtime environment cannot override the compiled selection.
    LAPIS_WORLD_EDIT=$((1-world_edit)) LAPIS_TREE_CHOPPER=$((1-tree_chopper)) .tests/optional-features
  done
done
# Also decode the enabled command graph (the main suite checks the default graph).
"${CC:-gcc}" "${flags[@]}" -DLAPIS_WORLD_EDIT=1 -DLAPIS_TREE_CHOPPER=1 \
  tests/command_packets.c tests/sanitizer.c "${sources[@]}" -Wl,--gc-sections -lm -o .tests/optional-command-packets
.tests/optional-command-packets
