/* Validate movement and apply fall damage, hunger and chunk visibility. */
#include "procedures.h"
#include "packets.h"
#include "tools.h"
#include "plates.h"
#include "registries.h"
#include "packet_dispatch.h"
#include "worldgen.h"
#include "world_border.h"
#include "mobs.h"
#include "commands.h"
#include <math.h>

void handlePlayerMovement(int client_fd, int length, int packet_id) {
  int expected = packet_id == 0x1D ? 25 : packet_id == 0x1E ? 33 : packet_id == 0x1F ? 9 : 1;
  if (length != expected) { recv_count = 0; return; }
  double x = 0, y = 0, z = 0;
  float yaw = 0, pitch = 0;
  uint8_t on_ground;

  // Read player position (and rotation)
  if (packet_id == 0x1D) cs_setPlayerPosition(client_fd, &x, &y, &z, &on_ground);
  else if (packet_id == 0x1F) cs_setPlayerRotation (client_fd, &yaw, &pitch, &on_ground);
  else if (packet_id == 0x20) cs_setPlayerMovementFlags (client_fd, &on_ground);
  else cs_setPlayerPositionAndRotation(client_fd, &x, &y, &z, &yaw, &pitch, &on_ground);

  if (recv_count <= 0) { recv_count = 0; return; }
  if ((packet_id == 0x1D || packet_id == 0x1E) &&
      (!isfinite(x) || !isfinite(y) || !isfinite(z) ||
       ((!plates_enabled || (plates_type() != PLATE_HUB && plates_type() != PLATE_SKYBOX)) && y < 0) || y < -64 || y >= 256)) {
    recv_count = 0;
    return;
  }
  if ((packet_id == 0x1E || packet_id == 0x1F) && (!isfinite(yaw) || !isfinite(pitch))) {
    recv_count = 0;
    return;
  }
  PlayerData *player;
  if (getPlayerData(client_fd, &player)) return;

  if ((packet_id == 0x1D || packet_id == 0x1E) && plates_movement_guard(player,x,y,z)) return;
  if ((packet_id == 0x1D || packet_id == 0x1E) && world_border_guard(player,x,z)) return;

  uint8_t block_feet = getBlockAt(player->x, player->y, player->z);
  uint8_t swimming = block_feet >= B_water && block_feet < B_water + 8;

  // Handle fall damage
  if (on_ground) {
    int16_t damage = player->grounded_y - player->y - 3;
    if (damage > 0 && (commands_gamemode(player) == 0 || commands_gamemode(player) == 2) && !swimming) {
      hurtEntity(client_fd, -1, D_fall, damage);
    }
    player->grounded_y = player->y;
  } else if (swimming) {
    player->grounded_y = player->y;
  }

  // Don't continue if all we got were flags
  if (packet_id == 0x20) return;

  // Update rotation in player data (if applicable)
  if (packet_id != 0x1D) {
    double angle = fmod((double)yaw + 180.0, 360.0);
    if (angle < 0) angle += 360.0;
    player->yaw = (int8_t)((angle - 180.0) * 127.0 / 180.0);
    if (pitch < -90) pitch = -90;
    if (pitch > 90) pitch = 90;
    player->pitch = (int8_t)(pitch / 90.0f * 127.0f);
  }

  // Whether to broadcast player position to other players
  uint8_t should_broadcast = true;

  #ifndef BROADCAST_ALL_MOVEMENT
    // If applicable, tie movement updates to the tickrate by using
    // a flag that gets reset on every tick. It might sound better
    // to just make the tick handler broadcast position updates, but
    // then we lose precision. While position is stored using integers,
    // here the client gives us doubles and floats directly.
    should_broadcast = !(player->flags & 0x40);
    if (should_broadcast) player->flags |= 0x40;
  #endif

  #ifdef SCALE_MOVEMENT_UPDATES_TO_PLAYER_COUNT
    // If applicable, broadcast only every client_count-th movement update
    if (++player->packets_since_update < client_count) {
      should_broadcast = false;
    } else {
      // Note that this does not explicitly set should_broadcast to true
      // This allows the above BROADCAST_ALL_MOVEMENT check to compound
      // Whether that's ever favorable is up for debate
      player->packets_since_update = 0;
    }
  #endif

  if (should_broadcast) {
    // If the packet had no rotation data, calculate it from player data
    if (packet_id == 0x1D) {
      yaw = player->yaw * 180 / 127;
      pitch = player->pitch * 90 / 127;
    }
    // Send current position data to all connected players
    for (int i = 0; i < MAX_PLAYERS; i ++) if (plates_player_active(&player_data[i])) {
      if (player_data[i].client_fd == -1) continue;
      if (player_data[i].flags & 0x20) continue;
      if (player_data[i].client_fd == client_fd) continue;
      if (packet_id == 0x1F) {
        sc_updateEntityRotation(player_data[i].client_fd, client_fd, player->yaw, player->pitch);
      } else {
        sc_teleportEntity(player_data[i].client_fd, client_fd, x, y, z, yaw, pitch);
      }
      sc_setHeadRotation(player_data[i].client_fd, client_fd, player->yaw);
    }
  }

  // Don't continue if all we got was rotation data
  if (packet_id == 0x1F) return;

  // Players send movement packets roughly 20 times per second when
  // moving, and much less frequently when standing still. We can
  // use this correlation between actions and packet count to cheaply
  // simulate hunger with a timer-based system, where the timer ticks
  // down with each position packet. The timer value itself then
  // naturally works as a substitute for saturation.
  if (commands_gamemode(player) != 1 && commands_gamemode(player) != 3) {
    if (player->saturation == 0) {
      if (player->hunger > 0) player->hunger--;
      player->saturation = 200;
      sc_setHealth(client_fd, player->health, player->hunger, player->saturation);
    } else if (player->flags & 0x08) {
      player->saturation -= 1;
    }
  }

  // Cast the values to short to get integer position
  short cx = (short)floor(x), cy = (short)y, cz = (short)floor(z);
  // Determine the player's chunk coordinates
  short _x = (short)div_floor(cx, 16), _z = (short)div_floor(cz, 16);
  // Calculate distance between previous and current chunk coordinates
  short dx = (short)(_x - div_floor(player->x, 16));
  short dz = (short)(_z - div_floor(player->z, 16));

  // Prevent players from leaving the world
  if (cy < 0) {
    cy = 0;
    player->grounded_y = 0;
    sc_synchronizePlayerPosition(client_fd, cx, 0, cz, player->yaw * 180 / 127, player->pitch * 90 / 127);
  } else if (cy > 255) {
    cy = 255;
    sc_synchronizePlayerPosition(client_fd, cx, 255, cz, player->yaw * 180 / 127, player->pitch * 90 / 127);
  }

  // Update position in player data
  player->x = cx;
  player->y = cy;
  player->z = cz;

  // Exit early if no chunk borders were crossed
  if (dx == 0 && dz == 0) return;

  // Check if the player has recently been in this chunk
  int found = false;
  for (int i = 0; i < VISITED_HISTORY; i ++) {
    if (player->visited_x[i] == _x && player->visited_z[i] == _z) {
      found = true;
      break;
    }
  }
  /* History suppresses repeat natural spawns, never chunk transmission. */

  // Update player's recently visited chunks
  for (int i = 0; i < VISITED_HISTORY - 1; i ++) {
    player->visited_x[i] = player->visited_x[i + 1];
    player->visited_z[i] = player->visited_z[i + 1];
  }
  player->visited_x[VISITED_HISTORY - 1] = _x;
  player->visited_z[VISITED_HISTORY - 1] = _z;

  if (!found) mobs_spawn_exploration(_x,_z,dx,dz,cy,fast_rand());

  world_send_view(client_fd,_x,_z,_x-dx,_z-dz,false);
}
