#include <stdlib.h>
#include "plates.h"
#include "registries.h"
#include "tools.h"
static uint64_t hash (int x, int z) {
  return splitmix64(world_seed ^ ((uint64_t)(uint32_t)x<<32) ^ (uint32_t)z);
}
static int volcanic_height (int x, int z) {
  int cx = x/16-(x%16 < 0), cz = z/16-(z%16 < 0);
  int dx = x-cx*16, dz = z-cz*16;
  int a = (int)(hash(cx,cz)%49u), b = (int)(hash(cx+1,cz)%49u);
  int c = (int)(hash(cx,cz+1)%49u), d = (int)(hash(cx+1,cz+1)%49u);
  return 30+((a*(16-dx)+b*dx)*(16-dz)+(c*(16-dx)+d*dx)*dz)/256;
}
uint8_t plates_terrain (int x, int y, int z) {
  if (x < -32768 || x > 32767 || z < -32768 || z > 32767 || y < 0 || y > 255) return B_air;
  int dx = x-8, dz = z-8;
  PlateType t = plates_type();
  if (t == PLATE_FLATWORLD) return y == 0 ? B_bedrock : y < 61 ? B_stone : y < 64 ? B_dirt : y == 64 ? B_grass_block : B_air;
  if (t == PLATE_HUB) {
    if (abs(dx) > 60 || abs(dz) > 60) return B_air;
    int r = dx*dx+dz*dz;
    /* Original blue virtual arena: concentric decks, light grid and towers. */
    if (r < 2500 && y >= 61 && y <= 64) {
      if (y == 61) return B_bedrock;
      if (y == 64 && (dx%8 == 0 || dz%8 == 0 || (r > 2200 && r < 2400))) return B_glowstone;
      return y == 64 ? B_light_blue_wool : B_blue_wool;
    }
    if (r >= 2400 && r < 2600 && y >= 65 && y <= 92) return y%8 == 0 ? B_glowstone : B_blue_wool;
    if (r > 700 && r < 1100 && (abs(dx) <= 2 || abs(dz) <= 2) && y > 64 && y <= 100)
      return y%12 == 0 ? B_glowstone : B_cyan_wool;
    return B_air;
  }
  if (t == PLATE_SKYBOX) {
    if (abs(dx) <= 5 && abs(dz) <= 5 && y >= 61 && y <= 64) {
      if (y == 61) return B_stone;
      if (y < 64) return B_dirt;
      if (dx == -3 && dz == -3) return B_water;
      if (dx == 3 && dz == -3) return B_lava;
      return B_grass_block;
    }
    if (dx == 3 && dz == 3 && y >= 65 && y <= 68) return B_oak_log;
    if (abs(dx-3) <= 2 && abs(dz-3) <= 2 && y >= 68 && y <= 70) return B_oak_leaves;
    return B_air;
  }
  if (t == PLATE_VOLCANIC) {
    if (y == 0 || y == 127) return B_bedrock;
    if (y > 127) return B_air;
    if (abs(dx) <= 5 && abs(dz) <= 5 && y >= 40 && y <= 76) return y <= 64 ? B_netherrack : B_air;
    if (y >= 116) return y == 116 && hash(x,z)%29u == 0 ? B_glowstone : B_netherrack;
    int h = volcanic_height(x,z);
    if (y <= h) return (hash(x,z)^(uint64_t)(unsigned)y*31u)%113u == 0 ? B_nether_quartz_ore : B_netherrack;
    return y <= 40 ? B_lava : B_air;
  }
  return B_air;
}
