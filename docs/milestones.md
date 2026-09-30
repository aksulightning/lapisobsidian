# Lapis Obsidian milestone report — 2026-09-30

Implemented scope: Milestones 0–4, with ordinary oaks and basic ground cover as
the initial vegetation subset. Milestone 4 adds persistent oak signs; the subsequent door feature adds working
two-block oak doors.
No scripting runtime, plugin system, database or third-party runtime dependency
was added. The Milestone 0/1 sections below describe their original scope;
Milestone 2 supersedes the earlier terrain/biome limitations.

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

## Milestone 2: world-generation features

### Files

- Added `include/beta173_biomes.h`, `src/beta173_biomes.c`,
  `include/beta173_features.h`, `src/beta173_features.c`,
  `include/beta173_math.h`, `src/beta173_math.c`, `src/beta173_surface.c`,
  `src/beta173_caves.c`, `src/beta173_ores.c`, `src/beta173_trees.c`,
  `src/beta173_decoration.c`.
- Added `tests/features.c` and four `tests/betanium-*-vectors.txt` fixtures for
  surfaces, caves, ores and trees.
- Updated RNG/noise/worldgen modules and headers, `src/worldgen.c`, generated
  registry snapshot/C/header, registry generator/lookup, registry/chunk/metadata
  tests, test runner, README, notices and worldgen/registry documentation.

### Decisions and behavior

Added per-column Beta climate biome selection and surface materials, caves,
ore veins, ordinary oaks, grass, ferns, flowers, dead bushes and snow. Appended
five modern protocol biome names without changing the existing 256-block compact
palette or enabling modern biome terrain. Existing packet structure, edit storage,
sprinting and gameplay handling remain in place.

Feature generation is phase-based and allocation-free. Caves replay nearby source
chunks; ores replay four possible sources; trees replay nine possible sources.
Every write is clipped to the target. Separate population streams and immutable
pre-vegetation clearance make results independent of exploration order. Ground
cover uses a bounded per-column stream. The server's block queries and chunk
serializer use the same final generator plus existing edits.

The adapter has a fixed four-chunk LRU cache; trees use two pre-vegetation scratch
chunks. Total fixed generation storage is approximately 226 KiB on the tested
64-bit target. A development benchmark generated 25 distinct seed-0 chunks in
0.654 CPU seconds before the adapter cache; this is a local measurement, not a
latency guarantee. On-demand sine entries avoid a separate 256 KiB lookup table.
No full-world allocation or pregeneration was introduced.

Generator version is now 2. Version 1 saves fail clearly before edit loading;
users must start a new world in another directory. There is no save migration.
Modern clients still use the core-pack-backed protocol 772 registry representation.
The serializer retains one biome per section, chosen from the chunk center;
per-column biome tinting is deferred.

### Validation

All 384 foundation reference vectors remain unchanged and pass. Added 108 external
feature fixtures: 20 surface hashes, 50 cave hashes (including water barriers),
25 ore hashes and 13 oak shape/tree-density records. All match the pinned betanium
reference, with the documented independent population stream for ore fixtures.

Additional C tests cover actual border-crossing canopies, reversed generation
order over 25 chunks, seed changes and cache eviction, compact coordinate edges,
required ore types and depth bounds, decoration substrate/occupancy constraints,
water and bedrock preservation, appended biome IDs, and rejection of version 1
metadata. The production packet test decodes final feature-bearing chunk bytes.
The no-Java/no-JAR source-only build still passes; registry regeneration is current.

Normal build and strict new-module compilation pass without warnings. ASan/UBSan
pass with no findings in tested paths. Full debug build succeeds with 219 inherited
core conversion/sign warnings and none attributed to new feature modules. The
sandbox emits an executable-name symbolization warning; LeakSanitizer remains
disabled because `/proc` is unavailable. A live TCP smoke check passes branded
status, protocol-772 login, known-pack negotiation, eleven registry packets, tags,
configuration finish, saved seed reload and conflicting-seed rejection.

### Deliberate limits

Only ordinary oak trees are included, including in taiga. Birch/spruce variants,
large branching trees, lakes, structures, clay deposits, fluid springs, reeds,
cacti, pumpkins and mushrooms remain deferred. Ground-cover placement is a compact
approximation rather than historical scatter. Climate/interpolation differences
from Milestone 1 remain. See `docs/worldgen.md` for the precise algorithms, fixture
provenance and adaptations. A real graphical client play session remains untested.

## Milestone 3: built-in slash commands

Added `include/commands.h`, `src/commands.c`, `src/command_packets.c`,
`tests/commands.c`, `tests/command_packets.c` and `docs/commands.md`. Updated
`src/main.c`, `src/packets.c`, `src/procedures.c`, `include/packets.h`, the test
runner and documentation. No save-layout, generator-version or registry changes.

Implemented all seven requested commands: help, seed, worldinfo, spawn, tp,
time and gamemode. A small `/admin` token login distinguishes administrative
commands without trusting client-supplied identities. The default disables admin
login. Bounded stack parsing, fixed per-player state, complete-packet validation
and existing packet/chunk/storage paths keep the implementation allocation-free.
A two-second teleport cooldown bounds repeated chunk transmissions. Normal UTF-8
chat and the existing !help/!msg syntax are retained, with formatting moved out of
the packet parser. The client receives a static command tree on entering play.

