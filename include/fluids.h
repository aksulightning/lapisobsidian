#ifndef H_FLUIDS
#define H_FLUIDS
#include <stdbool.h>
#include <stdint.h>
#include "globals.h"
#define FLUID_QUEUE_LIMIT 2048
#define FLUID_TICK_BUDGET 64
/* Edits keep their existing compact IDs in world.bin; the queue is transient. */
void fluids_init (void);
void fluids_block_changed (int x, int y, int z);
void fluids_tick (int64_t elapsed_us);
/* Buckets use the validated Use Item look direction, not Use Item On. */
bool fluids_use_bucket (PlayerData *player, float yaw, float pitch);
#endif
