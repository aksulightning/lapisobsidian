#include "plates.h"
#include "fluids.h"
#include "items.h"
#include "inventory.h"
#include "farming.h"
#include "mobs.h"
#include "doors.h"
#include "circuits.h"
#include "musicbox.h"
#include "signs.h"
#include "commands.h"
#include "world_border.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#ifdef _WIN32
#include <winsock2.h>
#endif

#include "globals.h"
#include "tools.h"
#include "varnum.h"
#include "packets.h"
#include "registries.h"
#include "worldgen.h"
#include "structures.h"
#include "serialize.h"
#include "procedures.h"

int client_states[MAX_PLAYERS * 2];

void setClientState (int client_fd, int new_state) {
  // Look for a client state with a matching file descriptor
  for (int i = 0; i < MAX_PLAYERS * 2; i += 2) {
    if (client_states[i] != client_fd) continue;
    client_states[i + 1] = new_state;
    return;
  }
  // If the above failed, look for an unused client state slot
  for (int i = 0; i < MAX_PLAYERS * 2; i += 2) {
    if (client_states[i] != -1) continue;
    client_states[i] = client_fd;
    client_states[i + 1] = new_state;
    return;
  }
}

int getClientState (int client_fd) {
  for (int i = 0; i < MAX_PLAYERS * 2; i += 2) {
    if (client_states[i] != client_fd) continue;
    return client_states[i + 1];
  }
  return STATE_NONE;
}

int getClientIndex (int client_fd) {
  for (int i = 0; i < MAX_PLAYERS * 2; i += 2) {
    if (client_states[i] != client_fd) continue;
    return i;
  }
  return -1;
}

// Restores player data to initial state (fresh spawn)
void resetPlayerData (PlayerData *player) {
  inventory_reset(player);
  items_forget_player(player);
  mobs_forget_player(player);
  player->health = 20;
  player->hunger = 20;
  player->saturation = 2500;
  player->x = 8;
  player->z = 8;
  player->y = 80;
  player->flags |= 0x02;
  player->grounded_y = 0;
  for (int i = 0; i < 41; i ++) {
    player->inventory_items[i] = 0;
    player->inventory_count[i] = 0;
  }
  for (int i = 0; i < 9; i ++) {
    player->craft_items[i] = 0;
    player->craft_count[i] = 0;
  }
  player->flags &= ~0x80;
}

// Assigns the given data to a player_data entry
int reservePlayerData (int client_fd, uint8_t *uuid, char *name) {
  /* Never replace another live transport's player session. */
  for (int i = 0; i < MAX_PLAYERS; i++)
    if (player_data[i].client_fd >= 0 && player_data[i].client_fd != client_fd &&
        (!memcmp(player_data[i].uuid,uuid,16) || !strcmp(player_data[i].name,name))) return 1;

  for (int i = 0; i < MAX_PLAYERS; i ++) {
    // Found existing player entry (UUID match)
    if (memcmp(player_data[i].uuid, uuid, 16) == 0) {
      plates_player_reset(&player_data[i]);
      inventory_reset(&player_data[i]);
      commands_reset_player(&player_data[i]);
      mobs_forget_player(&player_data[i]);
      signs_reset_player(&player_data[i]);
      // Set network file descriptor and username
      player_data[i].client_fd = client_fd;
      memcpy(player_data[i].name, name, 16);
      // Flag player as loading
      player_data[i].flags |= 0x20;
      player_data[i].flagval_16 = 0;
      // Reset their recently visited chunk list
      for (int j = 0; j < VISITED_HISTORY; j ++) {
        player_data[i].visited_x[j] = 32767;
        player_data[i].visited_z[j] = 32767;
      }
      return 0;
    }
    // Search for unallocated player slots
    uint8_t empty = true;
    for (uint8_t j = 0; j < 16; j ++) {
      if (player_data[i].uuid[j] != 0) {
        empty = false;
        break;
      }
    }
    // Found free space for a player, initialize default parameters
    if (empty) {
      if (player_data_count >= MAX_PLAYERS) return 1;
      plates_player_reset(&player_data[i]);
      inventory_reset(&player_data[i]);
      commands_reset_player(&player_data[i]);
      mobs_forget_player(&player_data[i]);
      signs_reset_player(&player_data[i]);
      player_data[i].client_fd = client_fd;
      player_data[i].flags |= 0x20;
      player_data[i].flagval_16 = 0;
      memcpy(player_data[i].uuid, uuid, 16);
      memcpy(player_data[i].name, name, 16);
      resetPlayerData(&player_data[i]);
      player_data_count ++;
      return 0;
    }
  }

  return 1;

}

int getPlayerData (int client_fd, PlayerData **output) {
  for (int i = 0; i < MAX_PLAYERS; i ++) {
    if (player_data[i].client_fd == client_fd && plates_player_active(&player_data[i])) {
      *output = &player_data[i];
      return 0;
    }
  }
  return 1;
}

// Returns the player with the given name, or NULL if not found
PlayerData *getPlayerByName (int start_offset, int end_offset, uint8_t *buffer) {
  for (int i = 0; i < MAX_PLAYERS; i ++) {
    if (!plates_player_active(&player_data[i]) || player_data[i].client_fd == -1) continue;
    int j;
    for (j = start_offset; j < end_offset && j < 256 && buffer[j] != ' '; j++) {
      if (player_data[i].name[j - start_offset] != buffer[j]) break;
    }
    if ((j == end_offset || buffer[j] == ' ') && j < 256) {
      return &player_data[i];
    }
  }
  return NULL;
}


// Marks a client as disconnected and cleans up player data
void handlePlayerDisconnect (int client_fd) {
  // Search for a corresponding player in the player data array
  for (int i = 0; i < MAX_PLAYERS; i ++) {
    if (player_data[i].client_fd != client_fd) continue;
    // Detach before cleanup: returning stacks must not send to a stalled peer.
    player_data[i].client_fd = -1;
    inventory_close(&player_data[i]);
    commands_reset_player(&player_data[i]);
    signs_reset_player(&player_data[i]);
    items_forget_player(&player_data[i]);
    mobs_forget_player(&player_data[i]);
    // Prepare leave message for broadcast
    uint8_t player_name_len = strlen(player_data[i].name);
    strcpy((char *)recv_buffer, player_data[i].name);
    strcpy((char *)recv_buffer + player_name_len, " left the game");
    // Broadcast this player's leave to all other connected clients
    for (int j = 0; j < MAX_PLAYERS; j ++) {
      if (player_data[j].client_fd == client_fd) continue;
      if (player_data[j].flags & 0x20) continue;
      // Send chat message
      sc_systemChat(player_data[j].client_fd, (char *)recv_buffer, 14 + player_name_len);
      // Remove leaving player's entity
      sc_removeEntity(player_data[j].client_fd, client_fd);
    }
    break;
  }
  // Find the client state entry and reset it
  for (int i = 0; i < MAX_PLAYERS * 2; i += 2) {
    if (client_states[i] == client_fd) {
      client_states[i] = -1;
      return;
    }
  }
}

