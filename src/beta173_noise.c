/* C port of betanium beta_noise; Copyright (C) 2026 Aksu Lightning.
 * GPL-3.0-or-later. Permutations are stored once; lookups wrap at 256. */
#include <math.h>
#include <float.h>
#include "beta173_noise.h"

_Static_assert(FLT_RADIX == 2 && FLT_MANT_DIG == 24 && DBL_MANT_DIG == 53,
  "Beta terrain requires IEEE-754 float and double precision");

static unsigned perm (const Beta173Noise *n, unsigned i) { return n->perm[i & 255u]; }
static double lerp (double t, double a, double b) { return a + t * (b - a); }
static double smooth (double t) { return t * t * t * (t * (t * 6.0 - 15.0) + 10.0); }
static unsigned cell (double v) {
  /* fmod avoids out-of-range double-to-integer conversions at large coordinates. */
  double r = fmod(v, 256.0);
  if (r < 0) r += 256.0;
  return (unsigned)r;
}
static bool valid_point (double x, double y, double z) {
  return isfinite(x) && isfinite(y) && isfinite(z) &&
    fabs(x) <= 1e12 && fabs(y) <= 1e12 && fabs(z) <= 1e12;
}
static double grad (unsigned hash, double x, double y, double z) {
  unsigned h = hash & 15u;
  double u = h < 8 ? x : y;
  double v = h < 4 ? y : (h == 12 || h == 14 ? x : z);
  return ((h & 1u) ? -u : u) + ((h & 2u) ? -v : v);
}

void beta173_noise_init (Beta173Noise *n, Beta173Rng *rng) {
  if (!n || !rng) return;
  n->ox = beta173_rng_double(rng) * 256.0;
  n->oy = beta173_rng_double(rng) * 256.0;
  n->oz = beta173_rng_double(rng) * 256.0;
  for (unsigned i = 0; i < 256; i ++) n->perm[i] = (uint8_t)i;
  for (unsigned i = 0; i < 256; i ++) {
    uint32_t offset = 0;
    beta173_rng_bound(rng, 256 - i, &offset);
    unsigned j = i + offset;
    uint8_t tmp = n->perm[i]; n->perm[i] = n->perm[j]; n->perm[j] = tmp;
  }
}

static void gradients (const Beta173Noise *n, unsigned ix, unsigned iy, unsigned iz,
  double x, double y, double z, double sx, double c[4]) {
  unsigned a = perm(n, ix) + iy, b = perm(n, ix + 1) + iy;
  unsigned aa = perm(n, a) + iz, ab = perm(n, a + 1) + iz;
  unsigned ba = perm(n, b) + iz, bb = perm(n, b + 1) + iz;
  c[0] = lerp(sx, grad(perm(n, aa), x, y, z), grad(perm(n, ba), x - 1, y, z));
  c[1] = lerp(sx, grad(perm(n, ab), x, y - 1, z), grad(perm(n, bb), x - 1, y - 1, z));
  c[2] = lerp(sx, grad(perm(n, aa + 1), x, y, z - 1), grad(perm(n, ba + 1), x - 1, y, z - 1));
  c[3] = lerp(sx, grad(perm(n, ab + 1), x, y - 1, z - 1), grad(perm(n, bb + 1), x - 1, y - 1, z - 1));
}

double beta173_perlin (const Beta173Noise *n, double x, double y, double z) {
  if (!n || !valid_point(x, y, z)) return 0;
  x += n->ox; y += n->oy; z += n->oz;
  double fx = floor(x), fy = floor(y), fz = floor(z), c[4];
  x -= fx; y -= fy; z -= fz;
  gradients(n, cell(fx), cell(fy), cell(fz), x, y, z, smooth(x), c);
  return lerp(smooth(z), lerp(smooth(y), c[0], c[1]), lerp(smooth(y), c[2], c[3]));
}

void beta173_noise_column (const Beta173Noise *noise, size_t count,
  double x, double z, double horizontal, double vertical, double out[17]) {
  if (!out) return;
  for (unsigned y = 0; y < 17; y ++) out[y] = 0;
  if (!noise || count > 16 || !valid_point(x * horizontal, vertical * 16, z * horizontal)) return;
  double frequency = 1.0;
  for (size_t octave = 0; octave < count; octave ++) {
    const Beta173Noise *n = &noise[octave];
    double wx = x * (horizontal * frequency) + n->ox;
    double wz = z * (horizontal * frequency) + n->oz;
    unsigned ix = cell(floor(wx)), iz = cell(floor(wz));
    wx -= floor(wx); wz -= floor(wz);
    double sx = smooth(wx), sz = smooth(wz), c[4] = {0};
    unsigned last_y = 256;
    for (unsigned y = 0; y < 17; y ++) {
      double wy = (double)y * (vertical * frequency) + n->oy;
      unsigned iy = cell(floor(wy)); wy -= floor(wy);
      /* Intentional historical behavior: reuse gradients within the same Y cell. */
      if (iy != last_y) {
        gradients(n, ix, iy, iz, wx, wy, wz, sx, c);
        last_y = iy;
      }
      out[y] += lerp(sz, lerp(smooth(wy), c[0], c[1]), lerp(smooth(wy), c[2], c[3])) / frequency;
    }
    frequency /= 2.0;
  }
}

