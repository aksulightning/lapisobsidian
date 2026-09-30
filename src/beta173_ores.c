/* betanium ore ellipsoids and Beta population counts.
 * Copyright (C) 2026 Aksu Lightning; GPL-3.0-or-later. */
#include <math.h>
#include "beta173_features.h"
#include "beta173_math.h"
#include "registries.h"

static void vein (Beta173Chunk *chunk, Beta173Rng *rng, uint8_t block, int count, int x, int y, int z) {
  float angle = beta173_rng_float(rng)*BETA173_PI;
  float sx = beta173_sin(angle)*(float)count/8.0f, sz = beta173_cos(angle)*(float)count/8.0f;
  double x1 = (float)(x+8)+sx, x2 = (float)(x+8)-sx;
  double z1 = (float)(z+8)+sz, z2 = (float)(z+8)-sz;
  double y1 = y+(int)beta173_draw(rng,3)+2, y2 = y+(int)beta173_draw(rng,3)+2;
  for (int step = 0; step <= count; step ++) {
    double px = x1+(x2-x1)*step/count, py = y1+(y2-y1)*step/count, pz = z1+(z2-z1)*step/count;
    double random_radius = beta173_rng_double(rng)*count/16.0;
    float taper = beta173_sin((float)step*BETA173_PI/(float)count)+1.0f;
    double radius = ((double)taper*random_radius+1.0)/2;
    int min_x = (int)floor(px-radius), max_x = (int)floor(px+radius);
    int min_y = (int)floor(py-radius), max_y = (int)floor(py+radius);
    int min_z = (int)floor(pz-radius), max_z = (int)floor(pz+radius);
    if (min_x < chunk->cx*16) min_x = chunk->cx*16;
    if (max_x > chunk->cx*16+15) max_x = chunk->cx*16+15;
    if (min_z < chunk->cz*16) min_z = chunk->cz*16;
    if (max_z > chunk->cz*16+15) max_z = chunk->cz*16+15;
    if (min_y < 0) min_y = 0;
    if (max_y > 127) max_y = 127;
    for (int bx = min_x; bx <= max_x; bx ++) for (int by = min_y; by <= max_y; by ++) {
      double nx = (bx+0.5-px)/radius, ny = (by+0.5-py)/radius;
      if (nx*nx+ny*ny >= 1) continue;
      for (int bz = min_z; bz <= max_z; bz ++) {
        double nz = (bz+0.5-pz)/radius;
        uint8_t *at = beta173_chunk_block(chunk,bx,by,bz);
        if (nx*nx+ny*ny+nz*nz < 1 && at && *at == B_stone) *at = block;
      }
    }
  }
}

void beta173_ores (uint64_t seed, Beta173Chunk *chunk) {
  if (!chunk || chunk->cx < -2048 || chunk->cx > 2047 || chunk->cz < -2048 || chunk->cz > 2047) return;
  static const struct { uint8_t block, attempts, size, bound; } ores[] = {
    {B_dirt,20,32,128}, {B_gravel,10,32,128}, {B_coal_ore,20,16,128},
    {B_iron_ore,20,8,64}, {B_gold_ore,2,8,32}, {B_redstone_ore,8,7,16},
    {B_diamond_ore,1,7,16}, {B_lapis_ore,1,6,0}
  };
  Beta173Rng rng; beta173_rng_seed(&rng,seed);
  uint64_t odd_x = beta173_odd(beta173_rng_long(&rng)), odd_z = beta173_odd(beta173_rng_long(&rng));
  /* The +8 center offset and maximum vein size 32 put every write inside
   * source [0,31] in X/Z. Replay the four possible sources in canonical order.
   * Independent stream: omitted lakes/dungeons never shift the ore sequence. */
  for (int sx = chunk->cx-1; sx <= chunk->cx; sx ++) for (int sz = chunk->cz-1; sz <= chunk->cz; sz ++) {
    beta173_rng_seed(&rng,beta173_coordinate_seed(seed,sx,sz,odd_x,odd_z) ^ UINT64_C(0x4f524553));
    for (size_t kind = 0; kind < sizeof(ores)/sizeof(ores[0]); kind ++) {
      for (unsigned i = 0; i < ores[kind].attempts; i ++) {
        int x = sx*16+(int)beta173_draw(&rng,16);
        int y = (int)beta173_draw(&rng,ores[kind].bound ? ores[kind].bound : 16);
        if (!ores[kind].bound) y += (int)beta173_draw(&rng,16);
        int z = sz*16+(int)beta173_draw(&rng,16);
        vein(chunk,&rng,ores[kind].block,ores[kind].size,x,y,z);
      }
    }
  }
}
