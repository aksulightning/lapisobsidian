/* Inventory slot translation, insertion and chest synchronization. */
#include "procedures.h"
#include "packets.h"
#include "tools.h"
#include "plates.h"
#include "registries.h"
#include "serialize.h"
#include <string.h>

/* Translate compact server slots to protocol window slots; 255 is unmapped. */
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

/* Values >= 41 identify window-specific storage, not ordinary inventory
 * indices. Callers must resolve crafting/furnace/chest storage before access. */
uint8_t clientSlotToServerSlot (int window_id, uint8_t slot) {

  if (window_id == 0) { // player inventory

    if (slot >= 36 && slot <= 44) return slot - 36;
    if (slot >= 9 && slot <= 35) return slot;
    if (slot == 45) return 40;
    if (slot >= 5 && slot <= 8) return 44 - slot;

    // Embed the player's 2x2 grid in the top-left cells of the shared 3x3 grid.
    if (slot == 1) return 41;
    if (slot == 2) return 42;
    if (slot == 3) return 44;
    if (slot == 4) return 45;

  } else if (window_id == 12) { // crafting table

    // Window-local storage uses the synthetic range starting at 41.
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

    // The chest window resolves these synthetic slots to packed chest storage.
    if (slot <= 26) return 41 + slot;
    // the rest of the slots are identical, just shifted by 18
    if (slot >= 27 && slot <= 62) return clientSlotToServerSlot(0, slot - 18);

  }
  #endif

  return 255;
}

/* Insert a valid stack into one main-inventory slot; never split it across
 * slots. Returns 0 on success/no-op, 1 without mutation if the selected slot
 * falls outside the main inventory or no slot is available. */
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

#ifdef ALLOW_CHESTS
/* Reconcile every viewer of this chest, including the initiating client,
 * with the authoritative slot; persist immediately unless interval sync owns it. */
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
