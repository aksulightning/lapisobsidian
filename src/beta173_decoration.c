/* Compact Beta-style ground cover. Climate/snow rule adapted from betanium,
 * Copyright (C) 2026 Aksu Lightning; GPL-3.0-or-later. */
#include "beta173_features.h"
#include "beta173_math.h"
#include "beta173_worldgen.h"
#include "registries.h"

void beta173_decoration (uint64_t seed, Beta173Chunk *chunk) {
  if (!chunk || chunk->cx < -2048 || chunk->cx > 2047 || chunk->cz < -2048 || chunk->cz > 2047) return;
  if (beta173_worldgen_seed() != seed) beta173_worldgen_init(seed);
  Beta173Rng rng; beta173_rng_seed(&rng,seed);
  uint64_t odd_x = beta173_odd(beta173_rng_long(&rng)), odd_z = beta173_odd(beta173_rng_long(&rng));
  for (unsigned x = 0; x < 16; x ++) for (unsigned z = 0; z < 16; z ++) {
    unsigned col = x*16+z; int y = 127;
    while (y > 0 && chunk->blocks[col*128+(unsigned)y] == B_air) y --;
    if (y >= 127) continue;
    uint8_t *ground = &chunk->blocks[col*128+(unsigned)y], *above = ground+1;
    int wx = chunk->cx*16+(int)x, wz = chunk->cz*16+(int)z;
    double temperature, humidity; beta173_climate(wx,wz,&temperature,&humidity);
    if (temperature-(y+1-64)/64.0*0.3 < 0.5) {
      if (*ground == B_grass_block || *ground == B_dirt || *ground == B_stone ||
          *ground == B_sand || *ground == B_gravel || *ground == B_oak_leaves || *ground == B_oak_log) {
        *above = B_snow;
        if (*ground == B_grass_block) *ground = B_snowy_grass_block;
      }
      continue;
    }
    /* Each column owns its draw stream. Ground-cover writes never cross a
     * chunk boundary or depend on edits or another chunk's population state. */
    beta173_rng_seed(&rng,beta173_coordinate_seed(seed,wx,wz,odd_x,odd_z) ^ UINT64_C(0x464c4f5241));
    unsigned roll = beta173_draw(&rng,256);
    Beta173Biome biome = (Beta173Biome)chunk->biomes[col];
    if (biome == BETA_DESERT && *ground == B_sand) {
      if (roll < 8) *above = B_dead_bush;
      continue;
    }
    if (*ground != B_grass_block && *ground != B_dirt) continue;
    if (roll < 2) { *above = B_dandelion; continue; }
    if (roll == 2) { *above = B_poppy; continue; }
    unsigned grasses = 0;
    if (biome == BETA_FOREST || biome == BETA_SEASONAL_FOREST) grasses = 32;
    else if (biome == BETA_RAINFOREST || biome == BETA_PLAINS) grasses = 64;
    else if (biome == BETA_TAIGA) grasses = 16;
    if (roll >= 3 && roll < grasses+3) {
      *above = biome == BETA_RAINFOREST && beta173_draw(&rng,3) != 0 ? B_fern : B_short_grass;
    }
  }
}
