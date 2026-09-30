/* betanium MathHelper/Java arithmetic behavior. GPL-3.0-or-later.
 * Copyright (C) 2026 Aksu Lightning. */
#include <math.h>
#include "beta173_math.h"

static float table_value (float index) {
  if (!isfinite(index) || index < -2147483648.0f || index >= 2147483648.0f) return 0;
  uint32_t wrapped = (uint32_t)(int32_t)index & 65535u;
  /* Evaluate the quantized sine table entry on demand, saving 256 KiB. */
  return (float)sin((double)wrapped * (6.283185307179586476925286766559 / 65536.0));
}
float beta173_sin (float angle) { return table_value(angle * 10430.378f); }
float beta173_cos (float angle) { return table_value(angle * 10430.378f + 16384.0f); }
uint32_t beta173_draw (Beta173Rng *rng, uint32_t bound) {
  uint32_t result = 0; beta173_rng_bound(rng, bound, &result); return result;
}
uint64_t beta173_odd (uint64_t value) {
  /* Java (signedLong / 2) * 2 + 1 truncates toward zero, including negatives. */
  if (!(value & 1u)) return value + 1u;
  return value & (UINT64_C(1) << 63) ? value + 2u : value;
}
uint64_t beta173_coordinate_seed (uint64_t seed, int x, int z, uint64_t odd_x, uint64_t odd_z) {
  return ((uint64_t)(int64_t)x * odd_x + (uint64_t)(int64_t)z * odd_z) ^ seed;
}
