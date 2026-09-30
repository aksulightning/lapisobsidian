# Lapis Obsidian milestone report — 2026-09-30

Implemented scope: Milestone 0 and the explicitly limited Milestone 1 foundation.
Milestones 2–4 remain pending. No commands, signs, feature generators, scripting
runtime, plugin system, database or third-party runtime dependency was added.

## Milestone 0: standalone registry snapshot

### Files

- Added `generated/registry_snapshot.json`, `src/registries.c`,
  `include/registries.h`, `src/registry.c`, `include/registry.h`,
  `include/protocol.h`, `tests/registry.c`, `tests/no-jar.sh`, `tests/run.sh`,
  `tests/sanitizer.c`, `NOTICE.md`, and registry documentation.
- Changed `build.sh`, `build_registries.js`, `.gitignore`,
  `.github/workflows/build.yml`, `README.md`, `src/globals.c`, `src/main.c`,
  `src/packets.c`, `include/packets.h`, `src/procedures.c`, `src/tools.c`,
  `src/varnum.c`, `include/varnum.h`, and `src/serialize.c`.
- Removed `extract_registries.sh` and extraction instructions/dependencies.

### Decisions and behavior

Retained upstream palette selection, compact IDs, item drop/placement mappings
and tag serializers. Stored only the necessary compatibility subset as checked-in
immutable C, with an optional maintainer-only JS serializer reading a reduced
local snapshot. No game binary was downloaded or used. The normal build compiles
C directly and exits without launching the server.

Versioned protocol 772 is checked at compile time; startup verifies palette wire
encoding and mappings. Item IDs are checked before narrowing network input.
Registry lookups reject out-of-range IDs. Unsupported login protocols and missing
or mismatched core-pack acknowledgements are rejected. Configuration waits for
that acknowledgement before sending omitted-NBT registry entries.

Wire tests exposed an upstream section-count mismatch: 32 sections were emitted
for the 24-section overworld. Corrected section count, encoded length, and non-air
counts. Also bounded malformed VarInts, corrected string short-read handling and
signed position packing in touched code. These are targeted compatibility/safety
fixes, not a packet-parser rewrite. Milestone 0 retained existing gameplay.

### Validation

Registry boundary tests cover compact ID 255, 256 and UINT32_MAX; highest valid
item 1415; required Beta blocks/items; biome and dimension bounds; and mapping
consistency. Production chunk bytes are independently decoded to verify framing,
24 sections, palette IDs, edit overlay, biome IDs, light array lengths and registry
packets. Tests also exercise valid, wrong-version and every truncated prefix of
the known-pack response, plus malformed VarInts and strings.

A source-only copy builds with PATH limited to shell tools and the C toolchain:
no Java, JS runtime, extraction directory, generated report directory or JAR.
The optional deterministic regeneration check passes. A live TCP smoke test
passes branded status, protocol-772 login, known-pack acknowledgement, eleven
registry packets, tags and configuration finish.

## Milestone 1: deterministic terrain foundation

### Files

- Added `include/beta173_rng.h`, `src/beta173_rng.c`,
  `include/beta173_noise.h`, `src/beta173_noise.c`,
  `include/beta173_worldgen.h`, `src/beta173_worldgen.c`,
  `include/world_metadata.h`, `src/world_metadata.c`.
- Added `tests/worldgen.c`, `tests/betanium-vectors.txt`, `tests/metadata.c`,
  `tests/chunk_packet.c`, `tests/packet_input.c` and `docs/worldgen.md`.
- Integrated through `src/worldgen.c`, `include/worldgen.h`, `src/main.c`,
  `include/globals.h`, `src/globals.c`, `src/serialize.c`, `src/procedures.c`,
  `src/packets.c`, test/build maintenance and documentation.

### Decisions and behavior

Ported betanium's Java RNG, Perlin/simplex, climate and density equations to C.
Uses unsigned arithmetic for Java wraparound, retains noise initialization order,
and preserves the historical column gradient-cache behavior. Terrain spans
Y=0..127 with sea level 64. Only stone, water, ice and air are generated; the
existing below-zero compatibility floor remains bedrock. Protocol biomes remain
plains until the surface/biome milestone. Existing edits overlay base terrain.

Uses fixed 256-byte permutations and a single 425-double density cache (24,680
bytes of fixed noise/cache data on the tested target). No allocation per chunk,
full-world allocation or pregeneration. World seed is a persistent 64-bit value;
`--seed` accepts signed decimal seeds. Metadata binds seed, generator and protocol.
Legacy saves and conflicting seeds fail clearly before edits are loaded.

### Validation

384 frozen external reference vectors passed: Java RNG and bounded RNG, Perlin
samples, density samples and terrain heights for six seeds including both signed
64-bit extremes. Development-target comparisons had zero numerical difference.
Adjacent positive/negative boundary nodes agree exactly; repeated generation,
changed generation order, reinitialization and different-seed tests passed.
Invalid coordinates, invalid RNG bounds and invalid seed text are rejected.
Metadata tests cover restart, conflict, corrupt/trailing metadata and legacy saves.

## Commands and results

Run from the repository root:

```sh
./build.sh
./lapis-obsidian --seed -12345
./tests/run.sh
SANITIZE=1 ./tests/run.sh
node build_registries.js --check  # optional maintainer check only
DEBUG=1 ./build.sh              # optional full debug build
```

Normal build: passed with `-Wall -Wextra` and no warnings. New modules and unit
tests: passed `-Wall -Wextra -Wconversion -Wshadow -Werror`. Full debug build:
compiles, but inherited core code still emits conversion/sign-conversion warnings;
these have not been hidden or mass-refactored. ASan/UBSan test suite: passed with
no findings in tested paths. LeakSanitizer is disabled in test executables because
the execution sandbox cannot inspect `/proc`; leak testing is not claimed.

## Remaining compatibility concerns

- A real vanilla-client rendering/play session has **not** been run. Socket
  negotiation and independent packet decoding are not substitutes for that gate.
- This is Beta-style terrain, not historical bit-for-bit emulation. Shared
  world-space climate samples and direct interpolation are documented adaptations;
  reference-based density/height fixtures include those adaptations.
- Biome surface blocks, caves, ores, trees, decorations, slash commands and
  persistent signs are still absent. Hunger and other inherited gameplay remain
  unchanged; sprinting remains supported.
- The inherited compact coordinate limits and raw world-edit/player layout
  remain. No migration of earlier saves is provided. Fixed spawn may be underwater.
- Core networking, inventory/chest handling and storage have not received a full
  adversarial audit. The tests do not establish that every malformed client packet
  is safe. Existing client authentication behavior is unchanged.
- Windows, Cosmopolitan and ESP-IDF builds have not been run here. The embedded
  task is branded Lapis Obsidian and has an 8 KiB stack for the C terrain path.
