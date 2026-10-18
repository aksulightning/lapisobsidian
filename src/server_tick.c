/* Periodic world, survival and mob updates. */
#include "procedures.h"
#include "packets.h"
#include "tools.h"
#include "plates.h"
#include "registries.h"
#include "items.h"
#include "mobs.h"
#include "worldgen.h"
#include "serialize.h"

/* Tick the selected world; the caller owns Plate selection and persistence
 * transactions. Elapsed time is in microseconds, while player cooldowns count
 * calls, so this must run at TICKS_PER_SECOND rather than once per packet. */
void handleServerTick (int64_t time_since_last_tick) {

  // Update world time
  world_time = (world_time + time_since_last_tick / 50000) % 24000;
  // Increment server tick counter
  server_ticks ++;
  items_tick(time_since_last_tick);

  // Update player events
  for (int i = 0; i < MAX_PLAYERS; i ++) if (plates_player_active(&player_data[i])) {
    PlayerData *player = &player_data[i];
    if (player->client_fd == -1) continue; // Skip offline players
    if (player->flags & 0x20) { // Check "client loading" flag
      // If 3 seconds (60 vanilla ticks) have passed, assume player has loaded
      player->flagval_16 ++;
      if (player->flagval_16 > (uint16_t)(3 * TICKS_PER_SECOND)) {
        handlePlayerJoin(player);
      } else continue;
    }
    // Reset player attack cooldown
    if (player->flags & 0x01) {
      if (player->flagval_8 >= (uint8_t)(0.6f * TICKS_PER_SECOND)) {
        player->flags &= ~0x01;
        player->flagval_8 = 0;
      } else player->flagval_8 ++;
    }
    // Handle eating animation
    if (player->flags & 0x10) {
      if (player->flagval_16 >= (uint16_t)(1.6f * TICKS_PER_SECOND)) {
        handlePlayerEating(&player_data[i], false);
        player->flags &= ~0x10;
        player->flagval_16 = 0;
      } else player->flagval_16 ++;
    }
    // Reset movement update cooldown if not broadcasting every update
    // Effectively ties player movement updates to the tickrate
    #ifndef BROADCAST_ALL_MOVEMENT
      player->flags &= ~0x40;
    #endif
    // Below this, process events that happen once per second
    if (server_ticks % (uint32_t)TICKS_PER_SECOND != 0) continue;
    // Send Keep Alive and Update Time packets
    sc_keepAlive(player->client_fd);
    sc_updateTime(player->client_fd, world_time);
    // Tick damage from lava
    uint8_t block = getBlockAt(player->x, player->y, player->z);
    if (block >= B_lava && block < B_lava + 4) {
      hurtEntity(player->client_fd, -1, D_lava, 8);
    }
    #ifdef ENABLE_CACTUS_DAMAGE
    // Tick damage from a cactus block if one is under/inside or around the player.
    if (block == B_cactus ||
      getBlockAt(player->x + 1, player->y, player->z) == B_cactus ||
      getBlockAt(player->x - 1, player->y, player->z) == B_cactus ||
      getBlockAt(player->x, player->y, player->z + 1) == B_cactus ||
      getBlockAt(player->x, player->y, player->z - 1) == B_cactus
    ) hurtEntity(player->client_fd, -1, D_cactus, 4);
    #endif
    // Heal from saturation if player is able and has enough food
    if (player->health >= 20 || player->health == 0) continue;
    if (player->hunger < 18) continue;
    if (player->saturation >= 600) {
      player->saturation -= 600;
      player->health ++;
    } else {
      player->hunger --;
      player->health ++;
    }
    sc_setHealth(player->client_fd, player->health, player->hunger, player->saturation);
  }

  // Perform regular checks for if it's time to write to disk
  writeDataToDiskOnInterval();

  /**
   * If the RNG seed ever hits 0, it'll never generate anything
   * else. This is because the fast_rand function uses a simple
   * XORshift. This isn't a common concern, so we only check for
   * this periodically. If it does become zero, we reset it to
   * the world seed as a good-enough fallback.
   */
  if (rng_seed == 0) rng_seed = (uint32_t)world_seed | 1u;

  mobs_tick(time_since_last_tick);

}
