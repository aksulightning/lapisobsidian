#ifndef H_BETA173_WORLDGEN
#define H_BETA173_WORLDGEN
#include <stdbool.h>
#include <stdint.h>

#define BETA173_GENERATOR_VERSION 1
#define BETA173_GRID_SIZE 425
/* Existing player and edit storage uses signed 16-bit X/Z. */
#define BETA173_MIN_COORD (-32768)
#define BETA173_MAX_COORD 32767

void beta173_worldgen_init (uint64_t seed);
bool beta173_coords_valid (int x, int z);
bool beta173_density_grid (int chunk_x, int chunk_z, double out[BETA173_GRID_SIZE]);
double beta173_density (int x, int y, int z);
uint8_t beta173_terrain (int x, int y, int z);
uint8_t beta173_height (int x, int z);
void beta173_climate (int x, int z, double *temperature, double *humidity);
#endif
