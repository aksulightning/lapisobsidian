#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p .tests
flags=(-std=c11 -O2 -Wall -Wextra -Wconversion -Wshadow -Werror -ffp-contract=off -Iinclude)
if [[ "${SANITIZE:-0}" == 1 ]]; then
  flags+=(-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie -no-pie)
fi
"${CC:-gcc}" "${flags[@]}" -c src/world_border.c -o .tests/world-border.o
"${CC:-gcc}" "${flags[@]}" -c src/mobs.c -o .tests/mobs.o
"${CC:-gcc}" "${flags[@]}" -c src/mob_packets.c -o .tests/mob-packets.o
"${CC:-gcc}" "${flags[@]}" -c src/items.c -o .tests/items.o
"${CC:-gcc}" "${flags[@]}" -c src/inventory_packets.c -o .tests/inventory-packets.o
"${CC:-gcc}" "${flags[@]}" -c src/doors.c -o .tests/doors.o
"${CC:-gcc}" "${flags[@]}" -c src/signs.c -o .tests/signs.o
"${CC:-gcc}" "${flags[@]}" -c src/sign_packets.c -o .tests/sign-packets.o
"${CC:-gcc}" "${flags[@]}" -c src/command_packets.c -o .tests/command-packets.o
"${CC:-gcc}" "${flags[@]}" tests/commands.c tests/sanitizer.c src/commands.c src/world_border.c src/globals.c -o .tests/commands
.tests/commands
"${CC:-gcc}" "${flags[@]}" tests/world_border.c tests/sanitizer.c src/world_border.c src/globals.c -o .tests/world-border
.tests/world-border
"${CC:-gcc}" "${flags[@]}" tests/farlands.c tests/sanitizer.c src/beta173_*.c -lm -o .tests/farlands
.tests/farlands
"${CC:-gcc}" "${flags[@]}" tests/registry.c tests/sanitizer.c src/registry.c src/registries.c -o .tests/registry
.tests/registry
"${CC:-gcc}" "${flags[@]}" tests/worldgen.c tests/sanitizer.c src/beta173_*.c -lm -o .tests/worldgen
.tests/worldgen
"${CC:-gcc}" "${flags[@]}" tests/features.c tests/sanitizer.c src/beta173_*.c -lm -o .tests/features
.tests/features
"${CC:-gcc}" "${flags[@]}" tests/metadata.c tests/sanitizer.c src/world_metadata.c -o .tests/metadata
if ! .tests/metadata 2>.tests/metadata-rejections.log; then cat .tests/metadata-rejections.log; exit 1; fi
# Keep inherited packet code's existing conversion warnings separate from new modules.
integration_flags=(-O2 -Wall -Wextra -ffp-contract=off -ffunction-sections -fdata-sections -Iinclude)
if [[ "${SANITIZE:-0}" == 1 ]]; then
  integration_flags+=(-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie -no-pie)
fi
"${CC:-gcc}" "${integration_flags[@]}" tests/chunk_packet.c tests/sanitizer.c src/doors.c src/signs.c src/sign_packets.c src/packets.c src/varnum.c src/worldgen.c src/globals.c src/registries.c src/registry.c src/beta173_*.c -Wl,--gc-sections -lm -o .tests/chunk_packet
.tests/chunk_packet
sources=()
for source in src/*.c; do
  [[ "$source" == src/main.c ]] || sources+=("$source")
done
"${CC:-gcc}" "${integration_flags[@]}" tests/packet_input.c tests/sanitizer.c "${sources[@]}" -Wl,--gc-sections -lm -o .tests/packet_input
if ! .tests/packet_input 2>.tests/packet-rejections.log; then cat .tests/packet-rejections.log; exit 1; fi
"${CC:-gcc}" "${integration_flags[@]}" tests/command_packets.c tests/sanitizer.c "${sources[@]}" -Wl,--gc-sections -lm -o .tests/command_packets
.tests/command_packets
"${CC:-gcc}" "${integration_flags[@]}" tests/signs.c tests/sanitizer.c "${sources[@]}" -Wl,--gc-sections -lm -o .tests/signs
.tests/signs
"${CC:-gcc}" "${integration_flags[@]}" tests/doors.c tests/sanitizer.c "${sources[@]}" -Wl,--gc-sections -lm -o .tests/doors
.tests/doors
"${CC:-gcc}" "${integration_flags[@]}" tests/items.c tests/sanitizer.c "${sources[@]}" -Wl,--gc-sections -lm -o .tests/items
.tests/items
"${CC:-gcc}" "${integration_flags[@]}" tests/mobs.c tests/sanitizer.c "${sources[@]}" -Wl,--gc-sections -lm -o .tests/mobs
.tests/mobs
./tests/no-jar.sh
