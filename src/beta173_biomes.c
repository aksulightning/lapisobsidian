/* Port of betanium biome_defs, Copyright (C) 2026 Aksu Lightning.
 * GPL-3.0-or-later. Evaluate the quantized lookup without a 4096-entry table. */
#include <math.h>
#include "beta173_biomes.h"
#include "registries.h"

Beta173Biome beta173_biome (double t, double h) {
  if (!isfinite(t) || !isfinite(h)) return BETA_PLAINS;
  t = t < 0 ? 0 : (t > 1 ? 1 : t);
  h = h < 0 ? 0 : (h > 1 ? 1 : h);
  t = floor(t * 63) / 63;
  h = floor(h * 63) / 63 * t;
  if (t < 0.1) return BETA_TUNDRA;
  if (h < 0.2) {
    if (t < 0.5) return BETA_TUNDRA;
    return t < 0.95 ? BETA_SAVANNA : BETA_DESERT;
  }
  if (h > 0.5 && t < 0.7) return BETA_SWAMPLAND;
  if (t < 0.5) return BETA_TAIGA;
  if (t < 0.97) return h < 0.35 ? BETA_SHRUBLAND : BETA_FOREST;
  if (h < 0.45) return BETA_PLAINS;
  return h < 0.9 ? BETA_SEASONAL_FOREST : BETA_RAINFOREST;
}

uint8_t beta173_biome_protocol (Beta173Biome biome) {
  static const uint8_t mapping[BETA_BIOME_COUNT] = {
    W_jungle, W_swamp, W_forest, W_forest, W_savanna, W_plains,
    W_taiga, W_desert, W_plains, W_snowy_plains
  };
  return (unsigned)biome < BETA_BIOME_COUNT ? mapping[biome] : W_plains;
}
