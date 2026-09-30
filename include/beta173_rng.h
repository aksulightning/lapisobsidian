#ifndef H_BETA173_RNG
#define H_BETA173_RNG
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct { uint64_t state; } Beta173Rng;
void beta173_rng_seed (Beta173Rng *rng, uint64_t seed);
uint32_t beta173_rng_next (Beta173Rng *rng, unsigned bits);
bool beta173_rng_bound (Beta173Rng *rng, uint32_t bound, uint32_t *value);
uint64_t beta173_rng_long (Beta173Rng *rng);
double beta173_rng_double (Beta173Rng *rng);
bool beta173_seed_parse (const char *text, uint64_t *seed);
#endif
