#ifndef H_FARMING
#define H_FARMING
#include <stdbool.h>
#include <stdint.h>
#include "globals.h"
#define FARM_LIMIT 256
bool farming_load (const char *path);
bool farming_save (void);
bool farming_use (PlayerData *p, int x, int y, int z, uint8_t face);
bool farming_harvest (PlayerData *p, int x, int y, int z);
void farming_block_changed (int x, int y, int z);
void farming_tick (int64_t elapsed_us);
bool farming_state_at (int x, int y, int z, uint8_t block, uint16_t *state);
void farming_send_chunk (int fd, int cx, int cz);
#endif