double beta173_noise_2d (const Beta173Noise *noise, size_t count,
  double x, double z, double frequency) {
  if (!noise || count > 16 || !valid_point(x * frequency, 0, z * frequency)) return 0;
  double result = 0, scale = 1;
  for (size_t i = 0; i < count; i ++) {
    const Beta173Noise *n = &noise[i];
    double wx = x * (frequency * scale) + n->ox;
    double wz = z * (frequency * scale) + n->oz;
    unsigned ix = cell(floor(wx)), iz = cell(floor(wz));
    wx -= floor(wx); wz -= floor(wz);
    unsigned aa = perm(n, perm(n, ix)) + iz;
    unsigned ba = perm(n, perm(n, ix + 1)) + iz;
    /* grad(hash,x,0,z) is algebraically the reference's special grad_2d. */
    double low = lerp(smooth(wx), grad(perm(n, aa), wx, 0, wz), grad(perm(n, ba), wx - 1, 0, wz));
    double high = lerp(smooth(wx), grad(perm(n, aa + 1), wx, 0, wz - 1), grad(perm(n, ba + 1), wx - 1, 0, wz - 1));
    result += lerp(smooth(wz), low, high) / scale;
    scale /= 2;
  }
  return result;
}

static double simplex_corner (unsigned h, double x, double y) {
  static const int8_t grad2[12][2] = {
    {1,1}, {-1,1}, {1,-1}, {-1,-1}, {1,0}, {-1,0},
    {1,0}, {-1,0}, {0,1}, {0,-1}, {0,1}, {0,-1}
  };
  double t = 0.5 - x*x - y*y;
  if (t < 0) return 0;
  t *= t;
  return t*t * (grad2[h % 12][0]*x + grad2[h % 12][1]*y);
}
static double simplex (const Beta173Noise *n, double x, double z) {
  const double f2 = 0.5 * (1.7320508075688772 - 1.0);
  const double g2 = (3.0 - 1.7320508075688772) / 6.0;
  x += n->ox; z += n->oy;
  double skew = (x + z) * f2;
  /* Beta simplex fast-floor intentionally subtracts one at nonpositive integers. */
  double i = x + skew > 0 ? floor(x + skew) : ceil(x + skew) - 1;
  double j = z + skew > 0 ? floor(z + skew) : ceil(z + skew) - 1;
  double t = (i + j) * g2;
  double dx = x - (i - t), dz = z - (j - t);
  unsigned i1 = dx > dz ? 1u : 0u, j1 = 1u - i1;
  unsigned ii = cell(i), jj = cell(j);
  double a = simplex_corner(perm(n, ii + perm(n, jj)), dx, dz);
  double b = simplex_corner(perm(n, ii + i1 + perm(n, jj + j1)), dx - i1 + g2, dz - j1 + g2);
  double c = simplex_corner(perm(n, ii + 1 + perm(n, jj + 1)), dx - 1 + 2*g2, dz - 1 + 2*g2);
  return 70.0 * (a + b + c);
}

double beta173_climate_noise (const Beta173Noise *noise, size_t count,
  double x, double z, double frequency, double lacunarity) {
  if (!noise || count > 16 || !valid_point(x * frequency, 0, z * frequency) ||
      !isfinite(lacunarity) || lacunarity < 0 || lacunarity > 1) return 0;
  frequency /= 1.5;
  double sum = 0, amplitude_divisor = 1, scale = 1;
  for (size_t i = 0; i < count; i ++) {
    sum += simplex(&noise[i], x * (frequency * scale), z * (frequency * scale)) * (0.55 / amplitude_divisor);
    scale *= lacunarity;
    amplitude_divisor *= 0.5;
  }
  return sum;
}

/* Surface sand/stone use a 16-sample Y run with world Z as its origin.
 * The cached-gradient quirk makes independent per-point Perlin samples wrong. */
void beta173_noise_surface (const Beta173Noise *noise, double x, double z,
  double frequency, double out[16]) {
  if (!noise || !out) return;
  for (unsigned y = 0; y < 16; y ++) out[y] = 0;
  if (!valid_point(x * frequency, z * frequency, 0)) return;
  double scale = 1;
  for (unsigned octave = 0; octave < 4; octave ++) {
    const Beta173Noise *n = &noise[octave];
    double wx = x * (frequency * scale) + n->ox, wz = n->oz;
    unsigned ix = cell(floor(wx)), iz = cell(floor(wz));
    wx -= floor(wx); wz -= floor(wz);
    unsigned last_y = 256;
    double c[4] = {0};
    for (unsigned y = 0; y < 16; y ++) {
      double wy = (z + y) * (frequency * scale) + n->oy;
      unsigned iy = cell(floor(wy)); wy -= floor(wy);
      if (iy != last_y) {
        gradients(n, ix, iy, iz, wx, wy, wz, smooth(wx), c);
        last_y = iy;
      }
      out[y] += lerp(smooth(wz), lerp(smooth(wy), c[0], c[1]), lerp(smooth(wy), c[2], c[3])) / scale;
    }
    scale /= 2;
  }
}
