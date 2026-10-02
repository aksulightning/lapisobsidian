# Water and lava

Water and lava now run on a bounded, non-recursive server tick queue. Water
spreads up to seven blocks horizontally and lava up to three, preferring to
fall when unsupported. Water normally updates in 200 ms and lava in 600 ms;
busy worlds may take longer. Removing or obstructing a supply drains its flow.
No new sources are created by flowing liquid (including waterfalls).

Craft an empty bucket with three iron ingots in a V on a crafting table.
Right-click a source to collect it, or aim at a surface with a filled bucket
to pour it. Only the main hand is supported. Empty buckets stack to 16, filled
buckets to one. Survival exchanges the held bucket; collecting from a stack
requires a spare main-inventory slot. Creative pouring keeps the filled bucket.
Adventure and spectator cannot change fluids. Bucket use validates the entire
packet and raycasts up to six blocks, stopping at intervening solid blocks.
The inherited player position has block precision, so edge-of-block aiming may
need adjustment; exact fractional player positions are not added to save files.

Water beside or above a lava source makes obsidian; flowing lava makes
cobblestone. Lava descending onto water makes stone. Pouring a bucket into an
opposing liquid uses the same basic cooling results. Bucket and cooling sounds
use named sounds already present in the client; no sound assets are bundled.
Plants, crops, torches, dust and plates can be washed away, using existing item
drops and side-table cleanup. Existing lava damage, dropped-item destruction
in lava, swimming and farmland hydration continue to use the same fluid IDs.

## Storage and limits

`src/fluids.c` owns a fixed 2,048-entry deduplicated queue (about 28 KiB of static
storage including its hash index), with at most 64 cell evaluations per 100 ms.
No allocation, recursion, whole-world simulation or chunk pregeneration is
needed. A bounded scan of saved edits and their neighbours resumes flow after
startup and recovers overflowed updates. Natural oceans and pools remain still
until edits disturb them. Work slows under load rather than increasing the tick
budget; one long stalled tick does not trigger unbounded catch-up work.

The existing 256-entry block palette and packed `world.bin` format stay intact.
Flow levels persist as ordinary block edits. Waterfalls use the existing level-one
flow state, rather than introducing new falling-state IDs or changing old saves.
Historical sources created by the old recursive flow cannot be distinguished
from deliberately placed sources and are not automatically removed.

Fluids share the inherited 20,000 block-edit limit. At capacity simulation pauses
on the first rejected edit; restoring terrain or removing existing fluid edits
allows it to resume. Large floods can consume this budget. The queue is transient;
restart recovery scans records incrementally, skipping chest inventory records.
Coordinates are checked before narrowing and flow is sealed at Y=0/255 and the
compact X/Z storage limits. The player world border is unchanged.

This is intentionally simple fluid behavior, not exact Beta simulation. There
is no waterlogging, infinite-water-source formation, fire spread, portal creation,
fluid entity currents, or shortest-path search around obstacles. Doors and
trapdoors currently remain fluid barriers, including while open. Client visual
fluid state is supplied by the checked-in protocol-772 palette.

## Verification

```sh
./build.sh
./tests/run.sh
SANITIZE=1 ./tests/run.sh
node build_registries.js --check  # optional maintenance check only
```

`tests/fluids.c` covers propagation and drainage, water/lava mixing, waterfall
source handling, lava timing, restart recovery, chunk palette use, bucket
inventory and recipe handling, packet validation, queue overflow and coordinate
limits. New fluid code compiles with the strict warning set. A live graphical
Minecraft client is still needed to assess aiming feel and visual animation.
