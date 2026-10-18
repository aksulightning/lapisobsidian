#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p .tests
flags=(-std=c11 -O2 -Wall -Wextra -Wconversion -Wshadow -Werror -ffp-contract=off -Iinclude)
if [[ "${SANITIZE:-0}" == 1 ]]; then
  flags+=(-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie -no-pie)
fi
experimental=()
libs=()
for feature in LAPIS_ENABLE_EXPERIMENTAL_DROPSYSTEM LAPIS_ENABLE_EXPERIMENTAL_AUTH; do
  value="${!feature:-0}"
  [[ "$value" == 0 || "$value" == 1 ]] || { echo "$feature must be 0 or 1" >&2; exit 1; }
  experimental+=("-D$feature=$value")
done
if [[ "${LAPIS_ENABLE_EXPERIMENTAL_AUTH:-0}" == 1 ]]; then libs=(-lcurl -lssl -lcrypto); fi
flags+=("${experimental[@]}")
for source in auth auth_profile durable drop_store plates plate_state plate_terrain fluids circuits notes midi musicbox farming packet_input server_config server_stats server_console; do
  "${CC:-gcc}" "${flags[@]}" -c "src/$source.c" -o ".tests/$source.o"
done
"${CC:-gcc}" "${flags[@]}" tests/server_config.c tests/sanitizer.c src/server_config.c src/beta173_rng.c -o .tests/server-config
.tests/server-config
"${CC:-gcc}" "${flags[@]}" tools/make_demo_song.c tests/sanitizer.c -o .tests/make-demo
mkdir -p .tests/demo/songs
rm -f .tests/demo/songs/lapis-demo.mid
(cd .tests/demo && ../make-demo)
"${CC:-gcc}" "${flags[@]}" tests/midi.c tests/sanitizer.c src/midi.c -o .tests/midi
.tests/midi .tests/demo/songs/lapis-demo.mid
"${CC:-gcc}" "${flags[@]}" -c src/world_border.c -o .tests/world-border.o
"${CC:-gcc}" "${flags[@]}" -c src/mobs.c -o .tests/mobs.o
"${CC:-gcc}" "${flags[@]}" -c src/mob_packets.c -o .tests/mob-packets.o
"${CC:-gcc}" "${flags[@]}" -c src/items.c -o .tests/items.o
"${CC:-gcc}" "${flags[@]}" -c src/inventory_packets.c -o .tests/inventory-packets.o
"${CC:-gcc}" "${flags[@]}" -c src/doors.c -o .tests/doors.o
"${CC:-gcc}" "${flags[@]}" -c src/signs.c -o .tests/signs.o
"${CC:-gcc}" "${flags[@]}" -c src/sign_packets.c -o .tests/sign-packets.o
"${CC:-gcc}" "${flags[@]}" -c src/command_packets.c -o .tests/command-packets.o
"${CC:-gcc}" "${flags[@]}" tests/commands.c tests/sanitizer.c src/commands.c src/server_stats.c src/world_border.c src/globals.c src/plate_state.c -o .tests/commands
.tests/commands
"${CC:-gcc}" "${flags[@]}" tests/server_console.c tests/sanitizer.c src/server_console.c src/server_stats.c src/globals.c src/plate_state.c -o .tests/server-console
.tests/server-console
"${CC:-gcc}" "${flags[@]}" tests/world_border.c tests/sanitizer.c src/world_border.c src/globals.c src/plate_state.c -o .tests/world-border
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
integration_flags+=("${experimental[@]}")
# Compile shared integration sources once. Each object has an explicit path;
# avoid repeated compiler/linker temporary files for every test executable.
mkdir -p .tests/integration
sources=()
for source in src/*.c; do
  [[ "$source" == src/main.c ]] && continue
  name=${source##*/}; name=${name%.c}
  "${CC:-gcc}" "${integration_flags[@]}" -c "$source" -o ".tests/integration/$name.o"
  sources+=(".tests/integration/$name.o")
done
chunk_sources=()
for name in plate_state block_changes block_rules fluids farming circuits notes doors signs sign_packets world_packets varnum worldgen globals registries registry; do
  chunk_sources+=(".tests/integration/$name.o")
