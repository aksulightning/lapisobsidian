#ifndef H_LAPIS_DOORS
#define H_LAPIS_DOORS
#include <stdbool.h>
#include <stdint.h>
#include "globals.h"
#define DOOR_LIMIT 256

typedef struct {
  int16_t x, z;
  uint8_t y, facing, open, used; /* lower Y; facing north/south/west/east */
} Door;
bool doors_place (PlayerData *player, int x, int y, int z, uint8_t face);
bool doors_interact (PlayerData *player, int x, int y, int z);
const Door *doors_at (int x, int y, int z);
bool doors_state_at (int x, int y, int z, uint16_t *state);
void doors_block_changed (int x, int y, int z);
void doors_send_chunk (int fd, int cx, int cz);
bool doors_load (const char *path);
bool doors_save (void);
void doors_refresh_power (void);
#endif