// Marks a client as connected and broadcasts their data to other players
void handlePlayerJoin (PlayerData* player) {

  // Prepare join message for broadcast
  uint8_t player_name_len = strlen(player->name);
  strcpy((char *)recv_buffer, player->name);
  strcpy((char *)recv_buffer + player_name_len, " joined the game");

  // Inform other clients (and the joining client) of the player's name and entity
  for (int i = 0; i < MAX_PLAYERS; i ++) if (plates_player_active(&player_data[i])) {
    sc_systemChat(player_data[i].client_fd, (char *)recv_buffer, 16 + player_name_len);
    sc_playerInfoUpdateAddPlayer(player_data[i].client_fd, *player);
    if (player_data[i].client_fd != player->client_fd) {
      sc_spawnEntityPlayer(player_data[i].client_fd, *player);
    }
  }

  // Clear "client loading" flag and fallback timer
  player->flags &= ~0x20;
  player->flagval_16 = 0;
  items_sync_player(player);
  mobs_sync_player(player);

}

void disconnectClient (int *client_fd, int cause) {
  if (*client_fd == -1) return;
  client_count --;
  setClientState(*client_fd, STATE_NONE);
  handlePlayerDisconnect(*client_fd);
  #ifdef _WIN32
  closesocket(*client_fd);
  printf("Disconnected client %d, cause: %d, errno: %d\n", *client_fd, cause, WSAGetLastError());
  #else
  close(*client_fd);
  printf("Disconnected client %d, cause: %d, errno: %d\n\n", *client_fd, cause, errno);
  #endif
  *client_fd = -1;
}

uint8_t serverSlotToClientSlot (int window_id, uint8_t slot) {

  if (window_id == 0) { // player inventory

    if (slot < 9) return slot + 36;
    if (slot >= 9 && slot <= 35) return slot;
    if (slot == 40) return 45;
    if (slot >= 36 && slot <= 39) return 44 - slot;
    if (slot >= 41 && slot <= 44) return slot - 40;

  } else if (window_id == 12) { // crafting table

    if (slot >= 41 && slot <= 49) return slot - 40;
    return serverSlotToClientSlot(0, slot - 1);

  } else if (window_id == 14) { // furnace

    if (slot >= 41 && slot <= 43) return slot - 41;
    return serverSlotToClientSlot(0, slot + 6);

  }

  return 255;
}

uint8_t clientSlotToServerSlot (int window_id, uint8_t slot) {

  if (window_id == 0) { // player inventory

    if (slot >= 36 && slot <= 44) return slot - 36;
    if (slot >= 9 && slot <= 35) return slot;
    if (slot == 45) return 40;
    if (slot >= 5 && slot <= 8) return 44 - slot;

    // map inventory crafting slots to player data crafting grid (semi-hack)
    // this abuses the fact that the buffers are adjacent in player data
    if (slot == 1) return 41;
    if (slot == 2) return 42;
    if (slot == 3) return 44;
    if (slot == 4) return 45;

  } else if (window_id == 12) { // crafting table

    // same crafting offset overflow hack as above
    if (slot >= 1 && slot <= 9) return 40 + slot;
    // the rest of the slots are identical, just shifted by one
    if (slot >= 10 && slot <= 45) return clientSlotToServerSlot(0, slot - 1);

  } else if (window_id == 14) { // furnace

    // move furnace items to the player's crafting grid
    // this lets us put them back in the inventory once the window closes
    if (slot <= 2) return 41 + slot;
    // the rest of the slots are identical, just shifted by 6
    if (slot >= 3 && slot <= 38) return clientSlotToServerSlot(0, slot + 6);

  }
  #ifdef ALLOW_CHESTS
  else if (window_id == 2) { // chest

    // overflow chest slots into crafting grid
    // technically invalid, expected to be handled on a per-case basis
    if (slot <= 26) return 41 + slot;
    // the rest of the slots are identical, just shifted by 18
    if (slot >= 27 && slot <= 62) return clientSlotToServerSlot(0, slot - 18);

  }
  #endif

  return 255;
}

int givePlayerItem (PlayerData *player, uint16_t item, uint8_t count) {

  if (item == 0 || count == 0) return 0;

  uint8_t slot = 255;
  uint8_t stack_size = getItemStackSize(item);

  for (int i = 0; i < 41; i ++) {
    if (player->inventory_items[i] == item && player->inventory_count[i] <= stack_size - count) {
      slot = i;
      break;
    }
  }

  if (slot == 255) {
    for (int i = 0; i < 41; i ++) {
      if (player->inventory_count[i] == 0) {
        slot = i;
        break;
      }
    }
  }

  // Fail to assign item if slot is outside of main inventory
  if (slot >= 36) return 1;

  player->inventory_items[slot] = item;
  player->inventory_count[slot] += count;
  sc_setContainerSlot(player->client_fd, 0, serverSlotToClientSlot(0, slot), player->inventory_count[slot], item);

  return 0;

}

