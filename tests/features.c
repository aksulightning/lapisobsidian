#include <assert.h>
#include <inttypes.h>
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
int main (void) {
  surface_vectors();
  cave_vectors();
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
  puts("features: 20 surface and 50 cave reference vectors, biomes, bounds and generation order passed");
  return 0;
}
