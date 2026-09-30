#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "world_metadata.h"
#include "beta173_worldgen.h"
#include "protocol.h"

bool world_metadata_open (const char *metadata_path, const char *edits_path,
  uint64_t *seed, bool explicit_seed, bool mirror_horizontal) {
  if (!metadata_path || !edits_path || !seed) return false;
  uint8_t header[25] = {'L','A','P','I','S','O','B','S', 0,0,0,BETA173_GENERATOR_VERSION,
    0,0,(uint8_t)(LAPIS_PROTOCOL_VERSION >> 8),(uint8_t)(LAPIS_PROTOCOL_VERSION & 255)};
  header[24] = mirror_horizontal ? 1 : 0;
  FILE *file = fopen(metadata_path, "rb");
  if (file) {
    uint8_t actual[25];
    size_t read = fread(actual, 1, sizeof(actual), file);
    int extra = fgetc(file);
    bool io_ok = !ferror(file);
    if (fclose(file) != 0) io_ok = false;
    bool upgrade = read >= 24 && actual[11] == 2;
    if (!io_ok || (read != 24 && read != sizeof(actual)) || extra != EOF ||
        memcmp(actual, header, 11) || (!upgrade && actual[11] != header[11]) ||
        memcmp(actual+12,header+12,4) || (read == 25 && actual[24] > 1)) {
      fputs("Lapis Obsidian: invalid or incompatible world metadata\n", stderr);
      return false;
    }
    /* Older version-2 headers have no flags byte and mean unmirrored terrain. */
    bool saved_mirror = read == 25 && actual[24] != 0;
    if (saved_mirror != mirror_horizontal) {
      fputs("Lapis Obsidian: requested horizontal mirroring differs from saved world\n", stderr);
      return false;
    }
    uint64_t saved_seed = 0;
    for (unsigned i = 16; i < 24; i ++) saved_seed = (saved_seed << 8) | actual[i];
    if (explicit_seed && saved_seed != *seed) {
      fputs("Lapis Obsidian: requested seed differs from saved world\n", stderr);
      return false;
    }
    if (upgrade) {
      /* Only the version byte changes. Seed, protocol and mirror flags remain
       * intact, including if an interrupted write leaves the old version. */
      file = fopen(metadata_path,"r+b");
      if (!file) { perror("World metadata upgrade"); return false; }
      bool ok = fseek(file,11,SEEK_SET) == 0 && fputc(header[11],file) != EOF;
      if (fclose(file) != 0) ok = false;
      if (!ok) { fputs("Lapis Obsidian: could not persist world upgrade\n",stderr); return false; }
      fputs("Lapis Obsidian: upgraded world to generator 3; Far Lands terrain begins at X/Z +/-3940. Existing edits retained.\n",stderr);
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
