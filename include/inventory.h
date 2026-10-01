#ifndef H_INVENTORY
#define H_INVENTORY
#include <stdbool.h>
#include "globals.h"
/* Session-only metadata; the compact player/world save formats stay unchanged. */
void inventory_reset (PlayerData *player);
bool inventory_open (PlayerData *player, uint8_t window);
bool inventory_close (PlayerData *player);
uint8_t inventory_window (const PlayerData *player);
#endif
