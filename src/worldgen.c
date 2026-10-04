#include <string.h>
#include "plates.h"
#include "globals.h"
#include "tools.h"
#include "registries.h"
#include "procedures.h"
#include "worldgen.h"
#include "plate_contexts.h"
#include "farlands.h"
#include "beta173_worldgen.h"
#include "beta173_features.h"


#define TERRAIN_CACHE_CHUNKS 4

typedef struct {
  uint64_t active_seed;
  uint8_t initialized, active_mirror;
  Beta173Chunk terrain_chunks[TERRAIN_CACHE_CHUNKS];
  Beta173Chunk *terrain_chunk;
  unsigned cache_count, cache_order[TERRAIN_CACHE_CHUNKS];
} WorldgenContext;
static WorldgenContext legacy_context, *ctx = &legacy_context;
size_t worldgen_context_size (void) { return sizeof(*ctx); }
void worldgen_context_select (void *memory) { ctx = memory ? memory : &legacy_context; }

uint8_t chunk_section[4096];

static void ensure_generator (void) {
  if (!ctx->initialized || ctx->active_seed != world_seed || ctx->active_mirror != world_mirror_horizontal) {
    beta173_worldgen_init(world_seed);
    ctx->active_seed = world_seed;
    ctx->active_mirror = world_mirror_horizontal;
    ctx->initialized = 1;
    ctx->cache_count = 0;
    for (unsigned i = 0; i < TERRAIN_CACHE_CHUNKS; i ++) ctx->cache_order[i] = i;
  }
}

static int chunk_coord (int value) { return value / 16 - (value % 16 < 0); }
static void mirror_chunk (Beta173Chunk *chunk, int cx) {
  /* Reverse complete X columns after features, so border-spanning caves and
   * canopies are reflected together. No extra chunk buffer is needed. */
  for (unsigned x = 0; x < 8; x ++) for (unsigned z = 0; z < 16; z ++) {
    unsigned a = x*16+z, b = (15-x)*16+z;
    uint8_t biome = chunk->biomes[a];
    chunk->biomes[a] = chunk->biomes[b]; chunk->biomes[b] = biome;
    for (unsigned y = 0; y < 128; y ++) {
      uint8_t block = chunk->blocks[a*128+y];
      chunk->blocks[a*128+y] = chunk->blocks[b*128+y];
      chunk->blocks[b*128+y] = block;
    }
  }
  chunk->cx = cx;
}
static bool ensure_chunk (int cx, int cz) {
  ensure_generator();
  if (cx < -2048 || cx > 2047 || cz < -2048 || cz > 2047) return false;
  unsigned index = 0;
  for (; index < ctx->cache_count; index ++) {
    const Beta173Chunk *entry = &ctx->terrain_chunks[ctx->cache_order[index]];
    if (entry->cx == cx && entry->cz == cz) break;
  }
  if (index == ctx->cache_count) {
    if (ctx->cache_count < TERRAIN_CACHE_CHUNKS) ctx->cache_count ++;
    else index = TERRAIN_CACHE_CHUNKS-1;
    Beta173Chunk *entry = &ctx->terrain_chunks[ctx->cache_order[index]];
    int source_x = world_mirror_horizontal ? -cx-1 : cx;
    if (!beta173_generate_chunk(world_seed,source_x,cz,BETA_DECORATION,entry)) return false;
    if (world_mirror_horizontal) mirror_chunk(entry,cx);
    farlands_apply(world_seed,world_mirror_horizontal != 0,entry);
  }
  unsigned slot = ctx->cache_order[index];
  for (; index > 0; index --) ctx->cache_order[index] = ctx->cache_order[index-1];
  ctx->cache_order[0] = slot;
  ctx->terrain_chunk = &ctx->terrain_chunks[slot];
  return true;
}
static uint8_t terrain_at (int x, int y, int z) {
  if (!beta173_coords_valid(x,z)) return B_air;
  if (plates_type() != PLATE_BETANIUM) return plates_terrain(x,y,z);
  if (y < 0) return B_bedrock;
  if (y >= 128 || !ensure_chunk(chunk_coord(x),chunk_coord(z))) return B_air;
  uint8_t *block = beta173_chunk_block(ctx->terrain_chunk,x,y,z);
  return block ? *block : B_air;
}

uint32_t getChunkHash (short x, short z) {
  uint64_t coordinates = (uint16_t)x | ((uint64_t)(uint16_t)z << 16);
  return (uint32_t)splitmix64(coordinates ^ world_seed);
}

uint8_t getChunkBiome (short x, short z) {
  if (plates_type() != PLATE_BETANIUM) return W_plains;
  if (!ensure_chunk(x,z)) return W_plains;
  return beta173_biome_protocol((Beta173Biome)ctx->terrain_chunk->biomes[8*16+8]);
}

uint8_t getHeightAt (int x, int z) {
  if (plates_type() != PLATE_BETANIUM && x == 8 && z == 8) return 64;
  for (int y = 127; y >= 0; y --) {
    uint8_t block = terrain_at(x,y,z);
    if (block != B_air && block != B_water && block != B_lava) return (uint8_t)y;
  }
  return 0;
}

uint8_t getTerrainAt (int x, int y, int z, ChunkAnchor anchor) {
  (void)anchor;
  return terrain_at(x, y, z);
}

uint8_t getBlockAt (int x, int y, int z) {
  if (!beta173_coords_valid(x, z)) return B_air;
  if (y < 0) return B_bedrock;
  if (y <= 255) {
    uint8_t change = getBlockChange(x, y, z);
    if (change != 0xff) return change;
  }
  return terrain_at(x, y, z);
}

uint8_t buildChunkSection (int cx, int cy, int cz) {
  memset(chunk_section, B_air, sizeof(chunk_section));
  if (cx < BETA173_MIN_COORD || cx > BETA173_MAX_COORD - 15 ||
      cz < BETA173_MIN_COORD || cz > BETA173_MAX_COORD - 15 || cy < -64 || cy > 304 ||
      cx % 16 != 0 || cy % 16 != 0 || cz % 16 != 0) return W_plains;
  if (plates_type() == PLATE_BETANIUM) ensure_generator();
  for (unsigned i = 0; i < 4096; i ++) {
    int x = cx + (int)(i % 16), z = cz + (int)((i / 16) % 16);
    int y = cy + (int)(i / 256);
    /* The network palette packs eight 8-bit IDs per big-endian long. */
    chunk_section[i ^ 7u] = terrain_at(x, y, z);
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
  return getChunkBiome((short)(cx/16), (short)(cz/16));
}
