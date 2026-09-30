#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "world_metadata.h"
#include "beta173_worldgen.h"
#include "protocol.h"

bool world_metadata_open (const char *metadata_path, const char *edits_path,
  uint64_t *seed, bool explicit_seed) {
  if (!metadata_path || !edits_path || !seed) return false;
  uint8_t header[24] = {'L','A','P','I','S','O','B','S', 0,0,0,BETA173_GENERATOR_VERSION,
    0,0,(uint8_t)(LAPIS_PROTOCOL_VERSION >> 8),(uint8_t)(LAPIS_PROTOCOL_VERSION & 255)};
  FILE *file = fopen(metadata_path, "rb");
  if (file) {
    uint8_t actual[24];
    size_t read = fread(actual, 1, sizeof(actual), file);
    int extra = fgetc(file);
    bool io_ok = !ferror(file);
    if (fclose(file) != 0) io_ok = false;
    if (!io_ok || read != sizeof(actual) || extra != EOF || memcmp(actual, header, 16)) {
      fputs("Lapis Obsidian: invalid or incompatible world metadata\n", stderr);
      return false;
    }
    uint64_t saved_seed = 0;
    for (unsigned i = 16; i < 24; i ++) saved_seed = (saved_seed << 8) | actual[i];
    if (explicit_seed && saved_seed != *seed) {
      fputs("Lapis Obsidian: requested seed differs from saved world\n", stderr);
      return false;
    }
    *seed = saved_seed;
    return true;
  }
  if (errno != ENOENT) { perror("World metadata read"); return false; }
  file = fopen(edits_path, "rb");
  if (file) {
    fclose(file);
    fputs("Lapis Obsidian: legacy world has no generator metadata; use a new world directory\n", stderr);
    return false;
  }
  if (errno != ENOENT) { perror("World edits read"); return false; }
  for (unsigned i = 0; i < 8; i ++) header[16 + i] = (uint8_t)(*seed >> (56 - 8*i));
  file = fopen(metadata_path, "wbx");
  if (!file) { perror("World metadata create"); return false; }
  bool ok = fwrite(header, 1, sizeof(header), file) == sizeof(header);
  if (fclose(file) != 0) ok = false;
  if (!ok) fputs("Lapis Obsidian: failed to persist world seed\n", stderr);
  return ok;
}
