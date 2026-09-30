#include <assert.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "beta173_features.h"
#include "beta173_worldgen.h"
#include "registries.h"

static Beta173Chunk chunk, repeat;
static uint32_t hash (const Beta173Chunk *value) {
  uint32_t result = UINT32_C(2166136261);
  for (size_t i = 0; i < sizeof(value->blocks); i ++) result = (result ^ value->blocks[i]) * UINT32_C(16777619);
  return result;
}
static void surface_vectors (void) {
  FILE *file = fopen("tests/betanium-surface-vectors.txt", "r"); assert(file);
  char seed_text[32]; uint32_t expected; unsigned count = 0;
  while (fscanf(file, "%31s %d %d %" SCNu32, seed_text, &chunk.cx, &chunk.cz, &expected) == 4) {
    uint64_t seed; assert(beta173_seed_parse(seed_text, &seed)); beta173_worldgen_init(seed);
    for (unsigned x = 0; x < 16; x ++) for (unsigned z = 0; z < 16; z ++) {
      unsigned col = x*16+z, height = 48+(x*5+z*3)%48;
      chunk.biomes[col] = (uint8_t)(col % BETA_BIOME_COUNT);
      for (unsigned y = 0; y < 128; y ++) chunk.blocks[col*128+y] = y <= height ? B_stone : y < 64 ? B_water : B_air;
    }
    beta173_surface(&chunk);
    if (hash(&chunk) != expected) fprintf(stderr, "surface %s %d %d: got %" PRIu32 " expected %" PRIu32 "\n", seed_text, chunk.cx, chunk.cz, hash(&chunk), expected);
    assert(hash(&chunk) == expected); count ++;
  }
  assert(feof(file) && count == 20); fclose(file);
}
static void cave_vectors (void) {
  FILE *file = fopen("tests/betanium-cave-vectors.txt", "r"); assert(file);
  char seed_text[32]; uint32_t expected; unsigned count = 0; int mode;
  while (fscanf(file, "%31s %d %d %d %" SCNu32, seed_text, &chunk.cx, &chunk.cz, &mode, &expected) == 5) {
    uint64_t seed; assert(beta173_seed_parse(seed_text, &seed));
    for (unsigned i = 0; i < BETA173_CHUNK_BLOCKS; i ++) {
      unsigned y = i%128;
      chunk.blocks[i] = mode == 1 && y == 32 ? B_water : y == 0 ? B_bedrock : y == 127 ? B_grass_block : y >= 124 ? B_dirt : B_stone;
    }
    beta173_caves(seed, &chunk);
    if (hash(&chunk) != expected) fprintf(stderr, "caves %s %d %d mode %d: got %" PRIu32 " expected %" PRIu32 "\n", seed_text, chunk.cx, chunk.cz, mode, hash(&chunk), expected);
    assert(hash(&chunk) == expected); count ++;
    for (unsigned i = 0; i < 256; i ++) {
      assert(chunk.blocks[i*128] == B_bedrock);
      if (mode == 1) assert(chunk.blocks[i*128+32] == B_water);
    }
  }
  assert(feof(file) && count == 50); fclose(file);
}
static void ore_vectors (void) {
  FILE *file = fopen("tests/betanium-ore-vectors.txt", "r"); assert(file);
  char seed_text[32]; uint32_t expected; unsigned count = 0, totals[256] = {0};
  while (fscanf(file, "%31s %d %d %" SCNu32, seed_text, &chunk.cx, &chunk.cz, &expected) == 4) {
    uint64_t seed; assert(beta173_seed_parse(seed_text, &seed));
    for (unsigned i = 0; i < BETA173_CHUNK_BLOCKS; i ++) chunk.blocks[i] = i%128 == 0 ? B_bedrock : B_stone;
    beta173_ores(seed, &chunk);
    if (hash(&chunk) != expected) fprintf(stderr, "ores %s %d %d: got %" PRIu32 " expected %" PRIu32 "\n", seed_text, chunk.cx, chunk.cz, hash(&chunk), expected);
    assert(hash(&chunk) == expected); count ++;
    for (unsigned i = 0; i < BETA173_CHUNK_BLOCKS; i ++) {
      totals[chunk.blocks[i]] ++;
      if (i%128 == 0) assert(chunk.blocks[i] == B_bedrock);
      if (chunk.blocks[i] == B_diamond_ore || chunk.blocks[i] == B_redstone_ore) assert(i%128 < 22);
    }
  }
  assert(feof(file) && count == 25); fclose(file);
  assert(totals[B_coal_ore] && totals[B_iron_ore] && totals[B_gold_ore] && totals[B_redstone_ore] && totals[B_lapis_ore] && totals[B_diamond_ore]);
}
static void tree_vectors (void) {
  FILE *file = fopen("tests/betanium-tree-vectors.txt", "r"); assert(file);
  char seed_text[32]; unsigned height, count = 0; uint32_t masks[4]; double density;
  while (fscanf(file, "%31s %u %" SCNu32 " %" SCNu32 " %" SCNu32 " %" SCNu32 " %lf", seed_text, &height, &masks[0], &masks[1], &masks[2], &masks[3], &density) == 7) {
    uint64_t seed; assert(beta173_seed_parse(seed_text, &seed));
    Beta173Rng rng; beta173_rng_seed(&rng, seed); Beta173Oak tree;
    beta173_oak_shape(&rng, &tree); assert(tree.height == height);
    assert(memcmp(tree.leaves,masks,sizeof(masks)) == 0);
    beta173_worldgen_init(seed);
    assert(fabs(beta173_tree_density(-16,112)-density) < 1e-12); count ++;
  }
  assert(feof(file) && count == 13); fclose(file);
}
static void population_order (void) {
  uint32_t hashes[25]; unsigned logs = 0, border_checks = 0;
  for (int x = -2; x <= 2; x ++) for (int z = -2; z <= 2; z ++) {
    assert(beta173_generate_chunk(0,x,z,BETA_DECORATION,&chunk));
    hashes[(x+2)*5+z+2] = hash(&chunk);
    for (unsigned i = 0; i < BETA173_CHUNK_BLOCKS; i ++) if (chunk.blocks[i] == B_oak_log) logs ++;
    /* A trunk on an east boundary must have the adjacent non-corner canopy.
     * Check the other chunk through independently generated production phases. */
    for (int lz = 0; lz < 16; lz ++) for (int y = 10; y < 127; y ++) {
      unsigned i = (unsigned)((15*16+lz)*128+y);
      if (chunk.blocks[i] != B_oak_log || chunk.blocks[i+1] == B_oak_log) continue;
      assert(beta173_generate_chunk(0,x+1,z,BETA_DECORATION,&repeat));
      uint8_t neighbor = repeat.blocks[(unsigned)(lz*128+y)];
      assert(neighbor == B_oak_leaves || neighbor == B_oak_log); border_checks ++;
    }
  }
  for (int x = 2; x >= -2; x --) for (int z = 2; z >= -2; z --) {
    assert(beta173_generate_chunk(0,x,z,BETA_DECORATION,&chunk));
    assert(hash(&chunk) == hashes[(x+2)*5+z+2]);
  }
  assert(logs > 0 && border_checks > 0);
}
static void decorations (void) {
  unsigned counts[256] = {0};
  for (int cx = -2048; cx <= 2047; cx += 128) {
    chunk.cx = cx; chunk.cz = cx/2;
    memset(chunk.blocks,B_air,sizeof(chunk.blocks));
    for (unsigned col = 0; col < 256; col ++) {
      chunk.biomes[col] = (uint8_t)(col % BETA_BIOME_COUNT);
      for (unsigned y = 0; y < 63; y ++) chunk.blocks[col*128+y] = B_stone;
      chunk.blocks[col*128+63] = col%10 == BETA_DESERT ? B_sand : B_grass_block;
      if (col%16 == 0) chunk.blocks[col*128+63] = B_water;
      if (col%16 == 1) chunk.blocks[col*128+127] = B_stone;
    }
    repeat = chunk;
    beta173_decoration(0,&chunk);
    beta173_decoration(0,&repeat);
    assert(memcmp(&chunk,&repeat,sizeof(chunk)) == 0);
    for (unsigned col = 0; col < 256; col ++) {
      for (unsigned y = 0; y < 63; y ++) assert(chunk.blocks[col*128+y] == B_stone);
      uint8_t cover = chunk.blocks[col*128+64]; counts[cover] ++;
      if (col%16 == 0 || col%16 == 1) assert(cover == B_air);
      if (cover == B_dead_bush) assert(chunk.blocks[col*128+63] == B_sand);
      if (cover == B_short_grass || cover == B_fern || cover == B_dandelion || cover == B_poppy) assert(chunk.blocks[col*128+63] == B_grass_block);
    }
  }
  assert(counts[B_short_grass] && counts[B_fern] && counts[B_dandelion] && counts[B_poppy] && counts[B_dead_bush] && counts[B_snow]);
  assert(beta173_generate_chunk(UINT64_MAX,-2048,-2048,BETA_DECORATION,&chunk));
  assert(beta173_generate_chunk(1,2047,2047,BETA_DECORATION,&repeat));
  assert(beta173_generate_chunk(UINT64_MAX,-2048,-2048,BETA_DECORATION,&repeat));
  assert(memcmp(&chunk,&repeat,sizeof(chunk)) == 0);
}
int main (void) {
  surface_vectors();
  cave_vectors();
  ore_vectors();
  tree_vectors();
  population_order();
  decorations();
  assert(beta173_biome(0.05, 1) == BETA_TUNDRA);
  assert(beta173_biome(1, 0) == BETA_DESERT);
  assert(beta173_biome(1, 1) == BETA_RAINFOREST);
  assert(beta173_biome_protocol(BETA_TAIGA) == W_taiga);
  assert(beta173_biome_protocol(BETA_TUNDRA) == W_snowy_plains);
  assert(beta173_biome_protocol(BETA_SWAMPLAND) == W_swamp);
  assert(beta173_biome_protocol(BETA_BIOME_COUNT) == W_plains);
  assert(!beta173_generate_chunk(0, -2049, 0, BETA_SURFACE, &chunk));
  assert(!beta173_generate_chunk(0, 0, 2048, BETA_SURFACE, &chunk));
  assert(!beta173_generate_chunk(0, 0, 0, BETA_SURFACE, NULL));
  assert(beta173_generate_chunk(1, -1, 0, BETA_SURFACE, &chunk));
  assert(!beta173_chunk_block(&chunk, -17, 10, 0));
  assert(!beta173_chunk_block(&chunk, -1, -1, 0));
  assert(!beta173_chunk_block(&chunk, -1, 128, 0));
  assert(!beta173_chunk_block(NULL, -1, 10, 0));
  assert(beta173_chunk_block(&chunk, -1, 10, 0));
  assert(beta173_generate_chunk(2, 100, 200, BETA_SURFACE, &repeat));
  assert(beta173_generate_chunk(1, -1, 0, BETA_SURFACE, &repeat));
  assert(memcmp(&chunk, &repeat, sizeof(chunk)) == 0);
  puts("features: 108 feature reference vectors, biomes, decoration, bounds, canopy borders and reverse generation order passed");
  return 0;
}
