/* Intentional Lapis Obsidian terrain, not an emulation of Java overflow. */
#include "farlands.h"
#include "beta173_noise.h"
#include "registries.h"
#include "world_border.h"

void farlands_apply (uint64_t seed, bool mirror, Beta173Chunk *chunk) {
  if (!chunk || chunk->cx < -2048 || chunk->cx > 2047 || chunk->cz < -2048 || chunk->cz > 2047) return;
  int bx = chunk->cx*16, bz = chunk->cz*16;
  if (bx > -WORLD_FAR_LANDS_START && bx+15 < WORLD_FAR_LANDS_START &&
      bz > -WORLD_FAR_LANDS_START && bz+15 < WORLD_FAR_LANDS_START) return;
  Beta173Rng rng; Beta173Noise wall, holes;
  beta173_rng_seed(&rng,seed ^ UINT64_C(0x4c41504953464152));
  beta173_noise_init(&wall,&rng); beta173_noise_init(&holes,&rng);
  for (int x = 0; x < 16; x++) for (int z = 0; z < 16; z++) {
    int wx = bx+x, wz = bz+z;
    if (wx > -WORLD_FAR_LANDS_START && wx < WORLD_FAR_LANDS_START &&
        wz > -WORLD_FAR_LANDS_START && wz < WORLD_FAR_LANDS_START) continue;
    /* Region limits stay at physical +/-3940 even when noise is reflected. */
    int nx = mirror ? -wx-1 : wx;
    uint8_t *column = &chunk->blocks[(unsigned)(x*16+z)*128];
    for (int y = 16; y < 128; y++) {
      double density = beta173_perlin(&wall,nx/24.0,y/60.0,wz/24.0) +
        0.5*beta173_perlin(&holes,nx/9.0,y/10.0,wz/9.0) + 0.10;
      if (y > 116) density -= (y-116)/12.0;
      column[y] = density > 0 ? B_stone : y < 64 ? B_water : B_air;
    }
    /* Grass caps and shallow dirt on exposed ledges; retain deep strata. */
    for (int y = 126; y >= 64; y--) if (column[y] == B_stone && column[y+1] == B_air) {
      column[y] = B_grass_block;
      for (int d = 1; d <= 2 && column[y-d] == B_stone; d++) column[y-d] = B_dirt;
    }
  }
}
