#ifndef H_WORLD_BORDER
#define H_WORLD_BORDER
#include <stdbool.h>
#include "globals.h"

#define WORLD_BORDER_LIMIT 4068
#define WORLD_FAR_LANDS_START 3940

bool world_border_outside (double x, double z);
void world_border_reset (PlayerData *player);
bool world_border_guard (PlayerData *player, double x, double z);
void world_teleport (PlayerData *player, int x, int y, int z);
void world_send_view (int fd, int cx, int cz, int old_cx, int old_cz, bool full);
#endif
