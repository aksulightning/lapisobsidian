# Farming and basic controls

These systems extend Lapis Obsidian's C gameplay modules without changing terrain
generation, protocol version 772, the compact world palette, or sprinting.

## Controls

- Redstone torches attach to the top or any horizontal side of solid blocks.
  They invert power adjacent to the actual supporting block. Removing that
  support removes the torch; removing a block beneath a wall torch does not.
- Stone pressure plates detect living players and mobs in their block cell.
  Oak plates additionally detect dropped items. Dead/loading/disconnected players
  and spectators do not activate plates. Plates release when empty, with checks
  every 100 ms even when no world blocks have changed. Arrows are not triggers.
- Oak trapdoors open by hand or adjacent redstone power. Iron trapdoors respond
  only to power. Powered trapdoors stay open and close when power disappears.
  Trapdoors consume power and do not act as power sources. Sneaking bypasses
  hand interaction for placement. Side/top clicks place bottom halves; underside
  clicks place top halves. Side-click direction determines facing, with north
  used for vertical clicks. Cursor-hit-height selection and waterlogging are
  intentionally not modeled in this basic implementation.
- Trapdoors do not require ongoing wall support. Mob/arrow and dropped-item
  collision treats an open trapdoor as passable and a closed one as solid. This
  matches the existing coarse block-cell collision, not exact thin shapes.
  Plate occupancy likewise uses the core's integer positions, not bounding boxes.

Craft stone/oak plates from two stone/oak-plank items horizontally. Six oak
planks in two horizontal rows produce two oak trapdoors; four iron ingots in a
square produce one iron trapdoor. Normal output clicks consume ingredients on
the server. Shift-click output and recipe-book entries are not implemented for
these recipes. Only oak is included for the wooden variants.

The fixed 256-component pool is shared with dust, torches, levers and note blocks.
`circuits.bin` version 2 retains seven-byte records; version 1 remains readable.
New records store wall facing or trapdoor facing/half/manual-open bits. Dynamic
plate activation and redstone power are recomputed. Old binaries cannot read the
new format. Keep the sidecar with the world files; do not downgrade without a
matching pre-update backup. No raw world-save migration is needed.

## Wheat and bread

1. Craft a hoe: two matching tool materials across the top and two sticks down
   either end. Wood, cobblestone, iron, gold, diamond and the inherited netherite
   material are supported. Hoe wear follows the core's probabilistic tool-break
   convention rather than storing individual durability values.
2. Right-click the top of dirt/grass with clear air above to make farmland.
3. Plant wheat seeds on the farmland's top face. Grass drops or the existing
   one-grass-to-one-seed recipe provide starter seeds.
4. Place water within four blocks in X/Z, at soil height or one block above.
   Source and flowing water count. Dry soil remains usable but growth pauses.
5. Hydrated wheat advances through eight visible ages. Each stage needs five
   hydrated plot visits, approximately 32 seconds; full growth takes roughly
   four minutes. Farm processing runs while the server runs, including away
   from players. There is no offline catch-up, light requirement, rain, trampling,
   bonemeal acceleration or automatic dry-soil decay in this subset.
6. Break mature wheat for one wheat and two seeds. Immature crops return one seed.
   Replant and put three wheat horizontally in a crafting table to make bread.
   A normal output click consumes one grain from each slot. Bread uses the
   existing eating system, restoring five food points; no new hunger system is
   introduced.

Creative farming consumes no seeds or tool wear; creative harvest has no drops.
Adventure and spectator modes cannot till, plant or harvest. Farmland itself drops
ordinary dirt. Removing soil cleans up the crop and attempts its harvest. If the
item pool is full, direct harvest leaves the crop intact, and support cleanup
retries later. At the item cap this can briefly leave unsupported wheat until
space is available. The two mature harvest stacks are checked together before
removing the crop.

## Storage and bounded work

`src/farming.c` keeps 256 compact plots (about 2.5 KiB), separate from the circuit
pool. It visits at most four slots per 100 ms, giving a full sweep every 6.4
seconds. Each visit checks the bounded 9x9 water neighborhood at two heights.
This bounds work independently of world size; actual cost still depends on the
inherited world-edit lookup. Water changes may take one sweep to appear.

`farming.bin` has the header `LOFARM`, version 1, reserved 0. Each seven-byte
record contains big-endian signed X/Z, soil Y (0..254), crop age (0..7 or 8 for
no crop), and wet-visit progress (0..4). Moisture is derived from water. Stage
changes and edits save via a temporary file and rename. Up to one partial stage
of progress may be lost on restart. Malformed/duplicate/oversized records fail
startup; stale records are cleaned, and older farmland edits are adopted with
age-zero crops. A world exceeding the plot limit fails clearly.

Include `farming.bin` and `circuits.bin` with `world.bin`, `world.meta`, signs and
doors in stopped-server backups. As with existing sidecars, atomic rename does
not make changes transactional across files and there is no power-loss fsync
guarantee. Recover the complete backup together. Placement/harvest save failures
roll back; growth failures retain the previous age and report the failure.

## Implementation and validation

New files: `include/farming.h`, `src/farming.c`, and `tests/farming_circuits.c`.
Circuit, mob, item and packet modules have small integration changes; crafting,
main tick/loading, registry snapshot and documentation are updated. No new
runtime, library, Java dependency, Minecraft assets or registry extraction.

```sh
./build.sh
./tests/run.sh
SANITIZE=1 ./tests/run.sh
node build_registries.js --check  # optional maintainer verification
```

Tests cover four wall orientations, support removal and inversion, plate actors
and release, trapdoor hand/power state and restart, old circuit saves, water
range and dry pauses, crop growth/restart/harvest, capacity, malformed farm saves,
save failure, permissions, recipes, authoritative bread output, chunk overlays,
and mob sound framing/categories/range. Existing protocol/chunk/worldgen and
source-only no-JAR tests remain in the suite. A graphical-client playtest is still
needed to judge rendered collision details and audible playback.

Validation completed: normal build, full regression suite, strict new-module
compilation, full AddressSanitizer/UndefinedBehaviorSanitizer suite, deterministic
registry snapshot check and source-only no-JAR build all passed. LeakSanitizer
is disabled by the existing sandbox test harness. No graphical/audio playtest
is claimed.
