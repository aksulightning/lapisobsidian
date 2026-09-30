# Beta world generation (generator version 2)

The behavior reference is betanium by Aksu Lightning, revision
03d77b9dae27a086a2398c21abe52fd07f27195c. Inspected modules: java_math, int64,
java_random, beta_noise, beta_world_gen, biome_defs, terrain_generator,
cave_generator, math_helper, ore_generator, population, structures and big_tree.
No reference runtime is a build, test, installation or server dependency.

## Terrain and arithmetic

`beta173_rng.c` implements Java's 48-bit LCG, bounded rejection sampling,
float/double generation and signed-low-word nextLong behavior. Seeds retain all
64 bits as uint64_t bit patterns. Unsigned arithmetic implements Java overflow;
`beta173_math.c` handles signed-truncating odd multipliers and coordinate seeds.
Cave and ore sine/cosine use the reference's float-quantized sine-table indices,
but compute entries on demand to avoid a 256 KiB table. Compilation disables FMA
contraction. Fast-math must not be enabled. IEEE-754 float/double are required.

`beta173_noise.c` ports Perlin and simplex, octave order, climate scaling, the
2D path, and historical Y-gradient caching in Perlin columns. Each permutation
is stored once as 256 bytes and addressed modulo 256. The terrain constructor
initializes all 82 terrain/surface/tree and ten climate permutations in reference
order. Density uses a 5x17x5 lattice, 4-block horizontal and 8-block vertical
interpolation, sea level 64, and top fade to air. Terrain spans Y=0..127.

## Feature phases

`beta173_features.c` fills a compact 16x16x128 chunk with 256 climate biome IDs.
The independently testable phases run in this order:

1. Base density, stone, water and ice.
2. `beta173_surface.c`: reference grass/dirt, desert sand, shoreline sand/gravel,
   sandstone transitions and randomized bedrock. Retains the original surface
   RNG's coordinate-only seed, draw order and column-noise cache behavior.
3. `beta173_caves.c`: reference tunnels, rooms and bounded branching. Replays
   sources within eight chunks, clips writes to the target, avoids water, adds
   lava at depth and repairs exposed grass. Preserves the historical one-block
   offset between normalized carving Y and the block-array index.
4. `beta173_ores.c`: reference ellipsoid veins with Beta dirt/gravel/coal/iron/
   gold/redstone/diamond/lapis counts, sizes and altitude distributions. Replays
   four possible source chunks in fixed order; veins replace only stone.
5. `beta173_trees.c`: ordinary oak shapes, reference tree-density noise and
   biome-dependent attempt counts. Replays the surrounding nine sources.
   Candidate clearance reads immutable terrain after caves, before vegetation.
   Canopies are clipped into each target in a consistent order. Logs take
   precedence over overlapping leaves. At most 16 attempts occur per source.
6. `beta173_decoration.c`: grass, ferns, dandelions, poppies, desert dead bushes,
   and climate/altitude-dependent snow. Uses bounded per-column placement;
   water, ice and occupied cells are preserved. Snow updates grass's snowy state.

Ores, trees and decoration have separate salted Java RNG streams. Tree candidates
also own their RNG stream, so a rejected candidate does not perturb other trees.
No feature reads player edits or mutable neighboring population state.

## Integration and memory

`worldgen.c` preserves getBlockAt/getTerrainAt/buildChunkSection, network byte
ordering and the edit overlay. Block queries, height queries and transmission
use the same final generated chunk. The adapter keeps four complete chunks in a fixed LRU cache to avoid repeated
generation during neighboring block/tick queries;
tree clearance caches two immutable pre-vegetation chunks. Changing the seed
invalidates the appropriate caches. There is no heap allocation in generation,
world-sized state, database, background work or pregeneration.

Fixed generator data on the tested 64-bit target is approximately 226 KiB:
29,160 bytes of noise/density storage, 4,096 bytes of reusable surface scratch,
and six 33,032-byte chunk structs, plus small scalar bookkeeping. This excludes
upstream registry, packet, edit and player storage. Generation is single-threaded;
these shared caches are not thread-safe.

The ten Beta climate biomes map to modern protocol identifiers without enabling
modern world generation. Rainforest maps to jungle, swampland to swamp, seasonal
forest/forest to forest, savanna to savanna, shrubland/plains to plains, taiga to
taiga, desert to desert, and tundra to snowy_plains. The inherited serializer
still sends one biome per section, selected from the chunk's center; surface
blocks use per-column climate biomes. Fine-grained client biome tinting is deferred.

## Optional horizontal mirroring

