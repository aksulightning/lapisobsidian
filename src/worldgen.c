#include <string.h>
#include "globals.h"
#include "tools.h"
#include "registries.h"
#include "procedures.h"
#include "worldgen.h"
#include "beta173_worldgen.h"

static uint64_t active_seed;
static uint8_t initialized;
uint8_t chunk_section[4096];

static void ensure_generator (void) {
  if (!initialized || active_seed != world_seed) {
    beta173_worldgen_init(world_seed);
    active_seed = world_seed;
    initialized = 1;
  }
}

uint32_t getChunkHash (short x, short z) {
  uint64_t coordinates = (uint16_t)x | ((uint64_t)(uint16_t)z << 16);
  return (uint32_t)splitmix64(coordinates ^ world_seed);
}

uint8_t getChunkBiome (short x, short z) {
  (void)x; (void)z;
  /* Climate already shapes terrain. Protocol biome/surface mapping is Milestone 2. */
  return W_plains;
}

uint8_t getHeightAt (int x, int z) {
  ensure_generator();
  return beta173_height(x, z);
}

uint8_t getTerrainAt (int x, int y, int z, ChunkAnchor anchor) {
  (void)anchor;
  ensure_generator();
  return beta173_terrain(x, y, z);
}

uint8_t getBlockAt (int x, int y, int z) {
  if (!beta173_coords_valid(x, z)) return B_air;
  if (y < 0) return B_bedrock;
  if (y <= 255) {
    uint8_t change = getBlockChange(x, y, z);
    if (change != 0xff) return change;
  }
  ensure_generator();
  return beta173_terrain(x, y, z);
}

uint8_t buildChunkSection (int cx, int cy, int cz) {
  memset(chunk_section, B_air, sizeof(chunk_section));
  if (cx < BETA173_MIN_COORD || cx > BETA173_MAX_COORD - 15 ||
      cz < BETA173_MIN_COORD || cz > BETA173_MAX_COORD - 15 || cy < -64 || cy > 304 ||
      cx % 16 != 0 || cy % 16 != 0 || cz % 16 != 0) return W_plains;
  ensure_generator();
  for (unsigned i = 0; i < 4096; i ++) {
    int x = cx + (int)(i % 16), z = cz + (int)((i / 16) % 16);
    int y = cy + (int)(i / 256);
    /* The network palette packs eight 8-bit IDs per big-endian long. */
    chunk_section[i ^ 7u] = beta173_terrain(x, y, z);
  }
  for (int i = 0; i < block_changes_count; i ++) {
    const BlockChange *change = &block_changes[i];
    if (change->block == 0xff) continue;
    /* Chest slots occupy subsequent edit records; never interpret them as blocks. */
    if (change->block == B_chest) {
      #ifdef ALLOW_CHESTS
      i += 14;
      #endif
      continue;
    }
    if (change->block == B_torch) continue;
    if (change->x < cx || change->x >= cx + 16 || change->z < cz || change->z >= cz + 16 ||
        change->y < cy || change->y >= cy + 16) continue;
    unsigned index = (unsigned)(change->x - cx) + (unsigned)(change->z - cz) * 16u +
      (unsigned)(change->y - cy) * 256u;
    chunk_section[index ^ 7u] = change->block;
  }
  return W_plains;
}
