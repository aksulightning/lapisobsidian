/* Player slots, connection states and join/disconnect lifecycle. */
#include "procedures.h"
#include "packets.h"
#include "tools.h"
#include "plates.h"
#include "registries.h"
#include "auth.h"
#include "webclient.h"
#include "inventory.h"
#include "items.h"
#include "mobs.h"
#include "signs.h"
#include "commands.h"
#include "serialize.h"
#include <stdio.h>
#include <string.h>
#include <errno.h>
#ifdef _WIN32
#include <winsock2.h>
#endif

/* Interleaved (descriptor, protocol state) pairs; descriptor -1 marks a vacancy. */
int client_states[MAX_PLAYERS * 2];

/* Update a known descriptor or claim a vacancy; a full table is left unchanged. */
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

/* Unknown descriptors share STATE_NONE with clients awaiting a handshake. */
int getClientState (int client_fd) {
  for (int i = 0; i < MAX_PLAYERS * 2; i += 2) {
    if (client_states[i] != client_fd) continue;
    return client_states[i + 1];
  }
  return STATE_NONE;
}

/* Returns the even array offset, not a player_data index; -1 means absent. */
int getClientIndex (int client_fd) {
  for (int i = 0; i < MAX_PLAYERS * 2; i += 2) {
    if (client_states[i] != client_fd) continue;
    return i;
  }
  return -1;
}

/* Reset survival/inventory state and request terrain-based spawning (flag 0x02).
 * Identity and connection ownership remain attached to this player slot. */
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

/* Bind the login identity to a saved/new slot and mark it loading. Existing
 * inventory and health survive reconnects. Identity validation belongs to the
 * login path; returns 0 on reservation, 1 when player capacity is exhausted. */
int reservePlayerData (int client_fd, uint8_t *uuid, char *name) {

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

/* Lookup is scoped to the selected Plate; failure leaves output untouched. */
int getPlayerData (int client_fd, PlayerData **output) {
  for (int i = 0; i < MAX_PLAYERS; i ++) {
    if (player_data[i].client_fd == client_fd && plates_player_active(&player_data[i])) {
      *output = &player_data[i];
      return 0;
    }
  }
  return 1;
}

/* Match a command-buffer token against connected names in this Plate, or NULL.
 * Callers supply valid bounds and at most 16 name bytes. This legacy lookup
 * compares only the supplied token, not the saved name length. */
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

/* Release connection/window state but retain the UUID-backed save slot. */
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
    uint8_t player_name_len = player_name_length(player_data[i].name);
    memcpy(recv_buffer, player_data[i].name, player_name_len);
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

/* Publish the player's entity, clear loading state and synchronize mobs/drops.
 * Called by the loaded packet or the tick handler's fallback loading timeout. */
void handlePlayerJoin (PlayerData* player) {

  // Prepare join message for broadcast
  uint8_t player_name_len = player_name_length(player->name);
  memcpy(recv_buffer, player->name, player_name_len);
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

/* Invalidate the caller's descriptor after cleanup. Virtual transports own
 * their IDs, so only unclaimed descriptors may be passed to the OS close call. */
void disconnectClient (int *client_fd, int cause) {
  if (*client_fd == -1) return;
  client_count --;
  setClientState(*client_fd, STATE_NONE);
  handlePlayerDisconnect(*client_fd);
  auth_forget(*client_fd);
  int transport_owned = 0;
  #if (defined(LAPIS_ENABLE_WEBCLIENT) && LAPIS_ENABLE_WEBCLIENT == 1) || defined(LAPIS_SINGLEPLAYER)
  transport_owned = webclient_disconnect(*client_fd);
  #endif
  #ifdef _WIN32
  if (!transport_owned) closesocket(*client_fd);
  printf("Disconnected client %d, cause: %d, errno: %d\n", *client_fd, cause, WSAGetLastError());
  #else
  if (!transport_owned) close(*client_fd);
  printf("Disconnected client %d, cause: %d, errno: %d\n\n", *client_fd, cause, errno);
  #endif
  *client_fd = -1;
}