// Sends the full sequence for spawning the player to the client
void spawnPlayer (PlayerData *player) {

  world_border_reset(player);
  if (world_border_outside(player->x,player->z)) {
    player->x = player->z = 8;
    player->y = (uint8_t)(getHeightAt(8,8)+1);
    player->grounded_y = player->y;
  }

  // Player spawn coordinates, initialized to placeholders
  float spawn_x = 8.5f, spawn_y = 80.0f, spawn_z = 8.5f;
  float spawn_yaw = 0.0f, spawn_pitch = 0.0f;

  if (player->flags & 0x02) { // Is this a new player?
    // Determine spawning Y coordinate based on terrain height
    spawn_y = getHeightAt(8, 8) + 1;
    player->y = spawn_y;
    player->flags &= ~0x02;
  } else { // Not a new player
    // Calculate spawn position from player data
    spawn_x = (float)player->x + 0.5;
    spawn_y = player->y;
    spawn_z = (float)player->z + 0.5;
    spawn_yaw = player->yaw * 180 / 127;
    spawn_pitch = player->pitch * 90 / 127;
  }

  // Teleport player to spawn coordinates (first pass)
  sc_synchronizePlayerPosition(player->client_fd, spawn_x, spawn_y, spawn_z, spawn_yaw, spawn_pitch);

  task_yield(); // Check task timer between packets

  // Clear crafting grid residue, unlock craft_items
  for (int i = 0; i < 9; i++) {
    player->craft_items[i] = 0;
    player->craft_count[i] = 0;
  }
  player->flags &= ~0x80;

  // Sync client inventory and hotbar
  for (uint8_t i = 0; i < 41; i ++) {
    sc_setContainerSlot(player->client_fd, 0, serverSlotToClientSlot(0, i), player->inventory_count[i], player->inventory_items[i]);
  }
  sc_setHeldItem(player->client_fd, player->hotbar);
  // Sync client health and hunger
  sc_setHealth(player->client_fd, player->health, player->hunger, player->saturation);
  // Sync client clock time
  sc_updateTime(player->client_fd, world_time);

  sc_playerAbilities(player->client_fd,commands_abilities(player));

  // Calculate player's chunk coordinates
  short _x = div_floor(player->x, 16), _z = div_floor(player->z, 16);

  // Indicate that we're about to send chunk data
  sc_setDefaultSpawnPosition(player->client_fd, 8, 80, 8);
  sc_startWaitingForChunks(player->client_fd);
  sc_setCenterChunk(player->client_fd, _x, _z);

  task_yield(); // Check task timer between packets

  // Send spawn chunk first
  sc_chunkDataAndUpdateLight(player->client_fd, _x, _z);
  for (int i = -VIEW_DISTANCE; i <= VIEW_DISTANCE; i ++) {
    for (int j = -VIEW_DISTANCE; j <= VIEW_DISTANCE; j ++) {
      if (i == 0 && j == 0) continue;
      sc_chunkDataAndUpdateLight(player->client_fd, _x + i, _z + j);
    }
  }
  // Re-teleport player after all chunks have been sent
  sc_synchronizePlayerPosition(player->client_fd, spawn_x, spawn_y, spawn_z, spawn_yaw, spawn_pitch);

  task_yield(); // Check task timer between packets

}

// Broadcasts a player's entity metadata (sneak/sprint state) to other players
void broadcastPlayerMetadata (PlayerData *player) {
  uint8_t sneaking = (player->flags & 0x04) != 0;
  uint8_t sprinting = (player->flags & 0x08) != 0;

  uint8_t entity_bit_mask = 0;
  if (sneaking) entity_bit_mask |= 0x02;
  if (sprinting) entity_bit_mask |= 0x08;

  int pose = 0;
  if (sneaking) pose = 5;

  EntityData metadata[] = {
    {
      0,                   // Index (Entity Bit Mask)
      0,                   // Type (Byte)
      { entity_bit_mask }, // Value
    },
    {
      6,        // Index (Pose),
      21,       // Type (Pose),
      { pose }, // Value (Standing)
    }
  };

  for (int i = 0; i < MAX_PLAYERS; i ++) if (plates_player_active(&player_data[i])) {
    PlayerData* other_player = &player_data[i];
    int client_fd = other_player->client_fd;

    if (client_fd == -1) continue;
    if (client_fd == player->client_fd) continue;
    if (other_player->flags & 0x20) continue;

    sc_setEntityMetadata(client_fd, player->client_fd, metadata, 2);
  }
}

// Sends a mob's entity metadata to the given player.
// If client_fd is -1, broadcasts to all player
void broadcastMobMetadata (int client_fd, int entity_id) {

  if (entity_id > -2 || entity_id < -1-MAX_MOBS) return;
  int mob_index = -entity_id - 2;
  if (mob_index < 0 || mob_index >= MAX_MOBS) return;
  MobData *mob = &mob_data[mob_index];

  EntityData *metadata;
  size_t length;

  switch (mob->type) {
    case 106: // Sheep
      if (!((mob->data >> 5) & 1)) // Don't send metadata if sheep isn't sheared
        return;

      metadata = malloc(sizeof *metadata);
      metadata[0] = (EntityData){
        17,                // Index (Sheep Bit Mask),
        0,                 // Type (Byte),
        { (uint8_t)0x10 }, // Value
      };
      length = 1;

      break;

    default: return;
  }

  if (client_fd == -1) {
    for (int i = 0; i < MAX_PLAYERS; i ++) if (plates_player_active(&player_data[i])) {
      PlayerData* player = &player_data[i];
      client_fd = player->client_fd;

      if (client_fd == -1) continue;
      if (player->flags & 0x20) continue;

      sc_setEntityMetadata(client_fd, entity_id, metadata, length);
    }
  } else {
    sc_setEntityMetadata(client_fd, entity_id, metadata, length);
  }

  free(metadata);
}

uint8_t getBlockChange (short x, uint8_t y, short z) {
  for (int i = 0; i < block_changes_count; i ++) {
    if (block_changes[i].block == 0xFF) continue;
    if (
      block_changes[i].x == x &&
      block_changes[i].y == y &&
      block_changes[i].z == z
    ) return block_changes[i].block;
    #ifdef ALLOW_CHESTS
      // Skip chest contents
      if (block_changes[i].block == B_chest) i += 14;
    #endif
  }
  return 0xFF;
}

// Handle running out of memory for new block changes
void failBlockChange (short x, uint8_t y, short z, uint8_t block) {
  (void)block;

  // Get previous block at this location
  uint8_t before = getBlockAt(x, y, z);

  // Broadcast a new update to all players
  for (int i = 0; i < MAX_PLAYERS; i ++) if (plates_player_active(&player_data[i])) {
    if (player_data[i].client_fd == -1) continue;
    if (player_data[i].flags & 0x20) continue;
    // Reset the block they tried to change
    sc_blockUpdate(player_data[i].client_fd, x, y, z, before);
    // Broadcast a chat message warning about the limit
    sc_systemChat(player_data[i].client_fd, "Block changes limit exceeded. Restore original terrain to continue.", 67);
  }

}

