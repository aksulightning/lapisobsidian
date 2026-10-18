/* Entity interaction, damage, armor and death consequences. */
#include "procedures.h"
#include "packets.h"
#include "tools.h"
#include "plates.h"
#include "registries.h"
#include "commands.h"
#include "items.h"
#include "mobs.h"
#include "inventory.h"
#include <string.h>

void interactEntity (int entity_id, int interactor_id) {

  PlayerData *player;
  if (getPlayerData(interactor_id, &player)) return;

  if (commands_gamemode(player) == 3) return;
  if (entity_id > -2 || entity_id < -1-MAX_MOBS) return;
  int mob_index = -entity_id - 2;
  if (mob_index < 0 || mob_index >= MAX_MOBS) return;
  MobData *mob = &mob_data[mob_index];

  switch (mob->type) {
    case 106: // Sheep
      if (player->inventory_items[player->hotbar] != I_shears)
        return;

      if ((mob->data >> 5) & 1) // Check if sheep has already been sheared
        return;

      mob->data |= 1 << 5; // Set sheared to true

      bumpToolDurability(player);

      uint8_t item_count = 1 + (fast_rand() & 1); // 1-2
      items_spawn(I_white_wool,item_count,mob->x,mob->y,mob->z,500);

      for (int i = 0; i < MAX_PLAYERS; i ++) if (plates_player_active(&player_data[i])) {
        PlayerData* player = &player_data[i];
        int client_fd = player->client_fd;

        if (client_fd == -1) continue;
        if (player->flags & 0x20) continue;

        sc_entityAnimation(client_fd, interactor_id, 0);
      }

      broadcastMobMetadata(-1, entity_id);

      break;
  }
}

