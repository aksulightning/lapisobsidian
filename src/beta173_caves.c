/* Port of betanium cave_generator, Copyright (C) 2026 Aksu Lightning.
 * GPL-3.0-or-later. Float intermediates and RNG draw order are intentional. */
#include <math.h>
#include "beta173_features.h"
#include "beta173_math.h"
#include "registries.h"

static unsigned index_at (int x, int y, int z) { return ((unsigned)x*16+(unsigned)z)*128+(unsigned)y; }
static bool carve (Beta173Chunk *chunk, double x, double y, double z, double horizontal, double vertical) {
  int ox = chunk->cx*16, oz = chunk->cz*16;
  int min_x = (int)floor(x-horizontal)-ox-1, max_x = (int)floor(x+horizontal)-ox+1;
  int min_z = (int)floor(z-horizontal)-oz-1, max_z = (int)floor(z+horizontal)-oz+1;
  int min_y = (int)floor(y-vertical)-1, max_y = (int)floor(y+vertical)+1;
  if (min_x < 0) min_x = 0;
  if (max_x > 16) max_x = 16;
  if (min_z < 0) min_z = 0;
  if (max_z > 16) max_z = 16;
  if (min_y < 1) min_y = 1;
  if (max_y > 120) max_y = 120;
  for (int lx = min_x; lx < max_x; lx ++) for (int lz = min_z; lz < max_z; lz ++) {
    for (int ly = max_y+1; ly >= min_y-1; ly --) if (ly >= 0 && ly < 128) {
      if (chunk->blocks[index_at(lx,ly,lz)] == B_water) return false;
      if (ly != min_y-1 && lx != min_x && lx != max_x-1 && lz != min_z && lz != max_z-1) ly = min_y;
    }
  }
  for (int lx = min_x; lx < max_x; lx ++) for (int lz = min_z; lz < max_z; lz ++) {
    double nx = (lx+ox+0.5-x)/horizontal, nz = (lz+oz+0.5-z)/horizontal;
    if (nx*nx+nz*nz >= 1) continue;
    bool grass = false;
    /* Historical indexing: the tested block is one above the normalized Y. */
    for (int ly = max_y-1; ly >= min_y; ly --) {
      double ny = (ly+0.5-y)/vertical;
      if (ny <= -0.7 || nx*nx+ny*ny+nz*nz >= 1) continue;
      unsigned index = index_at(lx,ly+1,lz); uint8_t block = chunk->blocks[index];
      if (block == B_grass_block) grass = true;
      if (block != B_stone && block != B_dirt && block != B_grass_block) continue;
      chunk->blocks[index] = ly < 10 ? B_lava : B_air;
      if (ly >= 10 && grass && chunk->blocks[index-1] == B_dirt) chunk->blocks[index-1] = B_grass_block;
    }
  }
  return true;
}

