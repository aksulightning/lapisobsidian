/* C port of betanium terrain density and climate formulas.
 * Copyright (C) 2026 Aksu Lightning; GPL-3.0-or-later.
 * Climate is sampled on a world-aligned density lattice to share boundary nodes. */
#include <math.h>
#include <string.h>
#include "beta173_worldgen.h"
#include "beta173_noise.h"
#include "registries.h"

static uint64_t generator_seed;
static Beta173Noise sand_gravel[4], stone_patch[4];
static Beta173Noise lower[16], upper[16], selector[8], depth[10], scale[16];
static Beta173Noise temperature_noise[4], humidity_noise[4], rain_noise[2];
static double cached_grid[BETA173_GRID_SIZE];
static int cached_x, cached_z;
static bool cache_valid, initialized;

static void init_octaves (Beta173Noise *noise, size_t count, Beta173Rng *rng) {
  for (size_t i = 0; i < count; i ++) beta173_noise_init(&noise[i], rng);
}

void beta173_worldgen_init (uint64_t seed) {
  Beta173Rng rng;
  beta173_rng_seed(&rng, seed);
  init_octaves(lower, 16, &rng);
  init_octaves(upper, 16, &rng);
  init_octaves(selector, 8, &rng);
  init_octaves(sand_gravel, 4, &rng);
  init_octaves(stone_patch, 4, &rng);
  init_octaves(depth, 10, &rng);
  init_octaves(scale, 16, &rng);
  beta173_rng_seed(&rng, seed * UINT64_C(9871));
  init_octaves(temperature_noise, 4, &rng);
  beta173_rng_seed(&rng, seed * UINT64_C(39811));
  init_octaves(humidity_noise, 4, &rng);
  beta173_rng_seed(&rng, seed * UINT64_C(543321));
  init_octaves(rain_noise, 2, &rng);
  cache_valid = false;
  initialized = true;
  generator_seed = seed;
}

bool beta173_coords_valid (int x, int z) {
  return x >= BETA173_MIN_COORD && x <= BETA173_MAX_COORD &&
    z >= BETA173_MIN_COORD && z <= BETA173_MAX_COORD;
}

static double clamp01 (double value) { return value < 0 ? 0 : (value > 1 ? 1 : value); }

void beta173_climate (int x, int z, double *temperature, double *humidity) {
  if (!temperature || !humidity) return;
  if (!initialized) beta173_worldgen_init(0);
  double t = beta173_climate_noise(temperature_noise, 4, x, z, 0.02500000037252903, 0.25);
  double h = beta173_climate_noise(humidity_noise, 4, x, z, 0.05000000074505806, 1.0 / 3.0);
  double rain = beta173_climate_noise(rain_noise, 2, x, z, 0.25, 0.5882352941176471);
  double rv = rain * 1.1 + 0.5;
  t = (t * 0.15 + 0.7) * 0.99 + rv * 0.01;
  *temperature = clamp01(1.0 - (1.0 - t) * (1.0 - t));
  *humidity = clamp01((h * 0.15 + 0.5) * 0.998 + rv * 0.002);
}

