#ifndef H_WORLD_METADATA
#define H_WORLD_METADATA
#include <stdbool.h>
#include <stdint.h>

/* Load/create seed and startup mirroring metadata; refuse incompatible saves. */
bool world_metadata_open (const char *metadata_path, const char *edits_path,
  uint64_t *seed, bool explicit_seed, bool mirror_horizontal);
#endif