uint8_t makeBlockChange (short x, uint8_t y, short z, uint8_t block) {

  // Transmit block update to all in-game clients
  for (int i = 0; i < MAX_PLAYERS; i ++) if (plates_player_active(&player_data[i])) {
    if (player_data[i].client_fd == -1) continue;
    if (player_data[i].flags & 0x20) continue;
    sc_blockUpdate(player_data[i].client_fd, x, y, z, block);
  }

  // Calculate terrain at these coordinates and compare it to the input block.
  // Since block changes get overlayed on top of terrain, we don't want to
  // store blocks that don't differ from the base terrain.
  ChunkAnchor anchor = {
    .x = x / CHUNK_SIZE,
    .z = z / CHUNK_SIZE
  };
  if (x % CHUNK_SIZE < 0) anchor.x --;
  if (z % CHUNK_SIZE < 0) anchor.z --;
  anchor.hash = getChunkHash(anchor.x, anchor.z);
  anchor.biome = getChunkBiome(anchor.x, anchor.z);

  uint8_t is_base_block = block == getTerrainAt(x, y, z, anchor);

  // In the block_changes array, 0xFF indicates a missing/restored entry.
  // We track the position of the first such "gap" for when the operation
  // isn't replacing an existing block change.
  int first_gap = block_changes_count;

  // Prioritize replacing entries with matching coordinates
  // This prevents having conflicting entries for one set of coordinates
  for (int i = 0; i < block_changes_count; i ++) {
    if (block_changes[i].block == 0xFF) {
      if (first_gap == block_changes_count) first_gap = i;
      continue;
    }
    if (
      block_changes[i].x == x &&
      block_changes[i].y == y &&
      block_changes[i].z == z
    ) {
      #ifdef ALLOW_CHESTS
      // When replacing chests, clear following 14 entries too (item data)
      if (block_changes[i].block == B_chest) {
        for (int j = 1; j < 15; j ++) block_changes[i + j].block = 0xFF;
      }
      #endif
      if (is_base_block) block_changes[i].block = 0xFF;
      else {
        #ifdef ALLOW_CHESTS
        // When placing chests, just unallocate the target block and fall
        // through to the chest-specific routine below.
        if (block == B_chest) {
          block_changes[i].block = 0xFF;
          if (first_gap > i) first_gap = i;
          #ifndef DISK_SYNC_BLOCKS_ON_INTERVAL
          writeBlockChangesToDisk(i, i);
          #endif
          break;
        }
        #endif
        block_changes[i].block = block;
      }
      #ifndef DISK_SYNC_BLOCKS_ON_INTERVAL
      writeBlockChangesToDisk(i, i);
      #endif
      signs_block_changed(x,y,z,block); doors_block_changed(x,y,z); circuits_block_changed(x,y,z,block); musicbox_block_changed(x,y,z); farming_block_changed(x,y,z); fluids_block_changed(x,y,z); return 0;
    }
    #ifdef ALLOW_CHESTS
    if (block_changes[i].block == B_chest) i += 14;
    #endif
  }

  // Don't create a new entry if it contains the base terrain block
  if (is_base_block) { signs_block_changed(x,y,z,block); doors_block_changed(x,y,z); circuits_block_changed(x,y,z,block); musicbox_block_changed(x,y,z); farming_block_changed(x,y,z); fluids_block_changed(x,y,z); return 0; }

  #ifdef ALLOW_CHESTS
  if (block == B_chest) {
    // Chests require 15 entries total, so for maximum space-efficiency,
    // we have to find a continuous gap that's at least 15 slots wide.
    // By design, this loop also continues past the current search range,
    // which naturally appends the chest to the end if a gap isn't found.
    int last_real_entry = first_gap - 1;
    for (int i = first_gap; i <= block_changes_count + 15; i ++) {
      if (i >= MAX_BLOCK_CHANGES) break; // No more space, trigger failBlockChange

      if (block_changes[i].block != 0xFF) {
        last_real_entry = i;
        continue;
      }
      if (i - last_real_entry != 15) continue;
      // A wide enough gap has been found, assign the chest
      block_changes[last_real_entry + 1].x = x;
      block_changes[last_real_entry + 1].y = y;
      block_changes[last_real_entry + 1].z = z;
      block_changes[last_real_entry + 1].block = block;
      // Zero out the following 14 entries for item data
      for (int i = 2; i <= 15; i ++) {
        block_changes[last_real_entry + i].x = 0;
        block_changes[last_real_entry + i].y = 0;
        block_changes[last_real_entry + i].z = 0;
        block_changes[last_real_entry + i].block = 0;
      }
      // Extend future search range if necessary
      if (i >= block_changes_count) {
        block_changes_count = i + 1;
      }
      // Write changes to disk (if applicable)
      #ifndef DISK_SYNC_BLOCKS_ON_INTERVAL
      writeBlockChangesToDisk(last_real_entry + 1, last_real_entry + 15);
      #endif
      signs_block_changed(x,y,z,block); doors_block_changed(x,y,z); circuits_block_changed(x,y,z,block); musicbox_block_changed(x,y,z); farming_block_changed(x,y,z); fluids_block_changed(x,y,z); return 0;
    }
    // If we're here, no changes were made
    failBlockChange(x, y, z, block);
    return 1;
  }
  #endif

  // Handle running out of memory for new block changes
  if (first_gap == MAX_BLOCK_CHANGES) {
    failBlockChange(x, y, z, block);
    return 1;
  }

  // Fall back to storing the change at the first possible gap
  block_changes[first_gap].x = x;
  block_changes[first_gap].y = y;
  block_changes[first_gap].z = z;
  block_changes[first_gap].block = block;
  // Write change to disk (if applicable)
  #ifndef DISK_SYNC_BLOCKS_ON_INTERVAL
  writeBlockChangesToDisk(first_gap, first_gap);
  #endif
  // Extend future search range if we've appended to the end
  if (first_gap == block_changes_count) {
    block_changes_count ++;
  }

  signs_block_changed(x,y,z,block); doors_block_changed(x,y,z); circuits_block_changed(x,y,z,block); musicbox_block_changed(x,y,z); farming_block_changed(x,y,z); fluids_block_changed(x,y,z); return 0;
}

// Returns the result of mining a block, taking into account the block type and tools
// Probability numbers obtained with this formula: N = floor(P * (2 ^ 32))
uint16_t getMiningResult (uint16_t held_item, uint8_t block) {

  switch (block) {

    case B_farmland: return I_dirt;
    case B_wheat: return I_wheat_seeds;
    case B_short_grass:
    case B_fern:
      if (held_item == I_shears) return registry_block_item(block);
      return (fast_rand()&7u) == 0 ? I_wheat_seeds : 0;

    case B_oak_leaves:
      if (held_item == I_shears) return I_oak_leaves;
      uint32_t r = fast_rand();
      if (r < 21474836) return I_apple; // 0.5%
      if (r < 85899345) return I_stick; // 2%
      if (r < 214748364) return I_oak_sapling; // 5%
      return 0;
      break;

    case B_stone:
    case B_cobblestone:
    case B_stone_slab:
    case B_cobblestone_slab:
    case B_sandstone:
    case B_furnace:
    case B_coal_ore:
    case B_iron_ore:
    case B_iron_block:
    case B_gold_block:
    case B_diamond_block:
    case B_redstone_block:
    case B_coal_block:
      // Check if player is holding (any) pickaxe
      if (
        held_item != I_wooden_pickaxe &&
        held_item != I_stone_pickaxe &&
        held_item != I_iron_pickaxe &&
        held_item != I_golden_pickaxe &&
        held_item != I_diamond_pickaxe &&
        held_item != I_netherite_pickaxe
      ) return 0;
      break;

    case B_gold_ore:
    case B_redstone_ore:
    case B_diamond_ore:
      // Check if player is holding an iron (or better) pickaxe
      if (
        held_item != I_iron_pickaxe &&
        held_item != I_golden_pickaxe &&
        held_item != I_diamond_pickaxe &&
        held_item != I_netherite_pickaxe
      ) return 0;
      break;

    case B_snow:
      // Check if player is holding (any) shovel
      if (
        held_item != I_wooden_shovel &&
        held_item != I_stone_shovel &&
        held_item != I_iron_shovel &&
        held_item != I_golden_shovel &&
        held_item != I_diamond_shovel &&
        held_item != I_netherite_shovel
      ) return 0;
      break;

    default: break;
  }

  if (block == B_redstone_ore) return I_redstone;
  return registry_block_item(block);

}

