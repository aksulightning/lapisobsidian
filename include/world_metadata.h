#ifndef H_WORLD_METADATA
#define H_WORLD_METADATA
#include <stdbool.h>
#include <stdint.h>

/* Load/create versioned seed metadata; refuse unversioned edit files. */
bool world_metadata_open (const char *metadata_path, const char *edits_path,
  uint64_t *seed, bool explicit_seed);
#endif