done
chunk_sources+=(.tests/integration/beta173_*.o)
"${CC:-gcc}" "${integration_flags[@]}" tests/chunk_packet.c tests/sanitizer.c "${chunk_sources[@]}" -Wl,--gc-sections -lm "${libs[@]}" -o .tests/chunk_packet
.tests/chunk_packet
"${CC:-gcc}" "${integration_flags[@]}" tests/plates.c tests/sanitizer.c "${sources[@]}" -Wl,--gc-sections -lm "${libs[@]}" -o .tests/plates
.tests/plates
"${CC:-gcc}" "${integration_flags[@]}" tests/fluids.c tests/sanitizer.c "${sources[@]}" -Wl,--gc-sections -lm "${libs[@]}" -o .tests/fluids
.tests/fluids
"${CC:-gcc}" "${integration_flags[@]}" tests/config_packets.c tests/sanitizer.c "${sources[@]}" -Wl,--gc-sections -lm "${libs[@]}" -o .tests/config-packets
.tests/config-packets
"${CC:-gcc}" "${integration_flags[@]}" tests/packet_input.c tests/sanitizer.c "${sources[@]}" -Wl,--gc-sections -lm "${libs[@]}" -o .tests/packet_input
if ! .tests/packet_input 2>.tests/packet-rejections.log; then cat .tests/packet-rejections.log; exit 1; fi
"${CC:-gcc}" "${integration_flags[@]}" tests/packet_dispatch.c tests/sanitizer.c "${sources[@]}" -Wl,--gc-sections -lm "${libs[@]}" -o .tests/packet-dispatch
.tests/packet-dispatch
"${CC:-gcc}" "${integration_flags[@]}" tests/command_packets.c tests/sanitizer.c "${sources[@]}" -Wl,--gc-sections -lm "${libs[@]}" -o .tests/command_packets
.tests/command_packets
"${CC:-gcc}" "${integration_flags[@]}" tests/signs.c tests/sanitizer.c "${sources[@]}" -Wl,--gc-sections -lm "${libs[@]}" -o .tests/signs
.tests/signs
"${CC:-gcc}" "${integration_flags[@]}" tests/doors.c tests/sanitizer.c "${sources[@]}" -Wl,--gc-sections -lm "${libs[@]}" -o .tests/doors
.tests/doors
"${CC:-gcc}" "${integration_flags[@]}" tests/items.c tests/sanitizer.c "${sources[@]}" -Wl,--gc-sections -lm "${libs[@]}" -o .tests/items
.tests/items
"${CC:-gcc}" "${integration_flags[@]}" tests/mobs.c tests/sanitizer.c "${sources[@]}" -Wl,--gc-sections -lm "${libs[@]}" -o .tests/mobs
.tests/mobs
"${CC:-gcc}" "${integration_flags[@]}" tests/circuits.c tests/sanitizer.c "${sources[@]}" -Wl,--gc-sections -lm "${libs[@]}" -o .tests/circuits
.tests/circuits
"${CC:-gcc}" "${integration_flags[@]}" tests/farming_circuits.c tests/sanitizer.c "${sources[@]}" -Wl,--gc-sections -lm "${libs[@]}" -o .tests/farming-circuits
.tests/farming-circuits
"${CC:-gcc}" "${integration_flags[@]}" tests/security.c tests/sanitizer.c "${sources[@]}" -Wl,--gc-sections -lm "${libs[@]}" -o .tests/security
.tests/security
if [[ "${LAPIS_ENABLE_EXPERIMENTAL_DROPSYSTEM:-0}" == 1 ]]; then
  "${CC:-gcc}" "${flags[@]}" -c tests/drop_store.c -o .tests/drop-test.o
  "${CC:-gcc}" "${integration_flags[@]}" tests/drop_store.c tests/sanitizer.c "${sources[@]}" -Wl,--gc-sections -lm "${libs[@]}" -o .tests/drop-store
  .tests/drop-store
fi
if [[ "${LAPIS_ENABLE_EXPERIMENTAL_AUTH:-0}" == 1 ]]; then
  "${CC:-gcc}" "${flags[@]}" -DLAPIS_AUTH_TESTING=1 -c tests/auth.c -o .tests/auth-test.o
  auth_sources=()
  for object in "${sources[@]}"; do [[ "$object" == .tests/integration/auth.o ]] || auth_sources+=("$object"); done
  "${CC:-gcc}" "${integration_flags[@]}" -DLAPIS_AUTH_TESTING=1 tests/auth.c tests/sanitizer.c src/auth.c "${auth_sources[@]}" -Wl,--gc-sections -lm "${libs[@]}" -o .tests/auth
  .tests/auth
fi
./tests/no-jar.sh