// Rolls a random number to determine whether the player's tool should break
void bumpToolDurability (PlayerData *player) {

  uint16_t held_item = player->inventory_items[player->hotbar];

  // In order to avoid storing durability data, items break randomly with
  // the probability weighted based on vanilla durability.
  uint32_t r = fast_rand();
  if (
    ((held_item == I_wooden_pickaxe || held_item == I_wooden_axe || held_item == I_wooden_shovel || held_item == I_wooden_hoe) && r < 72796055) ||
    ((held_item == I_stone_pickaxe || held_item == I_stone_axe || held_item == I_stone_shovel || held_item == I_stone_hoe) && r < 32786009) ||
    ((held_item == I_iron_pickaxe || held_item == I_iron_axe || held_item == I_iron_shovel || held_item == I_iron_hoe) && r < 17179869) ||
    ((held_item == I_golden_pickaxe || held_item == I_golden_axe || held_item == I_golden_shovel || held_item == I_golden_hoe) && r < 134217728) ||
    ((held_item == I_diamond_pickaxe || held_item == I_diamond_axe || held_item == I_diamond_shovel || held_item == I_diamond_hoe) && r < 2751420) ||
    ((held_item == I_netherite_pickaxe || held_item == I_netherite_axe || held_item == I_netherite_shovel || held_item == I_netherite_hoe) && r < 2114705) ||
    (held_item == I_shears && r < 18046081)
  ) {
    player->inventory_items[player->hotbar] = 0;
    player->inventory_count[player->hotbar] = 0;
    sc_entityEvent(player->client_fd, player->client_fd, 47);
    sc_setContainerSlot(player->client_fd, 0, serverSlotToClientSlot(0, player->hotbar), 0, 0);
  }

}

/* Single-block flowers and mushrooms share the modern client's instant break. */
static uint8_t isSmallPlant (uint8_t block) {
  return (block >= B_dandelion && block <= B_red_mushroom) || block == B_fern;
}

// Checks whether the given block would be mined instantly with the held tool
uint8_t isInstantlyMined (PlayerData *player, uint8_t block) {

  uint16_t held_item = player->inventory_items[player->hotbar];

  if (
    block == B_snow ||
    block == B_snow_block
  ) return (
    held_item == I_stone_shovel ||
    held_item == I_iron_shovel ||
    held_item == I_diamond_shovel ||
    held_item == I_netherite_shovel ||
    held_item == I_golden_shovel
  );

  if (block == B_oak_leaves)
    return held_item == I_shears;

  return (
    isSmallPlant(block) ||
    block == B_dead_bush ||
    block == B_short_grass ||
    block == B_torch ||
    block == B_redstone_torch ||
    block == B_lever ||
    block == B_lily_pad ||
    block == B_oak_sapling
  );

}

// Checks whether the given block has to have something beneath it
uint8_t isColumnBlock (uint8_t block) {
  return (
    isSmallPlant(block) ||
    block == B_snow ||
    block == B_moss_carpet ||
    block == B_cactus ||
    block == B_short_grass ||
    block == B_dead_bush ||
    block == B_sand ||
    block == B_torch ||
    block == B_redstone_torch ||
    block == B_lever ||
    block == B_oak_sapling
  );
}

// Checks whether the given block is non-solid
uint8_t isPassableBlock (uint8_t block) {
  return (
    isSmallPlant(block) ||
    block == B_air ||
    block == B_oak_sign ||
    (block >= B_water && block < B_water + 8) ||
    (block >= B_lava && block < B_lava + 4) ||
    block == B_snow ||
    block == B_moss_carpet ||
    block == B_short_grass ||
    block == B_dead_bush ||
    block == B_torch || block == B_redstone_torch || block == B_lever ||
    block == B_stone_pressure_plate || block == B_oak_pressure_plate || block == B_wheat
  );
}
// Checks whether the given block is non-solid and spawnable
uint8_t isPassableSpawnBlock (uint8_t block) {
    if ((block >= B_water && block < B_water + 8) ||
        (block >= B_lava && block < B_lava + 4))
    {
        return 0;
    }
    return isPassableBlock(block);
}

// Checks whether the given block can be replaced by another block
uint8_t isReplaceableBlock (uint8_t block) {
  return (
    block == B_air ||
    (block >= B_water && block < B_water + 8) ||
    (block >= B_lava && block < B_lava + 4) ||
    block == B_short_grass ||
    block == B_snow
  );
}

// Checks whether the given item can be used in a composter
// Returns the probability (out of 2^32) to return bone meal
uint32_t isCompostItem (uint16_t item) {

  // Output values calculated using the following formula:
  // P = 2^32 / (7 / compost_chance)

  if ( // Compost chance: 30%
    item == I_oak_leaves ||
    item == I_short_grass ||
    item == I_wheat_seeds ||
    item == I_oak_sapling ||
    item == I_moss_carpet
  ) return 184070026;

  if ( // Compost chance: 50%
    item == I_cactus ||
    item == I_sugar_cane
  ) return 306783378;

  if ( // Compost chance: 65%
    item == I_apple ||
    item == I_lily_pad
  ) return 398818392;

  return 0;
}

