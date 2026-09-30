/* Ordinary oak canopy adapted from betanium structures/population.
 * Copyright (C) 2026 Aksu Lightning; GPL-3.0-or-later. */
#include <stdlib.h>
#include <string.h>
#include "beta173_features.h"
#include "beta173_math.h"
#include "beta173_worldgen.h"
#include "registries.h"

void beta173_oak_shape (Beta173Rng *rng, Beta173Oak *tree) {
  if (!rng || !tree) return;
  memset(tree,0,sizeof(*tree));
  tree->height = (uint8_t)(beta173_draw(rng,3)+4);
  for (int layer = 0; layer < 4; layer ++) {
    int dy = layer-3, radius = 1-dy/2;
    for (int dx = -radius; dx <= radius; dx ++) for (int dz = -radius; dz <= radius; dz ++) {
      if (abs(dx) == radius && abs(dz) == radius) {
        uint32_t corner = beta173_draw(rng,2);
        if (!corner || !dy) continue;
      }
      tree->leaves[layer] |= UINT32_C(1) << (unsigned)((dx+2)*5+dz+2);
    }
  }
}

/* Two immutable pre-vegetation chunks suffice for usual border queries. A
 * cache miss recomputes terrain; neither cache size nor exploration order
 * affects acceptance. No world-sized state or allocation is involved. */
static Beta173Chunk base[2];
static bool valid[2];
static unsigned recent;
static uint64_t base_seed;
static const Beta173Chunk *base_chunk (uint64_t seed, int cx, int cz) {
  if (base_seed != seed) { valid[0] = valid[1] = false; base_seed = seed; }
  for (unsigned i = 0; i < 2; i ++) if (valid[i] && base[i].cx == cx && base[i].cz == cz) {
    recent = i; return &base[i];
  }
  unsigned next = recent ^ 1u;
  valid[next] = beta173_generate_chunk(seed,cx,cz,BETA_CAVES,&base[next]);
  recent = next;
  return valid[next] ? &base[next] : NULL;
}
static uint8_t base_block (uint64_t seed, int x, int y, int z) {
  if (!beta173_coords_valid(x,z) || y < 0 || y >= 128) return B_bedrock;
  int cx = x/16-(x%16 < 0), cz = z/16-(z%16 < 0);
  const Beta173Chunk *chunk = base_chunk(seed,cx,cz);
  if (!chunk) return B_bedrock;
  return chunk->blocks[((unsigned)(x-cx*16)*16+(unsigned)(z-cz*16))*128+(unsigned)y];
}
static int ground_height (uint64_t seed, int x, int z) {
  for (int y = 127; y >= 0; y --) if (base_block(seed,x,y,z) != B_air) return y+1;
  return 0;
}
static bool clear_tree (uint64_t seed, int x, int y, int z, const Beta173Oak *tree) {
  if (y < 1 || y >= 127-tree->height || !beta173_coords_valid(x-2,z-2) || !beta173_coords_valid(x+2,z+2)) return false;
  uint8_t ground = base_block(seed,x,y-1,z);
  if (ground != B_grass_block && ground != B_dirt) return false;
  /* Traverse by column to keep border cache misses bounded. */
  for (int dx = -2; dx <= 2; dx ++) for (int dz = -2; dz <= 2; dz ++) {
    for (int dy = 0; dy <= tree->height+1; dy ++) {
      int radius = dy == 0 ? 0 : dy >= tree->height-1 ? 2 : 1;
      if (abs(dx) > radius || abs(dz) > radius) continue;
      if (base_block(seed,x+dx,y+dy,z+dz) != B_air) return false;
    }
  }
  return true;
}
static void place_tree (Beta173Chunk *chunk, int x, int y, int z, const Beta173Oak *tree) {
  uint8_t *ground = beta173_chunk_block(chunk,x,y-1,z);
  if (ground) *ground = B_dirt;
  for (int layer = 0; layer < 4; layer ++) for (int dx = -2; dx <= 2; dx ++) for (int dz = -2; dz <= 2; dz ++) {
    unsigned bit = (unsigned)((dx+2)*5+dz+2);
    if (!(tree->leaves[layer] & (UINT32_C(1) << bit))) continue;
    uint8_t *block = beta173_chunk_block(chunk,x+dx,y+tree->height-3+layer,z+dz);
    if (block && *block == B_air) *block = B_oak_leaves;
  }
  for (int dy = 0; dy < tree->height; dy ++) {
    uint8_t *block = beta173_chunk_block(chunk,x,y+dy,z);
    if (block && (*block == B_air || *block == B_oak_leaves)) *block = B_oak_log;
  }
}

void beta173_trees (uint64_t seed, Beta173Chunk *chunk) {
  if (!chunk || chunk->cx < -2048 || chunk->cx > 2047 || chunk->cz < -2048 || chunk->cz > 2047) return;
  if (beta173_worldgen_seed() != seed) beta173_worldgen_init(seed);
  Beta173Rng rng; beta173_rng_seed(&rng,seed);
  uint64_t odd_x = beta173_odd(beta173_rng_long(&rng)), odd_z = beta173_odd(beta173_rng_long(&rng));
  /* Anchors cover a source chunk, canopies reach at most two blocks outward. */
  for (int sx = chunk->cx-1; sx <= chunk->cx+1; sx ++) for (int sz = chunk->cz-1; sz <= chunk->cz+1; sz ++) {
    if (sx < -2048 || sx > 2047 || sz < -2048 || sz > 2047) continue;
    beta173_rng_seed(&rng,beta173_coordinate_seed(seed,sx,sz,odd_x,odd_z) ^ UINT64_C(0x54524545));
    double t, h; beta173_climate(sx*16+8,sz*16+8,&t,&h);
    Beta173Biome biome = beta173_biome(t,h);
    int count = beta173_draw(&rng,10) == 0 ? 1 : 0;
    double density = beta173_tree_density(sx*16,sz*16);
    int extra = (int)((density/8+beta173_rng_double(&rng)*4+4)/3);
    if (biome == BETA_FOREST || biome == BETA_RAINFOREST || biome == BETA_TAIGA) count += extra+5;
    else if (biome == BETA_SEASONAL_FOREST) count += extra+2;
    else if (biome == BETA_DESERT || biome == BETA_TUNDRA || biome == BETA_PLAINS) count -= 20;
    if (count > 16) count = 16; /* Explicit work bound per source. */
    for (int i = 0; i < count; i ++) {
      int x = sx*16+(int)beta173_draw(&rng,16), z = sz*16+(int)beta173_draw(&rng,16);
      Beta173Rng candidate; beta173_rng_seed(&candidate,beta173_rng_long(&rng));
      if (x+2 < chunk->cx*16 || x-2 >= chunk->cx*16+16 || z+2 < chunk->cz*16 || z-2 >= chunk->cz*16+16) continue;
      Beta173Oak tree; beta173_oak_shape(&candidate,&tree);
      int y = ground_height(seed,x,z);
      if (clear_tree(seed,x,y,z,&tree)) place_tree(chunk,x,y,z,&tree);
    }
  }
}
