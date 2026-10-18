/* Shared random state, monotonic timing and bounded player-name helpers. */
#include "tools.h"
#ifndef ESP_PLATFORM
#include <time.h>
#ifndef CLOCK_MONOTONIC
#define CLOCK_MONOTONIC 1
#endif
#endif

/* Advance the shared gameplay RNG; zero is an absorbing state, guarded by the
 * periodic tick. This deterministic generator is not suitable for secrets. */
uint32_t fast_rand () {
  rng_seed ^= rng_seed << 13;
  rng_seed ^= rng_seed >> 17;
  rng_seed ^= rng_seed << 5;
  return rng_seed;
}

/* Stateless seed mixer: callers retain/update state if they need a sequence. */
uint64_t splitmix64 (uint64_t state) {
  uint64_t z = state + 0x9e3779b97f4a7c15;
  z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9;
  z = (z ^ (z >> 27)) * 0x94d049bb133111eb;
  return z ^ (z >> 31);
}

#ifndef ESP_PLATFORM
// Returns system time in microseconds.
// On ESP-IDF, this is available in "esp_timer.h", and returns time *since
// the start of the program*, and NOT wall clock time. To ensure
// compatibility, this should only be used to measure time intervals.
int64_t get_program_time () {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (int64_t)ts.tv_sec * 1000000LL + ts.tv_nsec / 1000LL;
}
#endif

unsigned player_name_length(const char name[16]) { unsigned n=0; while(n<16 && name[n]) n++; return n; }