// Returns the maximum stack size of an item
uint8_t getItemStackSize (uint16_t item) {
  if (item == I_water_bucket || item == I_lava_bucket) return 1;
  if (item == I_bucket) return 16;

  if (
    // Pickaxes
    item == I_wooden_pickaxe ||
    item == I_stone_pickaxe ||
    item == I_iron_pickaxe ||
    item == I_golden_pickaxe ||
    item == I_diamond_pickaxe ||
    item == I_netherite_pickaxe ||
    // Axes
    item == I_wooden_axe ||
    item == I_stone_axe ||
    item == I_iron_axe ||
    item == I_golden_axe ||
    item == I_diamond_axe ||
    item == I_netherite_axe ||
    // Shovels
    item == I_wooden_shovel ||
    item == I_stone_shovel ||
    item == I_iron_shovel ||
    item == I_golden_shovel ||
    item == I_diamond_shovel ||
    item == I_netherite_shovel ||
    // Swords
    item == I_wooden_sword ||
    item == I_stone_sword ||
    item == I_iron_sword ||
    item == I_golden_sword ||
    item == I_diamond_sword ||
    item == I_netherite_sword ||
    // Hoes
    item == I_wooden_hoe ||
    item == I_stone_hoe ||
    item == I_iron_hoe ||
    item == I_golden_hoe ||
    item == I_diamond_hoe ||
    item == I_netherite_hoe ||
    // Shears
    item == I_shears || item == I_bow
  ) return 1;

  if (
    item == I_snowball
  ) return 16;

  return 64;
}

// Returns defense points for the given piece of armor
// If the input item is not armor, returns 0
uint8_t getItemDefensePoints (uint16_t item) {

  switch (item) {
    case I_leather_helmet: return 1;
    case I_golden_helmet: return 2;
    case I_iron_helmet: return 2;
    case I_diamond_helmet: // Same as netherite
    case I_netherite_helmet: return 3;
    case I_leather_chestplate: return 3;
    case I_golden_chestplate: return 5;
    case I_iron_chestplate: return 6;
    case I_diamond_chestplate: // Same as netherite
    case I_netherite_chestplate: return 8;
    case I_leather_leggings: return 2;
    case I_golden_leggings: return 3;
    case I_iron_leggings: return 5;
    case I_diamond_leggings: // Same as netherite
    case I_netherite_leggings: return 6;
    case I_leather_boots: return 1;
    case I_golden_boots: return 1;
    case I_iron_boots: return 2;
    case I_diamond_boots: // Same as netherite
    case I_netherite_boots: return 3;
    default: break;
  }

  return 0;
}

// Calculates total defense points for the player's equipped armor
uint8_t getPlayerDefensePoints (PlayerData *player) {
  return (
    // Helmet
    getItemDefensePoints(player->inventory_items[39]) +
    // Chestplate
    getItemDefensePoints(player->inventory_items[38]) +
    // Leggings
    getItemDefensePoints(player->inventory_items[37]) +
    // Boots
    getItemDefensePoints(player->inventory_items[36])
  );
}

// Returns the designated server slot for the given piece of armor
// If input item is not armor, returns 255
uint8_t getArmorItemSlot (uint16_t item) {

    switch (item) {
    case I_leather_helmet:
    case I_golden_helmet:
    case I_iron_helmet:
    case I_diamond_helmet:
    case I_netherite_helmet:
      return 39;
    case I_leather_chestplate:
    case I_golden_chestplate:
    case I_iron_chestplate:
    case I_diamond_chestplate:
    case I_netherite_chestplate:
      return 38;
    case I_leather_leggings:
    case I_golden_leggings:
    case I_iron_leggings:
    case I_diamond_leggings:
    case I_netherite_leggings:
      return 37;
    case I_leather_boots:
    case I_golden_boots:
    case I_iron_boots:
    case I_diamond_boots:
    case I_netherite_boots:
      return 36;
    default: break;
  }

  return 255;
}

// Handles the player eating their currently held item
// Returns whether the operation was succesful (item was consumed)
// If `just_check` is set to true, the item doesn't get consumed
uint8_t handlePlayerEating (PlayerData *player, uint8_t just_check) {

  // Exit early if player is unable to eat
  if (commands_gamemode(player) == 1 || commands_gamemode(player) == 3 || player->hunger >= 20) return false;

  if (player->hotbar >= 41) return false;
  uint8_t *held_count = &player->inventory_count[player->hotbar];

  // Exit early if player isn't holding anything
  if (player->inventory_items[player->hotbar] == 0 || *held_count == 0) return false;

  uint8_t food = 0;
  uint16_t saturation = 0;

  // The saturation ratio from vanilla to here is about 1:500
  switch (player->inventory_items[player->hotbar]) {
    case I_chicken: food = 2; saturation = 600; break;
    case I_beef: food = 3; saturation = 900; break;
    case I_porkchop: food = 3; saturation = 300; break;
    case I_mutton: food = 2; saturation = 600; break;
    case I_cooked_chicken: food = 6; saturation = 3600; break;
    case I_cooked_beef: food = 8; saturation = 6400; break;
    case I_cooked_porkchop: food = 8; saturation = 6400; break;
    case I_cooked_mutton: food = 6; saturation = 4800; break;
    case I_rotten_flesh: food = 4; saturation = 0; break;
    case I_bread: food = 5; saturation = 3000; break;
    case I_apple: food = 4; saturation = 1200; break;
    default: break;
  }

  // If just checking the item, return before making any changes
  if (just_check) return food != 0;

  // Apply saturation and food boost
  player->saturation += saturation;
  player->hunger += food;
  if (player->hunger > 20) player->hunger = 20;

  // Consume held item
  *held_count -= 1;
  if (*held_count == 0) player->inventory_items[player->hotbar] = 0;

  // Update the client of these changes
  sc_entityEvent(player->client_fd, player->client_fd, 9);
  sc_setHealth(player->client_fd, player->health, player->hunger, player->saturation);
  sc_setContainerSlot(
    player->client_fd, 0,
    serverSlotToClientSlot(0, player->hotbar),
    *held_count, player->inventory_items[player->hotbar]
  );

  return true;
}

