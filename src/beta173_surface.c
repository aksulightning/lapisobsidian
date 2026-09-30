/* Port of betanium terrain surface rules, Copyright (C) 2026 Aksu Lightning.
 * GPL-3.0-or-later. */
#include "beta173_features.h"
#include "beta173_worldgen.h"
#include "registries.h"

void beta173_surface (Beta173Chunk *chunk) {
  if (!chunk || chunk->cx < -2048 || chunk->cx > 2047 || chunk->cz < -2048 || chunk->cz > 2047) return;
  /* Fixed scratch storage, reused by the single-threaded generator. */
  static double sand[256], stone[256];
  for (unsigned x = 0; x < 16; x ++) {
    beta173_surface_fields(chunk->cx * 16 + (int)x, chunk->cz * 16, sand + x*16, stone + x*16);
  }
  Beta173Rng rng;
  uint64_t seed = (uint64_t)(int64_t)chunk->cx * UINT64_C(341873128712) +
    (uint64_t)(int64_t)chunk->cz * UINT64_C(132897987541);
  beta173_rng_seed(&rng, seed);
  for (unsigned z = 0; z < 16; z ++) for (unsigned x = 0; x < 16; x ++) {
    unsigned col = x*16 + z;
    bool sandy = sand[col] + beta173_rng_double(&rng)*0.2 > 0;
    bool gravelly = beta173_gravel_noise(chunk->cx*16 + (int)x, chunk->cz*16 + (int)z) + beta173_rng_double(&rng)*0.2 > 3;
    int filler_depth = (int)(stone[col]/3 + 3 + beta173_rng_double(&rng)*0.25);
    uint8_t top = chunk->biomes[col] == BETA_DESERT ? B_sand : B_grass_block;
    uint8_t filler = chunk->biomes[col] == BETA_DESERT ? B_sand : B_dirt;
    int left = -1;
    for (int y = 127; y >= 0; y --) {
      uint32_t bedrock = 0;
      beta173_rng_bound(&rng, 5, &bedrock);
      uint8_t *block = &chunk->blocks[col*128 + (unsigned)y];
      if ((unsigned)y <= bedrock) { *block = B_bedrock; continue; }
      if (*block == B_air) { left = -1; continue; }
      if (*block != B_stone) continue;
      if (left == -1) {
        if (filler_depth <= 0) { top = B_air; filler = B_stone; }
        else if (y >= 60 && y <= 65) {
          top = chunk->biomes[col] == BETA_DESERT ? B_sand : B_grass_block;
          filler = chunk->biomes[col] == BETA_DESERT ? B_sand : B_dirt;
          if (gravelly) { top = B_air; filler = B_gravel; }
          if (sandy) { top = B_sand; filler = B_sand; }
        }
        if (y < 64 && top == B_air) top = B_water;
        left = filler_depth;
        *block = y >= 63 ? top : filler;
      } else if (left > 0) {
        left --; *block = filler;
        if (!left && filler == B_sand) {
          uint32_t depth = 0; beta173_rng_bound(&rng, 4, &depth);
          left = (int)depth; filler = B_sandstone;
        }
      }
    }
  }
}
