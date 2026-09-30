#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "beta173_rng.h"
#include "beta173_noise.h"
#include "beta173_worldgen.h"
#include "registries.h"

static void vectors (void) {
  FILE *file = fopen("tests/betanium-vectors.txt", "r");
  assert(file);
  char line[256], seed_text[32], kind, previous_kind = 0;
  uint64_t previous_seed = 0, seed;
  Beta173Rng rng;
  Beta173Noise noise;
  double grid[BETA173_GRID_SIZE];
  unsigned count = 0;
  while (fgets(line, sizeof(line), file)) {
    assert(sscanf(line, "%c %31s", &kind, seed_text) == 2);
    assert(beta173_seed_parse(seed_text, &seed));
    if (kind != previous_kind || seed != previous_seed) {
      beta173_rng_seed(&rng, seed);
      if (kind == 'N') beta173_noise_init(&noise, &rng);
      if (kind == 'D' || kind == 'H') beta173_worldgen_init(seed);
      previous_kind = kind; previous_seed = seed;
    }
    double actual, expected;
    if (kind == 'R' || kind == 'B') {
      int arg;
      assert(sscanf(line, "%*c %*s %d %lf", &arg, &expected) == 2);
      uint32_t value;
      if (kind == 'R') {
        value = beta173_rng_next(&rng, 32);
        actual = value <= INT32_MAX ? (double)value : (double)value - 4294967296.0;
      } else {
        assert(beta173_rng_bound(&rng, (uint32_t)arg, &value));
        actual = value;
      }
    } else if (kind == 'N') {
      double x, y, z;
      assert(sscanf(line, "%*c %*s %lf %lf %lf %lf", &x, &y, &z, &expected) == 4);
      actual = beta173_perlin(&noise, x, y, z);
    } else if (kind == 'D') {
      int x, z, index;
      assert(sscanf(line, "%*c %*s %d %d %d %lf", &x, &z, &index, &expected) == 4);
      assert(index >= 0 && index < BETA173_GRID_SIZE);
      assert(beta173_density_grid(x, z, grid));
      actual = grid[index];
    } else {
      int x, z;
      assert(kind == 'H');
      assert(sscanf(line, "%*c %*s %d %d %lf", &x, &z, &expected) == 3);
      actual = beta173_height(x, z);
    }
    assert(fabs(actual - expected) <= 1e-10 + fabs(expected) * 1e-12);
    count ++;
  }
  assert(fclose(file) == 0 && count == 384);
}

int main (void) {
  uint64_t seed = 0;
  assert(beta173_seed_parse("-9223372036854775808", &seed) && seed == (UINT64_C(1) << 63));
  assert(beta173_seed_parse("9223372036854775807", &seed) && seed == INT64_MAX);
  assert(beta173_seed_parse("-1", &seed) && seed == UINT64_MAX);
  const char *invalid[] = {"", "-", "1x", " 1", "9223372036854775808", "-9223372036854775809", "00000000000000000000"};
  for (size_t i = 0; i < sizeof(invalid)/sizeof(invalid[0]); i ++) assert(!beta173_seed_parse(invalid[i], &seed));
  assert(!beta173_seed_parse(NULL, &seed));
  Beta173Rng rng;
  beta173_rng_seed(&rng, 0);
  assert(fabs(beta173_rng_double(&rng) - 0.730967787376657) < 1e-15);
  beta173_rng_seed(&rng, 0);
  assert(beta173_rng_long(&rng) == UINT64_C(13483975608033169720));
  uint32_t value;
  assert(!beta173_rng_bound(&rng, 0, &value));
  assert(!beta173_rng_bound(&rng, UINT32_MAX, &value));
  uint64_t state = rng.state;
  assert(beta173_rng_next(&rng, 33) == 0 && rng.state == state);
  vectors();

  static double a[BETA173_GRID_SIZE], b[BETA173_GRID_SIZE], repeat[BETA173_GRID_SIZE];
  beta173_worldgen_init(UINT64_MAX);
  assert(beta173_density_grid(-1, -1, a));
  assert(beta173_density_grid(0, -1, b));
  for (unsigned z = 0; z < 5; z ++) for (unsigned y = 0; y < 17; y ++) {
    assert(a[(4*5+z)*17+y] == b[z*17+y]);
  }
  assert(beta173_density_grid(-1, 0, b));
  for (unsigned x = 0; x < 5; x ++) for (unsigned y = 0; y < 17; y ++) {
    assert(a[(x*5+4)*17+y] == b[(x*5)*17+y]);
  }
  assert(beta173_density_grid(-1, -1, repeat));
  assert(memcmp(a, repeat, sizeof(a)) == 0);
  beta173_worldgen_init(UINT64_MAX);
  assert(beta173_density_grid(-1, -1, repeat));
  assert(memcmp(a, repeat, sizeof(a)) == 0);
  beta173_worldgen_init(1);
  assert(beta173_density_grid(-1, -1, repeat));
  assert(memcmp(a, repeat, sizeof(a)) != 0);
  assert(!beta173_density_grid(INT_MAX, INT_MIN, repeat));
  assert(!beta173_density_grid(0, 0, NULL));
  assert(beta173_terrain(INT_MIN, 60, 0) == B_air);
  assert(beta173_terrain(0, INT_MIN, 0) == B_bedrock);
  assert(beta173_terrain(0, INT_MAX, 0) == B_air);
  assert(beta173_terrain(0, 128, 0) == B_air);
  puts("worldgen: 384 external reference vectors, determinism and borders passed");
  return 0;
}
