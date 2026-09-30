#ifndef H_BETA173_MATH
#define H_BETA173_MATH
#include "beta173_rng.h"
#define BETA173_PI 3.1415927410125732421875f
float beta173_sin (float angle);
float beta173_cos (float angle);
/* Bounded, internal feature draws; callers supply a positive constant bound. */
uint32_t beta173_draw (Beta173Rng *rng, uint32_t bound);
uint64_t beta173_odd (uint64_t value);
uint64_t beta173_coordinate_seed (uint64_t seed, int x, int z, uint64_t odd_x, uint64_t odd_z);
#endif
