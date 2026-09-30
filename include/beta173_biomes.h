#ifndef H_BETA173_BIOMES
#define H_BETA173_BIOMES
#include <stdbool.h>
#include <stdint.h>

typedef enum {
  BETA_RAINFOREST, BETA_SWAMPLAND, BETA_SEASONAL_FOREST, BETA_FOREST,
  BETA_SAVANNA, BETA_SHRUBLAND, BETA_TAIGA, BETA_DESERT, BETA_PLAINS,
  BETA_TUNDRA, BETA_BIOME_COUNT
} Beta173Biome;
Beta173Biome beta173_biome (double temperature, double humidity);
uint8_t beta173_biome_protocol (Beta173Biome biome);
#endif
