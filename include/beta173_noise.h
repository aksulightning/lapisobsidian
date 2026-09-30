#ifndef H_BETA173_NOISE
#define H_BETA173_NOISE
#include "beta173_rng.h"

typedef struct {
  uint8_t perm[256];
  double ox, oy, oz;
} Beta173Noise;

void beta173_noise_init (Beta173Noise *noise, Beta173Rng *rng);
double beta173_perlin (const Beta173Noise *noise, double x, double y, double z);
/* Full 17-node vertical column; preserves Beta's cached-gradient quirk. */
void beta173_noise_column (const Beta173Noise *noise, size_t count,
  double x, double z, double horizontal, double vertical, double out[17]);
double beta173_noise_2d (const Beta173Noise *noise, size_t count,
  double x, double z, double frequency);
double beta173_climate_noise (const Beta173Noise *noise, size_t count,
  double x, double z, double frequency, double lacunarity);
#endif