`WORLD_MIRROR_HORIZONTAL` in `include/globals.h` defaults to 0 and initializes
the startup variable `world_mirror_horizontal`. Set the constant to 1 and rebuild
to enable it. The server reflects complete generated chunks across the plane
X=-0.5: block X maps to `-X-1`, source chunk X maps to `-chunkX-1`, and local X
maps to `15-localX`. Y/Z are unchanged. This bijection preserves both compact
coordinate boundaries and reflects features that cross chunk borders. It reverses
the world, rather than duplicating one half to create bilateral symmetry.

The adapter reverses block and biome columns in place after generation, before
applying player edits. Height queries and packet sections use the reflected cache;
its key includes the startup setting. No extra chunk storage is allocated. Saved
edits and player coordinates are not transformed; this option does not convert an
existing world. The variable is a startup option, not a live gameplay toggle.

New `world.meta` files have 25 bytes: the existing 24-byte header plus a flags byte
whose bit 0 stores horizontal mirroring. Other flag bits and trailing bytes are
rejected. Earlier 24-byte version-2 headers load as mirroring disabled. Changing
the requested setting for an existing save is rejected. The generator version
stays 2 because unmirrored generation is unchanged. Metadata and packet tests
cover both modes, old headers, mismatches, biome reflection, edits and world edges.

## Intentional differences and limits

- This is Beta-style, not exact historical world hashes. Shared world-space
  climate lattice nodes and direct density interpolation differ from the reference
  chunk-local offsets and historical accumulated interpolation.
- Lakes, dungeons, clay deposits and fluid springs are omitted. Ores therefore
  use an independent sequence rather than the complete historical population RNG.
- Tree anchors cover their source chunk without the historical +8 offset.
  Clearance ignores other trees to make generation independent of exploration
  order. Only ordinary oaks are present; birch, spruce and large branching tree
  variants are deferred. Taiga currently receives the same oak material/shape.
- Ground cover uses per-column probabilities, not historical scatter attempts.
  Reeds, cacti, pumpkins, mushrooms and other decorations are deferred. Snow
  follows the reference climate/altitude rule on the implemented solid surfaces.
- At the compact X/Z boundary (-32768..32767), trees needing out-of-world
  clearance are rejected. Caves and ores can still have deterministic sources
  just beyond it, with writes clipped to valid target chunks.
- The modern overworld remains -64..319; the compatibility floor below Y=0 is
  bedrock and above Y=127 is air unless edited. Fixed spawn can be underwater.
  Height queries include trees and ground cover, so spawn can also be on a canopy.
- `world.meta` binds generator version 2, protocol and seed. Version 1 saves
  are rejected before edits load. Start a fresh world in another directory and
  retain old saves separately; no migration is provided. Raw edit/player storage
  remains upstream's layout. Do not remove metadata to bypass this check.

## Frozen reference vectors

Fixtures were generated externally from the pinned reference. Checked-in tests
read numbers and execute only C; Lua is neither shipped nor required.

| File | Rows | Method |
| --- | ---: | --- |
| `betanium-vectors.txt` | 384 | RNG, Perlin, density and height; density/height incorporate the documented shared climate lattice and direct interpolation |
| `betanium-surface-vectors.txt` | 20 | Original surface replacement on a synthetic stepped stone/water chunk with all ten biomes |
| `betanium-cave-vectors.txt` | 50 | Original cave generator on stone/dirt/grass, with and without a water plane at Y=32 |
| `betanium-ore-vectors.txt` | 25 | Original minable routine with this project's documented population counts, salt and four-source replay |
| `betanium-tree-vectors.txt` | 13 | Original ordinary oak on flat grass, plus original tree-density samples |

Base rows encode seed, kind and sample coordinates/results. Surface/ore rows
encode signed seed, chunk X/Z and FNV-1a hash. Cave rows add the water-plane mode.
Hashes cover compact block IDs in X-major, Z-major, Y-fast order (32,768 bytes),
using offset 2166136261 and multiplier 16777619 with uint32 wraparound. Surface
inputs have height `48+(x*5+z*3)%48`, water below 64 and biome `(x*16+z)%10`.
Cave inputs use bedrock at 0, grass at 127, dirt at 124..126 and stone elsewhere;
mode 1 adds water at 32. Ore inputs use bedrock at 0 and stone elsewhere. Tree
rows contain seed, trunk height, four 25-bit leaf masks (X-major/Z-fast, centered
in 5x5), and tree-density noise at world (-16,112). See `tests/features.c`.

All reference fixtures match on the development target. Additional tests check
reverse generation order over 25 chunks, actual canopies crossing an east edge,
seed/cache changes, coordinate limits, decoration placement constraints, required
ore types, water/bedrock preservation, and the production chunk packet palette
and block bytes. Full-suite ASan/UBSan tests cover these paths. To refresh fixtures,
run the pinned reference with these exact inputs; never obtain expected reference
values from the implementation under test. Generator changes require an explicit
version change and review of saved-world compatibility.
