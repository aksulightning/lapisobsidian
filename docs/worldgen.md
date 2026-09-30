# Beta terrain foundation (generator version 1)

The behavior reference is betanium by Aksu Lightning, revision
03d77b9dae27a086a2398c21abe52fd07f27195c. Inspected modules: java_math, int64,
java_random, beta_noise, beta_world_gen, biome_defs and terrain_generator.
No reference runtime is a build, test, installation or server dependency.

`beta173_rng.c` implements the Java 48-bit LCG, bounded rejection sampling,
double generation and signed-low-word nextLong behavior. Seeds retain all 64
bits, represented as uint64_t bit patterns. Unsigned multiplication provides
defined wraparound for climate seed multipliers; parsing accepts Java's signed
64-bit decimal seed range. Terrain needs floor, float rounding, and interpolation;
it does not yet need the reference java_math sine/cosine helpers used by caves.

`beta173_noise.c` ports Perlin and simplex, octave order, climate scaling, the
2D path, and the historical Y-gradient cache in Perlin columns. Each permutation
is stored once as 256 bytes and addressed modulo 256. Compilation disables FMA
contraction. Fast-math must not be enabled. The port assumes IEEE-754 float/double.

`beta173_worldgen.c` initializes 66 terrain noise permutations and 10 climate
permutations. It consumes the RNG calls for the two unused surface generators
to maintain the reference noise sequence. Density uses a 5x17x5 lattice, with
4-block horizontal and 8-block vertical interpolation, sea level 64, and top
fade to air. A single density lattice is cached, keyed by chunk coordinate and
invalidated on seed initialization. Fixed noise/cache storage totals 24,680 bytes
on the tested 64-bit target, excluding small scalar bookkeeping. There is no
world-sized allocation, heap allocation or pregeneration.

`worldgen.c` preserves the existing getBlockAt/getTerrainAt/buildChunkSection
entry points, chunk_section byte ordering and edit overlay. Height queries and
chunk transmission share the same density source. It skips chest payload records
when applying edits. The old hash terrain and its features are removed from the
active path. Protocol biome IDs are deliberately still plains in Milestone 1;
climate influences density and sea-surface ice but does not yet add surfaces.

## Intentional differences and limits

- Climate is evaluated at shared world-space lattice nodes. The reference's
  chunk-local sampling offsets do not always agree at chunk edges; this port
  guarantees identical boundary nodes. It is Beta-style, not a claim of exact
  original Beta or reference chunk hashes.
- The port interpolates directly between density corners rather than accumulating
  stepwise floating-point increments. Near-zero density decisions can therefore
  differ from historical iterative interpolation.
- No caves, ores, trees, structures, surface materials or decorations are enabled.
  Modern biomes are not generated. Spawn selection remains the upstream fixed
  X/Z height query and can be underwater for some seeds.
- The core is single-threaded; the one-entry cache is not thread-safe. The compact
  upstream X/Z range is retained. Requests outside it fail or return air without
  overflowing. The client's overworld dimension remains -64..319 for compatibility.
- Raw edit/player storage remains upstream's layout. `world.meta` is a separate
  fixed 24-byte, big-endian header: magic, generator version, protocol, seed.
  Metadata prevents accidental legacy terrain/seed mixing. This is not a general
  world-file migration or portability system.

## Frozen reference vectors

`tests/betanium-vectors.txt` contains 384 external reference results for seeds
0, 1, -1, INT64_MAX, INT64_MIN and 2701385324. Rows encode:

| Kind | Fields after seed |
| --- | --- |
| R | sequence number, signed nextInt |
| B | bound, nextInt(bound) |
| N | x, y, z, Perlin result |
| D | chunk x, chunk z, lattice index, density |
| H | world x, world z, highest stone y |

R/B/N are direct outputs of the reference RNG/noise modules. D/H use the
reference terrain fields and density formulas with the documented shared climate
lattice and direct interpolation. The generation harness was run externally;
the checked-in tests read only numbers and execute only C. All 384 values matched
exactly on the development target. C tests use a small floating-point tolerance
for other platforms, plus exact shared-border and generation-order comparisons.
To refresh, use the pinned reference with the same seeds, sample locations,
climate lattice and formulas; inspect differences before changing generator
version or fixtures. Never derive expected fixtures from the implementation
under test.