void hurtEntity (int entity_id, int attacker_id, uint8_t damage_type, uint8_t damage) {

  if (attacker_id > 0) { // Attacker is a player

    PlayerData *player;
    if (getPlayerData(attacker_id, &player)) return;

    if (commands_gamemode(player) == 3) return;

    // Check if attack cooldown flag is set
    if (player->flags & 0x01) return;

    // Scale damage based on held item
    uint16_t held_item = player->inventory_items[player->hotbar];
    if (held_item == I_wooden_sword) damage *= 4;
    else if (held_item == I_golden_sword) damage *= 4;
    else if (held_item == I_stone_sword) damage *= 5;
    else if (held_item == I_iron_sword) damage *= 6;
    else if (held_item == I_diamond_sword) damage *= 7;
    else if (held_item == I_netherite_sword) damage *= 8;

    // Enable attack cooldown
    player->flags |= 0x01;
    player->flagval_8 = 0;

  }

  // Whether this attack caused the target entity to die
  uint8_t entity_died = false;

  if (entity_id > 0) { // The attacked entity is a player

    PlayerData *player;
    if (getPlayerData(entity_id, &player)) return;

    if (commands_gamemode(player) == 1 || commands_gamemode(player) == 3) return;

    // Don't continue if the player is already dead
    if (player->health == 0) return;

    // Calculate damage reduction from player's armor
    uint8_t defense = getPlayerDefensePoints(player);
    // This uses the old (pre-1.9) protection calculation. Factors are
    // scaled up 256 times to avoid floating point math. Due to lost
    // precision, the 4% reduction factor drops to ~3.9%, although the
    // the resulting effective damage is then also rounded down.
    uint8_t effective_damage = damage * (256 - defense * 10) / 256;

    // Process health change on the server
    if (player->health <= effective_damage) {

      player->health = 0;
      entity_died = true;

      // Prepare death message in recv_buffer
      uint8_t player_name_len = player_name_length(player->name);
      memcpy(recv_buffer, player->name, player_name_len);

      if (damage_type == D_fall && damage > 8) {
        // Killed by a greater than 5 block fall
        strcpy((char *)recv_buffer + player_name_len, " fell from a high place");
        recv_buffer[player_name_len + 23] = '\0';
      } else if (damage_type == D_fall) {
        // Killed by a less than 5 block fall
        strcpy((char *)recv_buffer + player_name_len, " hit the ground too hard");
        recv_buffer[player_name_len + 24] = '\0';
      } else if (damage_type == D_lava) {
        // Killed by being in lava
        strcpy((char *)recv_buffer + player_name_len, " tried to swim in lava");
        recv_buffer[player_name_len + 22] = '\0';
      } else if (attacker_id < -1) {
        // Killed by a mob
        strcpy((char *)recv_buffer + player_name_len, " was slain by a mob");
        recv_buffer[player_name_len + 19] = '\0';
      } else if (attacker_id > 0) {
        // Killed by a player
        PlayerData *attacker;
        if (getPlayerData(attacker_id, &attacker)) return;
        strcpy((char *)recv_buffer + player_name_len, " was slain by ");
        memcpy(recv_buffer + player_name_len + 14, attacker->name, player_name_length(attacker->name));
        recv_buffer[player_name_len + 14 + player_name_length(attacker->name)] = '\0';
      } else if (damage_type == D_cactus) {
        // Killed by being near a cactus
        strcpy((char *)recv_buffer + player_name_len, " was pricked to death");
        recv_buffer[player_name_len + 21] = '\0';
      } else {
        // Unknown death reason
        strcpy((char *)recv_buffer + player_name_len, " died");
        recv_buffer[player_name_len + 5] = '\0';
      }

    } else player->health -= effective_damage;

    // Update health on the client
    sc_setHealth(entity_id, player->health, player->hunger, player->saturation);

  } else { // The attacked entity is a mob

    if (entity_id > -2 || entity_id < -1-MAX_MOBS) return;
    int mob_index = -entity_id - 2;
    if (mob_index < 0 || mob_index >= MAX_MOBS) return;
    MobData *mob = &mob_data[mob_index];

    uint8_t mob_health = mob->data & 31;

    // Don't continue if the mob is already dead
    if (mob_health == 0) return;

    if (attacker_id > 0) {
      PlayerData *attacker;
      if (!getPlayerData(attacker_id,&attacker)) mobs_attacked(entity_id,attacker);
    }
    if (damage) mobs_hurt_sound(entity_id,mob_health <= damage);
    // Set the mob's panic timer
    mob->data |= (3 << 6);

    // Process health change on the server
    if (mob_health <= damage) {

      uint16_t loot = 0;
      uint8_t count = 0;
      switch (mob->type) {
        case 25: loot = I_chicken; count = 1; break;
        case 28: loot = I_beef; count = (uint8_t)(1+fast_rand()%3); break;
        case 95: loot = I_porkchop; count = (uint8_t)(1+fast_rand()%3); break;
        case 106: loot = I_mutton; count = (uint8_t)(1+(fast_rand()&1)); break;
        case MOB_SKELETON: loot = I_bone; count = (uint8_t)(1+fast_rand()%2); break;
        case MOB_SPIDER: loot = I_string; count = (uint8_t)(1+fast_rand()%2); break;
        case MOB_CREEPER: loot = I_gunpowder; count = (uint8_t)(fast_rand()%3); break;
        case 145: loot = I_rotten_flesh; count = (uint8_t)(fast_rand()%3); break;
        default: break;
      }
      /* Save the position before Y becomes the inherited death-animation timer. */
      if (count) items_spawn(loot,count,mob->x,mob->y,mob->z,500);
      if (mob->type == MOB_SKELETON) items_spawn(I_arrow,(uint8_t)(1+fast_rand()%2),mob->x,mob->y,mob->z,500);
      mob->data -= mob_health;
      mob->y = 0;
      entity_died = true;

    } else mob->data -= damage;

  }

  // Broadcast damage event to all players
  for (int i = 0; i < MAX_PLAYERS; i ++) if (plates_player_active(&player_data[i])) {
    int client_fd = player_data[i].client_fd;
    if (client_fd == -1) continue;
    sc_damageEvent(client_fd, entity_id, damage_type);
    // Below this, handle death events
    if (!entity_died) continue;
    sc_entityEvent(client_fd, entity_id, 3);
    if (entity_id >= 0) {
      // If a player died, broadcast their death message
      sc_systemChat(client_fd, (char *)recv_buffer, strlen((char *)recv_buffer));
    }
  }

}
