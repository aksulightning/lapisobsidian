#include "plates.h"
#include <math.h>
#include "world_border.h"
#include "packets.h"
#include "worldgen.h"

/* Ignore queued pre-teleport positions until the client arrives at spawn. */
static bool returning[MAX_PLAYERS];
static int player_index (const PlayerData *player) {
  for (int i = 0; i < MAX_PLAYERS; i++) if (plates_player_active(&player_data[i])) if (player == &player_data[i]) return i;
  return -1;
}
bool world_border_outside (double x, double z) {
  return !isfinite(x) || !isfinite(z) || x <= -WORLD_BORDER_LIMIT ||
    x >= WORLD_BORDER_LIMIT || z <= -WORLD_BORDER_LIMIT || z >= WORLD_BORDER_LIMIT;
}
void world_border_reset (PlayerData *player) {
  int slot = player_index(player); if (slot >= 0) returning[slot] = false;
}
void world_send_view (int fd, int cx, int cz, int old_cx, int old_cz, bool full) {
  if (cx < -2048 || cx > 2047 || cz < -2048 || cz > 2047 ||
      old_cx < -2048 || old_cx > 2047 || old_cz < -2048 || old_cz > 2047) return;
  sc_setCenterChunk(fd,cx,cz);
  if (full) sc_chunkDataAndUpdateLight(fd,cx,cz);
  for (int dx = -VIEW_DISTANCE; dx <= VIEW_DISTANCE; dx++) {
    for (int dz = -VIEW_DISTANCE; dz <= VIEW_DISTANCE; dz++) {
      int x = cx+dx, z = cz+dz;
      if (full && !dx && !dz) continue;
      if (!full && x >= old_cx-VIEW_DISTANCE && x <= old_cx+VIEW_DISTANCE &&
          z >= old_cz-VIEW_DISTANCE && z <= old_cz+VIEW_DISTANCE) continue;
      sc_chunkDataAndUpdateLight(fd,x,z);
    }
  }
}
void world_teleport (PlayerData *player, int x, int y, int z) {
  if (!player || y < 0 || y > 255) return;
  bool outside = world_border_outside(x,z);
  if (outside) { x = 8; y = (int)getHeightAt(8,8)+1; z = 8; }
  world_border_reset(player);
  player->x = (short)x; player->y = (uint8_t)y; player->z = (short)z;
  player->grounded_y = (uint8_t)y;
  for (unsigned i = 0; i < VISITED_HISTORY; i++) player->visited_x[i] = player->visited_z[i] = 32767;
  int cx = x/16-(x%16 < 0), cz = z/16-(z%16 < 0);
  world_send_view(player->client_fd,cx,cz,cx,cz,true);
  float yaw = (float)player->yaw*180.0f/127.0f, pitch = (float)player->pitch*90.0f/127.0f;
  sc_synchronizePlayerPosition(player->client_fd,x+0.5,y,z+0.5,yaw,pitch);
  for (int i = 0; i < MAX_PLAYERS; i++) if (plates_player_active(&player_data[i])) {
    PlayerData *other = &player_data[i];
    if (other->client_fd < 0 || other == player || (other->flags & 0x20)) continue;
    sc_teleportEntity(other->client_fd,player->client_fd,x+0.5,y,z+0.5,yaw,pitch);
  }
  if (outside) {
    char message[] = "World border reached (X/Z +/-4068). Returned to spawn.";
    sc_systemChat(player->client_fd,message,(uint16_t)(sizeof(message)-1));
    int slot = player_index(player); if (slot >= 0) returning[slot] = true;
  }
}
bool world_border_guard (PlayerData *player, double x, double z) {
  int slot = player_index(player); if (slot < 0) return true;
  if (returning[slot]) {
    if (x < 7 || x > 10 || z < 7 || z > 10 || !isfinite(x) || !isfinite(z)) return true;
    returning[slot] = false;
  }
  if (!world_border_outside(x,z)) return false;
  /* Pass a known boundary coordinate, avoiding any cast of network doubles. */
  world_teleport(player,WORLD_BORDER_LIMIT,0,0);
  return true;
}