void handlePlayerAction (PlayerData *player, int action, short x, short y, short z) {

  if (!player || player->hotbar >= 41) return;
  if (action == 3 || action == 4) {
    items_drop_slot(player,player->hotbar,action == 3);
    return;
  }

  // "Finish eating" action, called any time eating stops
  if (action == 5) {
    // Reset eating timer and clear eating flag
    player->flagval_16 = 0;
    player->flags &= ~0x10;
  }

  if (commands_gamemode(player) >= 2) return;

  // Ignore further actions not pertaining to mining blocks
  if (action != 0 && action != 2) return;

  if (y < 0 || y > 255 || abs((int)x-player->x) > 6 ||
      abs((int)y-player->y) > 6 || abs((int)z-player->z) > 6) return;

  if (farming_harvest(player,x,y,z)) return;

  // In creative, only the "start mining" action is sent
  // No additional verification is performed, the block is simply removed
  if (action == 0 && commands_gamemode(player) == 1) {
    makeBlockChange(x, y, z, 0);
    return;
  }

  uint8_t block = getBlockAt(x, y, z);

  // If this is a "start mining" packet, the block must be instamine
  if (action == 0 && block == B_note_block) circuits_strike(x,y,z);
  if (action == 0 && !isInstantlyMined(player, block)) return;

  uint16_t held_item = player->inventory_items[player->hotbar];
  uint16_t item = circuits_drop(x,y,z,getMiningResult(held_item, block));
  /* At capacity, leave the block and tool intact rather than destroy its loot. */
  if (item && !items_can_spawn(item,1,x,y,z)) {
    sc_blockUpdate(player->client_fd,x,y,z,block);
    return;
  }
  if (makeBlockChange(x, (uint8_t)y, z, B_air)) return;
  bumpToolDurability(player);
  if (item) items_spawn(item,1,x,y,z,500);

  uint8_t block_above = getBlockAt(x, y + 1, z);

  // Check if any blocks above this should break, and if so,
  // iterate upward over all blocks in the column and break them
  for (int above_y = y+1; above_y <= 255 && isColumnBlock(block_above) && !circuits_wall_torch_at(x,above_y,z); above_y++) {
    uint16_t above_item = getMiningResult(0,block_above);
    if (above_item && !items_can_spawn(above_item,1,x,above_y,z)) break;
    if (makeBlockChange(x,(uint8_t)above_y,z,B_air)) break;
    if (above_item) items_spawn(above_item,1,x,above_y,z,500);
    block_above = above_y < 255 ? getBlockAt(x,(uint8_t)(above_y+1),z) : B_air;
  }
}

void handlePlayerUseItem (PlayerData *player, short x, short y, short z, uint8_t face) {
  if (commands_gamemode(player) == 3) return;

  if (player->hotbar >= 41) return;
  // Get targeted block (if coordinates are provided)
  uint8_t target = face == 255 ? 0 : getBlockAt(x, y, z);
  // Get held item properties
  uint8_t *count = &player->inventory_count[player->hotbar];
  // Access the packed inventory through its struct, never an unaligned uint16_t pointer.

  if (face != 255 && (y < 0 || y > 255 || abs((int)x-player->x) > 6 ||
      abs((int)y-player->y) > 6 || abs((int)z-player->z) > 6)) return;

  // Check interaction with containers when not sneaking
  if (!(player->flags & 0x04) && face != 255) {
    if (target == B_jukebox) { musicbox_menu(player,x,y,z); return; }
    if (target == B_lever || target == B_note_block || target == B_oak_trapdoor || target == B_iron_trapdoor) { circuits_interact(player,x,y,z); return; }
    if (target == B_oak_door) {
      doors_interact(player,x,y,z);
      return;
    } else if (target == B_oak_sign) {
      signs_interact(player,x,y,z);
      return;
    } else if (target == B_crafting_table) {
      if (!inventory_open(player,12)) return;
      sc_openScreen(player->client_fd, 12, "Crafting", 8);
      return;
    } else if (target == B_furnace) {
      if (!inventory_open(player,14)) return;
      sc_openScreen(player->client_fd, 14, "Furnace", 7);
      return;
    } else if (target == B_composter) {
      // Check if the player is holding anything
      if (*count == 0) return;
      // Check if the item is a valid compost item
      uint32_t compost_chance = isCompostItem(player->inventory_items[player->hotbar]);
      if (compost_chance != 0) {
        // Take away composted item
        if (commands_gamemode(player) != 1 && (*count -= 1) == 0) player->inventory_items[player->hotbar] = 0;
        sc_setContainerSlot(player->client_fd, 0, serverSlotToClientSlot(0, player->hotbar), *count, player->inventory_items[player->hotbar]);
        // Test compost chance and give bone meal on success
        if (fast_rand() < compost_chance) {
          givePlayerItem(player, I_bone_meal, 1);
        }
        return;
      }
    }
    #ifdef ALLOW_CHESTS
    else if (target == B_chest) {
      // Get a pointer to the entry following this chest in block_changes
      uint8_t *storage_ptr = NULL;
      for (int i = 0; i < block_changes_count; i ++) {
        if (block_changes[i].block != B_chest) continue;
        if (block_changes[i].x != x || block_changes[i].y != y || block_changes[i].z != z) continue;
        storage_ptr = (uint8_t *)(&block_changes[i + 1]);
        break;
      }
      if (storage_ptr == NULL || !inventory_open(player,2)) return;
      // Terrible memory hack!!
      // Copy the pointer into the player's crafting table item array.
      // This allows us to save some memory by repurposing a feature that
      // is mutually exclusive with chests, though it is otherwise a
      // terrible idea for obvious reasons.
      memcpy(player->craft_items, &storage_ptr, sizeof(storage_ptr));
      // Flag craft_items as locked due to holding a pointer
      player->flags |= 0x80;
      // Show the player the chest UI
      sc_openScreen(player->client_fd, 2, "Chest", 5);
      // Load the slots of the chest from the block_changes array.
      // This is a similarly dubious memcpy hack, but at least we're not
      // mixing data types? Kind of?
      for (int i = 0; i < 27; i ++) {
        uint16_t item;
        uint8_t count;
        memcpy(&item, storage_ptr + i * 3, 2);
        memcpy(&count, storage_ptr + i * 3 + 2, 1);
        sc_setContainerSlot(player->client_fd, 2, i, count, item);
      }
      return;
    }
    #endif
  }

  // If the selected slot doesn't hold any items, exit
  if (*count == 0) return;

  // Check special item handling
  if (player->inventory_items[player->hotbar] == I_bone_meal) {
    uint8_t target_below = getBlockAt(x, y - 1, z);
    if (target == B_oak_sapling) {
      // Consume the bone meal (yes, even before checks)
      // Wasting bone meal on misplanted saplings is vanilla behavior
      if (commands_gamemode(player) != 1 && (*count -= 1) == 0) player->inventory_items[player->hotbar] = 0;
      sc_setContainerSlot(player->client_fd, 0, serverSlotToClientSlot(0, player->hotbar), *count, player->inventory_items[player->hotbar]);
      if ( // Saplings can only grow when placed on these blocks
        target_below == B_dirt ||
        target_below == B_grass_block ||
        target_below == B_snowy_grass_block ||
        target_below == B_mud
      ) {
        // Bone meal has a 25% chance of growing a tree from a sapling
        if ((fast_rand() & 3) == 0) placeTreeStructure(x, y, z);
      }
    }
  } else if (handlePlayerEating(player, true)) {
    // Reset eating timer and set eating flag
    player->flagval_16 = 0;
    player->flags |= 0x10;
  } else if (getItemDefensePoints(player->inventory_items[player->hotbar]) != 0) {
    // For some reason, this action is sent twice when looking at a block
    // Ignore the variant that has coordinates
    if (face != 255) return;
    // Swap to held piece of armor
    uint8_t slot = getArmorItemSlot(player->inventory_items[player->hotbar]);
    uint16_t prev_item = player->inventory_items[slot];
    player->inventory_items[slot] = player->inventory_items[player->hotbar];
    player->inventory_count[slot] = 1;
    player->inventory_items[player->hotbar] = prev_item;
    player->inventory_count[player->hotbar] = 1;
    // Update client inventory
    sc_setContainerSlot(player->client_fd, -2, serverSlotToClientSlot(0, slot), 1, player->inventory_items[player->hotbar]);
    sc_setContainerSlot(player->client_fd, -2, serverSlotToClientSlot(0, player->hotbar), 1, prev_item);
    return;
  }

  if (commands_gamemode(player) == 2) return;

  // Don't proceed with block placement if no coordinates were provided
  if (face == 255) return;

  if (farming_use(player,x,y,z,face)) return;

  uint16_t circuit_item = player->inventory_items[player->hotbar];
  if (circuit_item == I_redstone || circuit_item == I_redstone_torch || circuit_item == I_lever || circuit_item == I_note_block || circuit_item == I_stone_pressure_plate || circuit_item == I_oak_pressure_plate || circuit_item == I_oak_trapdoor || circuit_item == I_iron_trapdoor) {
    if (circuits_place(player,x,y,z,face,circuit_item)) {
      if (commands_gamemode(player) != 1) *count -= 1;
      if (!*count) player->inventory_items[player->hotbar] = 0;
    }
    sc_setContainerSlot(player->client_fd,0,serverSlotToClientSlot(0,player->hotbar),*count,player->inventory_items[player->hotbar]);
    return;
  }

  if (player->inventory_items[player->hotbar] == I_oak_door) {
    if (doors_place(player,x,y,z,face)) {
      if (commands_gamemode(player) != 1) *count -= 1;
      if (*count == 0) player->inventory_items[player->hotbar] = 0;
    }
    sc_setContainerSlot(player->client_fd,0,serverSlotToClientSlot(0,player->hotbar),*count,player->inventory_items[player->hotbar]);
    return;
  }

  if (player->inventory_items[player->hotbar] == I_oak_sign) {
    if (signs_place(player,x,y,z,face)) {
      if (commands_gamemode(player) != 1) *count -= 1;
      if (*count == 0) player->inventory_items[player->hotbar] = 0;
    }
    sc_setContainerSlot(player->client_fd,0,serverSlotToClientSlot(0,player->hotbar),*count,player->inventory_items[player->hotbar]);
    return;
  }

  // If the selected item doesn't correspond to a block, exit
  uint8_t block = I_to_B(player->inventory_items[player->hotbar]);
  if (block == 0) return;

  // Check adjacent coordinates before narrowing back into the compact save types.
  if ((face == 0 && y == 0) || (face == 1 && y == 255) ||
      (face == 2 && z == -32768) || (face == 3 && z == 32767) ||
      (face == 4 && x == -32768) || (face == 5 && x == 32767)) return;

  switch (face) {
    case 0: y -= 1; break;
    case 1: y += 1; break;
    case 2: z -= 1; break;
    case 3: z += 1; break;
    case 4: x -= 1; break;
    case 5: x += 1; break;
    default: break;
  }

  // Check if the block's placement conditions are met
  if (
    !( // Is player in the way?
      !isPassableBlock(block) &&
      x == player->x &&
      (y == player->y || y == player->y + 1) &&
      z == player->z
    ) &&
    isReplaceableBlock(getBlockAt(x, y, z)) &&
    (!isColumnBlock(block) || getBlockAt(x, y - 1, z) != B_air)
  ) {
    // Apply server-side block change
    if (makeBlockChange(x, y, z, block)) return;
    // Decrease item amount in selected slot
    if (commands_gamemode(player) != 1) *count -= 1;
    // Clear item id in slot if amount is zero
    if (*count == 0) player->inventory_items[player->hotbar] = 0;
  }

  // Sync hotbar contents to player
  sc_setContainerSlot(player->client_fd, 0, serverSlotToClientSlot(0, player->hotbar), *count, player->inventory_items[player->hotbar]);

}

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
      uint8_t player_name_len = strlen(player->name);
      strcpy((char *)recv_buffer, player->name);

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
        strcpy((char *)recv_buffer + player_name_len + 14, attacker->name);
        recv_buffer[player_name_len + 14 + strlen(attacker->name)] = '\0';
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

