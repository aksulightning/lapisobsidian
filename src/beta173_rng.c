/* C port of betanium's Java random behavior.
 * Copyright (C) 2026 Aksu Lightning; GPL-3.0-or-later. */
#include "beta173_rng.h"

#define JAVA_MASK UINT64_C(0xffffffffffff)
#define JAVA_MULT UINT64_C(25214903917)

void beta173_rng_seed (Beta173Rng *rng, uint64_t seed) {
  if (rng) rng->state = (seed ^ JAVA_MULT) & JAVA_MASK;
}

uint32_t beta173_rng_next (Beta173Rng *rng, unsigned bits) {
  if (!rng || bits == 0 || bits > 32) return 0;
  /* Unsigned multiplication deliberately wraps, as Java's long arithmetic. */
  rng->state = (rng->state * JAVA_MULT + 11u) & JAVA_MASK;
  return (uint32_t)(rng->state >> (48 - bits));
}

bool beta173_rng_bound (Beta173Rng *rng, uint32_t bound, uint32_t *value) {
  if (!rng || !value || bound == 0 || bound > INT32_MAX) return false;
  if ((bound & (bound - 1)) == 0) {
    *value = (uint32_t)(((uint64_t)bound * beta173_rng_next(rng, 31)) >> 31);
    return true;
  }
  uint32_t bits, result;
  do {
    bits = beta173_rng_next(rng, 31);
    result = bits % bound;
    /* Java rejects when this expression overflows a signed int. */
  } while ((uint64_t)bits - result + bound - 1 > INT32_MAX);
  *value = result;
  return true;
}

uint64_t beta173_rng_long (Beta173Rng *rng) {
  uint32_t hi = beta173_rng_next(rng, 32);
  uint32_t lo = beta173_rng_next(rng, 32);
  uint64_t low = lo;
  if (lo & UINT32_C(0x80000000)) low |= UINT64_C(0xffffffff00000000);
  return ((uint64_t)hi << 32) + low;
}

double beta173_rng_double (Beta173Rng *rng) {
  uint32_t hi = beta173_rng_next(rng, 26);
  uint32_t lo = beta173_rng_next(rng, 27);
  return ((double)hi * 134217728.0 + lo) / 9007199254740992.0;
}

bool beta173_seed_parse (const char *text, uint64_t *seed) {
  if (!text || !seed) return false;
  bool negative = *text == '-';
  if (negative || *text == '+') text ++;
  if (!*text) return false;
  uint64_t limit = negative ? UINT64_C(9223372036854775808) : INT64_MAX;
  uint64_t value = 0;
  for (size_t i = 0; text[i]; i ++) {
    if (i >= 19 || text[i] < '0' || text[i] > '9') return false;
    uint64_t digit = (uint64_t)(text[i] - '0');
    if (value > (limit - digit) / 10) return false;
    value = value * 10 + digit;
  }
  *seed = negative ? UINT64_C(0) - value : value;
  return true;
}
