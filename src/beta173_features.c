#include <string.h>
#include "beta173_features.h"
#include "beta173_worldgen.h"

uint8_t *beta173_chunk_block (Beta173Chunk *chunk, int x, int y, int z) {
  if (!chunk || chunk->cx < -2048 || chunk->cx > 2047 || chunk->cz < -2048 || chunk->cz > 2047 ||
      y < 0 || y >= 128) return NULL;
  int bx = chunk->cx*16, bz = chunk->cz*16;
  if (x < bx || x >= bx+16 || z < bz || z >= bz+16) return NULL;
  return &chunk->blocks[((unsigned)(x-bx)*16 + (unsigned)(z-bz))*128 + (unsigned)y];
}

bool beta173_generate_chunk (uint64_t seed, int cx, int cz, Beta173Phase phase, Beta173Chunk *chunk) {
  if (!chunk || cx < -2048 || cx > 2047 || cz < -2048 || cz > 2047 || (unsigned)phase > BETA_DECORATION) return false;
  if (beta173_worldgen_seed() != seed) beta173_worldgen_init(seed);
  chunk->cx = cx; chunk->cz = cz;
  for (unsigned x = 0; x < 16; x ++) for (unsigned z = 0; z < 16; z ++) {
    int wx = cx*16 + (int)x, wz = cz*16 + (int)z;
    double t, h; beta173_climate(wx, wz, &t, &h);
    chunk->biomes[x*16+z] = (uint8_t)beta173_biome(t, h);
    for (unsigned y = 0; y < 128; y ++) chunk->blocks[(x*16+z)*128+y] = beta173_terrain(wx, (int)y, wz);
  }
  if (phase >= BETA_SURFACE) beta173_surface(chunk);
  if (phase >= BETA_CAVES) beta173_caves(seed, chunk);
  if (phase >= BETA_ORES) beta173_ores(seed, chunk);
  if (phase >= BETA_TREES) beta173_trees(seed, chunk);
  return true;
}