// Simulates events scheduled for regular intervals
// Takes the time since the last tick in microseconds as the only arguemnt
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

#ifdef ALLOW_CHESTS
// Broadcasts a chest slot update to all clients who have that chest open,
// except for the client who initiated the update.
void broadcastChestUpdate (int origin_fd, uint8_t *storage_ptr, uint16_t item, uint8_t count, uint8_t slot) {
  (void)origin_fd;

  for (int i = 0; i < MAX_PLAYERS; i ++) if (plates_player_active(&player_data[i])) {
    if (player_data[i].client_fd == -1) continue;
    if (player_data[i].flags & 0x20) continue;
    // Filter for players that have this chest open
    if (memcmp(player_data[i].craft_items, &storage_ptr, sizeof(storage_ptr)) != 0) continue;
    // Send slot update packet
    sc_setContainerSlot(player_data[i].client_fd, 2, slot, count, item);
  }

  #ifndef DISK_SYNC_BLOCKS_ON_INTERVAL
  writeChestChangesToDisk(storage_ptr, slot);
  #endif

}
#endif

ssize_t writeEntityData (int client_fd, EntityData *data) {
  writeByte(client_fd, data->index);
  writeVarInt(client_fd, data->type);

  switch (data->type) {
    case 0: // Byte
      return writeByte(client_fd, data->value.byte);
    case 21: // Pose
      writeVarInt(client_fd, data->value.pose);
      return 0;

    default: return -1;
  }
}

// Returns the networked size of an EntityData entry
int sizeEntityData (EntityData *data) {
  int value_size;

  switch (data->type) {
    case 0: // Byte
      value_size = 1;
      break;
    case 21: // Pose
      value_size = sizeVarInt(data->value.pose);
      break;

    default: return -1;
  }

  return 1 + sizeVarInt(data->type) + value_size;
}

// Returns the networked size of an array of EntityData entries
int sizeEntityMetadata (EntityData *metadata, size_t length) {
  int total_size = 0;
  for (size_t i = 0; i < length; i ++) {
    int size = sizeEntityData(&metadata[i]);
    if (size == -1) return -1;
    total_size += size;
  }
  return total_size;
}
