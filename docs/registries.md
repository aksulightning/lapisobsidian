# Protocol registry snapshot

The supported protocol is 772 (Java 1.21.8). `include/protocol.h` defines the
server version, and generated C has a compile-time check against the snapshot
version. Startup verifies that all 256 encoded palette entries match their
network block-state IDs and that block drops are valid item IDs.

The snapshot retains bareiron's exact palette-selection order: its whitelist,
followed by the earliest default block states with matching items after its
blacklist. Retaining all 256 entries avoids changing its compact storage IDs.
Only item names used by the palette/gameplay and compatibility tests are emitted.
The valid network item range is 0..1415; acceptance of an ID does not implement
that item's gameplay. Lookups distinguish compact block IDs from network IDs.

The ten server-local biome IDs are plains=0, mangrove_swamp=1, desert=2,
snowy_plains=3, beach=4, forest=5, taiga=6, jungle=7, swamp=8, savanna=9.
The first five IDs retain upstream order; the appended entries support Beta
climate-biome mapping. These are local registry IDs, not vanilla numeric biome IDs. Dimension type overworld=0 uses the client's built-in
384-block dimension, with minimum Y=-64. Chunk packets contain 24 sections.

Configuration sends one entry for each required variant registry, the damage
type identifiers, ten biomes, and overworld. These are names without NBT payloads,
as in upstream. The client must first acknowledge exactly the 1.21.8 core pack;
otherwise configuration is rejected. No textures, models, recipes, Java code,
or game binaries are distributed. Variant entries provide protocol compatibility
and do not enable corresponding gameplay.

## Sources

- bareiron `02733bee99289a9d2fe152d65bab82c7b3326767`: palette selection, tag
  selection and packet serialization.
- PrismarineJS/minecraft-data `f5d7d74604d8c6153fd086bfe035e0630a5207cc`,
  `data/pc/1.21.8/blocks.json`, `items.json`, `version.json`: numeric IDs.
  Only selected names, IDs and mappings are retained. The project declares MIT
  licensing in its README and notes upstream data provenance.
- misode/mcmeta `6b439c652da3af9d99455de19645fc3902a983af`, tag
  `1.21.8-registries`: required registry identifier lists. No game assets retained.

## Maintenance

`generated/registry_snapshot.json` is the reduced, ordered maintenance input.
`build_registries.js` reuses upstream's packet serializers and writes immutable
arrays in `src/registries.c` plus `include/registries.h`. It reads only the checked-in
snapshot. Normal builds and runtime never run it. Generation is deterministic.

To update protocol support, update the protocol constant, reduced snapshot and
version/range assertions together; inspect changes to packet layouts, known packs,
states, biome order, dimension height and tags; regenerate; run tests including
chunk decoding; verify a real matching vanilla client login. Do not reorder the
internal block palette without a saved-world migration. Numeric ID agreement and
wire-format tests do not replace a real client interoperability check.

## Sign state extension

Milestone 4 adds `include/sign_protocol.h`, a small maintained protocol-772
snapshot for sixteen dry standing-sign states, four dry wall-sign states and
sign block-entity type 7. Compact block ID 143 remains unchanged in saved worlds.
Orientation-specific states are sent as updates after the chunk packet, with
bounded sign block-entity NBT. See `docs/signs.md` for provenance, encoding and
validation. This table must be reviewed alongside generated registries during
protocol upgrades; normal builds do not regenerate either snapshot.

## Oak door state extension

`src/doors.c` maintains the four oak-door facing bases from the same pinned
minecraft-data `blocks.json`, guarded by a protocol-772 assertion. It computes
upper/lower, open/closed, fixed-left-hinge, unpowered states without changing
compact block ID 144. Review this snapshot and its sixteen wire-state fixtures
alongside the sign extension when upgrading protocol; see `docs/doors.md`.

## Mob extension

The gameplay extension retains five extra item names/IDs in the ordered snapshot:
bow=858, arrow=859, string=907, gunpowder=909, bone=1038. The block palette and all
existing mappings are unchanged. `mobs.c` and `mob_packets.c` use minimal entity
and particle constants checked against the same versioned minecraft-data source:
[entities](https://github.com/PrismarineJS/minecraft-data/blob/master/data/pc/1.21.8/entities.json),
[protocol](https://github.com/PrismarineJS/minecraft-data/blob/master/data/pc/1.21.8/protocol.json),
[particles](https://github.com/PrismarineJS/minecraft-data/blob/master/data/pc/1.21.8/particles.json),
and [sounds](https://github.com/PrismarineJS/minecraft-data/blob/master/data/pc/1.21.8/sounds.json).
Named sound holders avoid storing the full sound registry. Compile-time protocol
assertions and independent packet-decoding tests guard this compatibility data.

## Circuit and note compatibility

The palette and saved block identifiers remain unchanged. `src/circuits.c` holds
minimal protocol-772 state formulas for dust, floor levers, standing redstone
torches and tuned note blocks, checked against the same pinned
`pc/1.21.8/blocks.json` reference. Dust uses the torch carrier plus a persisted
side-table kind; individual block updates supply its wire state after chunk
transmission. `src/notes.c` sends named client note-block sounds, checked against
`pc/1.21.8/sounds.json`. No complete sound registry or game assets are retained.


The controls/farming extension adds only bread=912 to the item-name snapshot.
`circuits.c` also represents redstone wall torches (5918..5925), stone/oak plates
(5826..5827, 5892..5893), and dry oak/iron trapdoors (6140..6203,
11288..11351). `farming.c` represents wheat age (4342..4349) and farmland moisture
(4350..4357). These ranges and property ordering were checked against the same
pinned `pc/1.21.8/blocks.json`; bread against `items.json`, mob voices against
`sounds.json`. Static protocol assertions require review on a version change.
The 256-entry block palette and the raw world/player/mob record layouts remain
unchanged. This adds no asset data or normal-build generation step.

## Fluid compatibility

The fluid extension adds only bucket=967, water_bucket=968 and lava_bucket=969
to the item-name snapshot, from the same pinned `pc/1.21.8/items.json` reference.
Water/lava source and flowing block states already exist in the compact palette;
no IDs or saved record layouts change. Bucket and cooling sounds use named
holders from `pc/1.21.8/sounds.json`. See `docs/fluids.md` for behavior and limits.

## Plates compatibility

Plates retain dimension type 0 (overworld) and the existing block/biome snapshots.
Login advertises catalog world keys, and Respawn supplies the selected
`lapis_obsidian:<name>` key. Volcanic uses only existing block IDs. The protocol-772
entity additions are ghast=55, zombified_piglin=148 and fireball=50, checked against
the same pinned minecraft-data entity reference. Named ghast and zombified-piglin
sounds require no asset distribution. PlayerData and BlockChange layouts remain
unchanged; the experiment keeps its saves in a separate directory.
