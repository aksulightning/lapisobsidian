#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include "world_metadata.h"

int main (void) {
  const char *meta = ".tests/seed.meta", *world = ".tests/edits.bin";
  remove(meta); remove(world);
  uint64_t seed = UINT64_MAX;
  assert(world_metadata_open(meta, world, &seed, true, false));
  assert(!world_metadata_open(meta, world, &seed, false, true));
  seed = 0;
  assert(world_metadata_open(meta, world, &seed, false, false) && seed == UINT64_MAX);
  seed = 1;
  assert(!world_metadata_open(meta, world, &seed, true, false));
  FILE *f = fopen(meta, "r+b"); assert(f);
  assert(fputc('?', f) != EOF); assert(fclose(f) == 0);
  assert(!world_metadata_open(meta, world, &seed, false, false));
  remove(meta);
  f = fopen(world, "wb"); assert(f); assert(fclose(f) == 0);
  assert(!world_metadata_open(meta, world, &seed, false, false));
  remove(world);
  assert(world_metadata_open(meta, world, &seed, false, false));
  f = fopen(meta, "ab"); assert(f); assert(fputc(0, f) != EOF); assert(fclose(f) == 0);
  assert(!world_metadata_open(meta, world, &seed, false, false));
  remove(meta);
  assert(world_metadata_open(meta, world, &seed, false, false));
  f = fopen(meta, "r+b"); assert(f);
  assert(fseek(f,8,SEEK_SET) == 0);
  const unsigned char previous_generator[4] = {0,0,0,1};
  assert(fwrite(previous_generator,1,4,f) == 4); assert(fclose(f) == 0);
  assert(!world_metadata_open(meta, world, &seed, false, false));
  remove(meta);
  assert(world_metadata_open(meta, world, &seed, false, true));
  assert(world_metadata_open(meta, world, &seed, false, true));
  assert(!world_metadata_open(meta, world, &seed, false, false));
  f = fopen(meta, "r+b"); assert(f); assert(fseek(f,24,SEEK_SET) == 0);
  assert(fputc(2,f) != EOF); assert(fclose(f) == 0);
  assert(!world_metadata_open(meta, world, &seed, false, true));
  remove(meta);
  assert(world_metadata_open(meta, world, &seed, false, false));
  unsigned char legacy[24];
  f = fopen(meta, "rb"); assert(f); assert(fread(legacy,1,24,f) == 24); assert(fclose(f) == 0);
  f = fopen(meta, "wb"); assert(f); assert(fwrite(legacy,1,24,f) == 24); assert(fclose(f) == 0);
  assert(world_metadata_open(meta, world, &seed, false, false));
  assert(!world_metadata_open(meta, world, &seed, false, true));
  remove(meta);
  puts("world metadata: persistence, seed mismatch, corruption, generator version, mirroring and legacy compatibility passed");
  return 0;
}
