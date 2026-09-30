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
"${CC:-gcc}" "${flags[@]}" tests/worldgen.c tests/sanitizer.c src/beta173_*.c -lm -o .tests/worldgen
"${CC:-gcc}" "${flags[@]}" tests/features.c tests/sanitizer.c src/beta173_*.c -lm -o .tests/features
.tests/features
.tests/worldgen
"${CC:-gcc}" "${flags[@]}" tests/metadata.c tests/sanitizer.c src/world_metadata.c -o .tests/metadata
if ! .tests/metadata 2>.tests/metadata-rejections.log; then cat .tests/metadata-rejections.log; exit 1; fi
# Keep inherited packet code's existing conversion warnings separate from new modules.
integration_flags=(-O2 -Wall -Wextra -ffp-contract=off -ffunction-sections -fdata-sections -Iinclude)
if [[ "${SANITIZE:-0}" == 1 ]]; then
  integration_flags+=(-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie -no-pie)
fi
"${CC:-gcc}" "${integration_flags[@]}" tests/chunk_packet.c tests/sanitizer.c src/packets.c src/varnum.c src/worldgen.c src/globals.c src/registries.c src/registry.c src/beta173_*.c -Wl,--gc-sections -lm -o .tests/chunk_packet
.tests/chunk_packet
sources=()
for source in src/*.c; do
  [[ "$source" == src/main.c ]] || sources+=("$source")
done
"${CC:-gcc}" "${integration_flags[@]}" tests/packet_input.c tests/sanitizer.c "${sources[@]}" -Wl,--gc-sections -lm -o .tests/packet_input
if ! .tests/packet_input 2>.tests/packet-rejections.log; then cat .tests/packet-rejections.log; exit 1; fi
./tests/no-jar.sh
