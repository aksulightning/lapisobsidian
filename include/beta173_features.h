#ifndef H_BETA173_FEATURES
#define H_BETA173_FEATURES
#include "beta173_biomes.h"
#include "beta173_rng.h"

#define BETA173_CHUNK_BLOCKS 32768
/* Phase boundaries are public to allow independent deterministic feature tests. */
typedef enum { BETA_BASE, BETA_SURFACE, BETA_CAVES, BETA_ORES, BETA_TREES, BETA_DECORATION } Beta173Phase;
typedef struct {
  int cx, cz;
  uint8_t blocks[BETA173_CHUNK_BLOCKS];
  uint8_t biomes[256];
} Beta173Chunk;

bool beta173_generate_chunk (uint64_t seed, int cx, int cz, Beta173Phase phase, Beta173Chunk *chunk);
uint8_t *beta173_chunk_block (Beta173Chunk *chunk, int x, int y, int z);
void beta173_surface (Beta173Chunk *chunk);
void beta173_caves (uint64_t seed, Beta173Chunk *chunk);
void beta173_ores (uint64_t seed, Beta173Chunk *chunk);
void beta173_vegetation (uint64_t seed, Beta173Chunk *chunk, bool decorate);
#endif
