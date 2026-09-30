#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include "world_metadata.h"

int main (void) {
  const char *meta = ".tests/seed.meta", *world = ".tests/edits.bin";
  remove(meta); remove(world);
  uint64_t seed = UINT64_MAX;
  assert(world_metadata_open(meta, world, &seed, true));
  seed = 0;
  assert(world_metadata_open(meta, world, &seed, false) && seed == UINT64_MAX);
  seed = 1;
  assert(!world_metadata_open(meta, world, &seed, true));
  FILE *f = fopen(meta, "r+b"); assert(f);
  assert(fputc('?', f) != EOF); assert(fclose(f) == 0);
  assert(!world_metadata_open(meta, world, &seed, false));
  remove(meta);
  f = fopen(world, "wb"); assert(f); assert(fclose(f) == 0);
  assert(!world_metadata_open(meta, world, &seed, false));
  remove(world);
  assert(world_metadata_open(meta, world, &seed, false));
  f = fopen(meta, "ab"); assert(f); assert(fputc(0, f) != EOF); assert(fclose(f) == 0);
  assert(!world_metadata_open(meta, world, &seed, false));
  remove(meta);
  puts("world metadata: persistence, seed mismatch, corruption and legacy rejection passed");
  return 0;
}