bool beta173_density_grid (int cx, int cz, double out[BETA173_GRID_SIZE]) {
  if (!out || cx < -2048 || cx > 2047 || cz < -2048 || cz > 2047) return false;
  if (!initialized) beta173_worldgen_init(0);
  for (unsigned ix = 0; ix < 5; ix ++) {
    for (unsigned iz = 0; iz < 5; iz ++) {
      int x = cx * 16 + (int)ix * 4, z = cz * 16 + (int)iz * 4;
      double gx = (double)x / 4.0, gz = (double)z / 4.0;
      double lo[17], up[17], sel[17], t, h;
      beta173_climate(x, z, &t, &h);
      double erosion = 1.0 - h * t;
      erosion *= erosion; erosion *= erosion; erosion = 1.0 - erosion;
      double stretch = (beta173_noise_2d(depth, 10, gx, gz, 1.121) + 256.0) / 512.0;
      stretch *= erosion;
      if (stretch > 1.0) stretch = 1.0;
      double pivot = beta173_noise_2d(scale, 16, gx, gz, 200.0) / 8000.0;
      if (pivot < 0) pivot = -pivot * 0.3;
      pivot = pivot * 3.0 - 2.0;
      if (pivot < 0) {
        pivot /= 2.0;
        if (pivot < -1.0) pivot = -1.0;
        pivot /= 1.4; pivot /= 2.0;
        stretch = 0;
      } else {
        if (pivot > 1.0) pivot = 1.0;
        pivot /= 8.0;
      }
      if (stretch < 0) stretch = 0;
      stretch += 0.5;
      pivot = 17.0 / 2.0 + (pivot * 17.0 / 16.0) * 4.0;
      beta173_noise_column(lower, 16, gx, gz, 684.412, 684.412, lo);
      beta173_noise_column(upper, 16, gx, gz, 684.412, 684.412, up);
      beta173_noise_column(selector, 8, gx, gz, 684.412 / 80.0, 684.412 / 160.0, sel);
      for (unsigned y = 0; y < 17; y ++) {
        double weight = (sel[y] / 10.0 + 1.0) / 2.0;
        double a = lo[y] / 512.0, b = up[y] / 512.0;
        double density = weight < 0 ? a : (weight > 1 ? b : a + (b - a) * weight);
        double vertical = ((double)y - pivot) * 12.0 / stretch;
        if (vertical < 0) vertical *= 4.0;
        density -= vertical;
        if (y > 13) {
          /* Java's float intermediate is observable in the upper density fade. */
          double fade = (double)(float)((double)(y - 13) / 3.0);
          density = density * (1.0 - fade) + -10.0 * fade;
        }
        out[(ix * 5 + iz) * 17 + y] = density;
      }
    }
  }
  return true;
}

static int chunk_coord (int value) {
  int q = value / 16;
  return value % 16 < 0 ? q - 1 : q;
}
static void ensure_grid (int x, int z) {
  int cx = chunk_coord(x), cz = chunk_coord(z);
  if (!cache_valid || cx != cached_x || cz != cached_z) {
    cache_valid = beta173_density_grid(cx, cz, cached_grid);
    cached_x = cx; cached_z = cz;
  }
}
static double lerp (double t, double a, double b) { return a + t * (b - a); }

double beta173_density (int x, int y, int z) {
  if (!beta173_coords_valid(x, z) || y < 0 || y >= 128) return -10;
  ensure_grid(x, z);
  unsigned rx = (unsigned)(x - cached_x * 16), rz = (unsigned)(z - cached_z * 16);
  unsigned ix = rx / 4, iz = rz / 4, iy = (unsigned)y / 8;
  unsigned a = (ix * 5 + iz) * 17 + iy, b = a + 5 * 17;
  double fx = (double)(rx % 4) / 4.0, fz = (double)(rz % 4) / 4.0;
  double low = lerp(fz, lerp(fx, cached_grid[a], cached_grid[b]),
    lerp(fx, cached_grid[a + 17], cached_grid[b + 17]));
  double high = lerp(fz, lerp(fx, cached_grid[a + 1], cached_grid[b + 1]),
    lerp(fx, cached_grid[a + 18], cached_grid[b + 18]));
  return lerp((double)(y % 8) / 8.0, low, high);
}

uint8_t beta173_terrain (int x, int y, int z) {
  if (!beta173_coords_valid(x, z)) return B_air;
  if (y < 0) return B_bedrock;
  if (y >= 128) return B_air;
  if (beta173_density(x, y, z) > 0) return B_stone;
  if (y >= 64) return B_air;
  if (y == 63) {
    double t, h;
    beta173_climate(x, z, &t, &h);
    if (t < 0.5) return B_ice;
  }
  return B_water;
}

uint8_t beta173_height (int x, int z) {
  if (!beta173_coords_valid(x, z)) return 0;
  for (int y = 127; y >= 0; y --) {
    if (beta173_density(x, y, z) > 0) return (uint8_t)y;
  }
  return 0;
}

uint64_t beta173_worldgen_seed (void) {
  if (!initialized) beta173_worldgen_init(0);
  return generator_seed;
}
void beta173_surface_fields (int x, int z, double sand[16], double stone[16]) {
  if (!initialized) beta173_worldgen_init(0);
  beta173_noise_surface(sand_gravel, x, z, 1.0/32, sand);
  beta173_noise_surface(stone_patch, x, z, 1.0/16, stone);
}
double beta173_gravel_noise (int x, int z) {
  if (!initialized) beta173_worldgen_init(0);
  return beta173_noise_2d(sand_gravel, 4, x, z, 1.0/32);
}