Game-mode updates now affect both protocol state and core mining, placement,
damage, hunger and interaction checks. Component-free creative inventory updates
are supported with slot/count/registry validation. Permissions and modes reset on
reconnect; the existing raw player-save structure remains unchanged. Time remains
runtime state. The startup --mirror-horizontal option is unaffected.

Validation: normal build, strict warnings for new modules, full regression tests,
and ASan/UBSan pass. New tests cover valid and malformed commands, lengths, argument
bounds, permissions, mode effects, teleport/time values, signed/unsigned command
packets, every truncated prefix of command/chat fixtures, UTF-8, creative inventory,
command-tree structure and game-mode packet bytes. A live login/configuration/play
smoke test exercises all requested commands, administrator login, creative movement,
teleports, spawn, malformed-command rejection and reconnect permission reset.

Limits: this is not full vanilla command syntax or complete game-mode emulation.
No selectors, relative/fractional coordinates, scripts, persistent permissions or
custom item components. Admin tokens cross the inherited unencrypted connection;
use a trusted network or tunnel. Graphical client playtesting and a full audit of
inherited packet/inventory paths remain outstanding. Exact commands and details
are in `docs/commands.md` and the build/test section below.

## Milestone 4: persistent signs

Added `include/signs.h`, `include/sign_protocol.h`, `src/signs.c`,
`src/sign_packets.c`, `tests/signs.c` and `docs/signs.md`. Updated the main packet
routing/startup path, packet declarations and chunk transmission, block-change
and player-session hooks, crafting, test runner/input fixtures, README and registry
documentation. Existing world/player binary layouts and generator version remain.

The fixed 128-record C pool stores four plain UTF-8 lines on each side, with
bounded editor sessions per connection. Standing and wall oak signs support
placement, orientation, editing, broadcasts/chunk reloads, direct removal,
support removal and stale-record cleanup. Existing block changes remain in
`world.bin`; versioned explicit-width text records use `signs.bin`. Writes use a
temporary snapshot and rename; no database or new runtime/dependency is added.
See `docs/signs.md` for cross-file crash consistency and backup requirements.

Protocol code encodes modern sign block entities and front/back editor packets.
Sign, placement and mining frames are fully validated before world mutation;
coordinate narrowing, UTF-8, byte limits, edit authorization and session expiry
are checked. The compact palette stays unchanged, while a protocol-pinned small
state table supplies the extra standing/wall orientations. The Beta-era oak-sign
recipe yields one sign. Two sanitizer-reported unaligned inventory reads, an
empty-grid crafting index and chest-content traversal on the touched paths were
fixed without changing storage formats or refactoring unrelated systems.

Validation: normal build, full regression/no-JAR suite, strict warnings for new
modules, and ASan/UBSan passed. Sign tests cover sides, placement, malformed and
truncated input, Unicode/NBT, permissions, editor locks, storage limits, failed
writes, corrupt files, restart, removal and stale cleanup. A live server socket
test verifies placement/editor, Unicode text, process restart/chunk transmission,
re-edit, removal and malformed-packet disconnection. Graphical playtesting remains
outstanding. Full details, compatibility limits and exact commands: `docs/signs.md`.

## Following Milestone 4: oak doors

Added `include/doors.h`, `src/doors.c`, `tests/doors.c` and `docs/doors.md`.
Updated crafting, startup, block-update/chunk transmission and block-change/use
hooks, test runner and documentation. The fixed 256-door pool uses approximately
2 KiB; `doors.bin` adds explicit-width state records without changing world/player
layouts. Two-block placement rolls back on capacity/save failure; either half
operates the door, and removal/replacement/support loss cleans up the pair.

All four facing directions, both halves and open/closed states use a minimal
protocol-772 snapshot. The Beta recipe yields one oak door. Normal/no-JAR builds,
full regressions, strict warnings for the module and ASan/UBSan pass. Live socket
testing covers both placement halves, interaction, process restart, saved open
state in chunk transmission and paired removal. Graphical playtesting remains
outstanding. Fixed left hinges, no door sounds/redstone or iron-door activation,
and inherited mob collision behavior are current limits. See `docs/doors.md` for
file details, backup/recovery behavior and exact validation commands.

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
- Signs support ordinary oak standing/wall forms; dyes, glow, wax and hanging
  signs are deferred. Vegetation is the initial
  subset described above. Inherited survival hunger remains; creative/spectator
  bypass its updates. Sprinting remains supported.
- The inherited compact coordinate limits and raw world-edit/player layout
  remain. No migration of earlier saves is provided. Fixed spawn may be underwater.
- Core networking, inventory/chest handling and storage have not received a full
  adversarial audit. The tests do not establish that every malformed client packet
  is safe. Existing client authentication behavior is unchanged.
- Windows, Cosmopolitan and ESP-IDF builds have not been run here. The embedded
  task is branded Lapis Obsidian and has an 8 KiB stack for the C terrain path.