static void tunnel (Beta173Rng *source, Beta173Chunk *chunk, double x, double y, double z,
    float radius, float yaw, float pitch, int step, int total, double vertical_scale) {
  Beta173Rng rng; beta173_rng_seed(&rng, beta173_rng_long(source));
  if (total <= 0) total = 112-(int)beta173_draw(&rng,28);
  bool room = step == -1;
  if (room) step = total/2;
  int branch = (int)beta173_draw(&rng,(uint32_t)(total/2))+total/4;
  bool steep = beta173_draw(&rng,6) == 0;
  float yaw_velocity = 0, pitch_velocity = 0;
  int center_x = chunk->cx*16+8, center_z = chunk->cz*16+8;
  for (; step < total; step ++) {
    float angle = (float)step*BETA173_PI/(float)total;
    double horizontal = 1.5 + (double)(beta173_sin(angle)*radius), vertical = horizontal*vertical_scale;
    float cp = beta173_cos(pitch);
    x += (double)(beta173_cos(yaw)*cp); y += beta173_sin(pitch); z += (double)(beta173_sin(yaw)*cp);
    pitch *= steep ? 0.92f : 0.7f;
    pitch += pitch_velocity*0.1f; yaw += yaw_velocity*0.1f;
    pitch_velocity *= 0.9f; yaw_velocity *= 0.75f;
    float a = beta173_rng_float(&rng), b = beta173_rng_float(&rng), c = beta173_rng_float(&rng);
    pitch_velocity += ((a-b)*c)*2.0f;
    a = beta173_rng_float(&rng); b = beta173_rng_float(&rng); c = beta173_rng_float(&rng);
    yaw_velocity += ((a-b)*c)*4.0f;
    if (!room && step == branch && radius > 1.0f) {
      /* Children have radius <= 1, so recursion is bounded to one branch level. */
      float child = beta173_rng_float(&rng)*0.5f+0.5f;
      tunnel(source,chunk,x,y,z,child,yaw-BETA173_PI*0.5f,pitch/3.0f,step,total,1);
      child = beta173_rng_float(&rng)*0.5f+0.5f;
      tunnel(source,chunk,x,y,z,child,yaw+BETA173_PI*0.5f,pitch/3.0f,step,total,1);
      return;
    }
    if (!room && beta173_draw(&rng,4) == 0) continue;
    double dx = x-center_x, dz = z-center_z, left = total-step;
    double reach = (double)((radius+2.0f)+16.0f);
    if (dx*dx+dz*dz-left*left > reach*reach) return;
    if (x < center_x-16-horizontal*2 || x > center_x+16+horizontal*2 ||
        z < center_z-16-horizontal*2 || z > center_z+16+horizontal*2) continue;
    if (carve(chunk,x,y,z,horizontal,vertical) && room) break;
  }
}

static void source_caves (Beta173Rng *rng, int sx, int sz, Beta173Chunk *chunk) {
  uint32_t inner = beta173_draw(rng,40)+1, middle = beta173_draw(rng,inner)+1;
  uint32_t systems = beta173_draw(rng,middle);
  if (beta173_draw(rng,15) != 0) systems = 0;
  for (uint32_t i = 0; i < systems; i ++) {
    int x = sx*16+(int)beta173_draw(rng,16);
    uint32_t ymax = beta173_draw(rng,120)+8;
    int y = (int)beta173_draw(rng,ymax), z = sz*16+(int)beta173_draw(rng,16);
    uint32_t branches = 1;
    if (beta173_draw(rng,4) == 0) {
      float radius = 1.0f+beta173_rng_float(rng)*6.0f;
      tunnel(rng,chunk,x,y,z,radius,0,0,-1,-1,0.5);
      branches += beta173_draw(rng,4);
    }
    for (uint32_t j = 0; j < branches; j ++) {
      float yaw = (beta173_rng_float(rng)*BETA173_PI)*2.0f;
      float pitch = ((beta173_rng_float(rng)-0.5f)*2.0f)/8.0f;
      float a = beta173_rng_float(rng), b = beta173_rng_float(rng);
      tunnel(rng,chunk,x,y,z,a*2.0f+b,yaw,pitch,0,0,1);
    }
  }
}
void beta173_caves (uint64_t seed, Beta173Chunk *chunk) {
  if (!chunk || chunk->cx < -2048 || chunk->cx > 2047 || chunk->cz < -2048 || chunk->cz > 2047) return;
  Beta173Rng rng; beta173_rng_seed(&rng,seed);
  uint64_t odd_x = beta173_odd(beta173_rng_long(&rng)), odd_z = beta173_odd(beta173_rng_long(&rng));
  for (int x = chunk->cx-8; x <= chunk->cx+8; x ++) for (int z = chunk->cz-8; z <= chunk->cz+8; z ++) {
    beta173_rng_seed(&rng,beta173_coordinate_seed(seed,x,z,odd_x,odd_z));
    source_caves(&rng,x,z,chunk);
  }
}
