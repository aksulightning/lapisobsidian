/* Apply block and item interactions from the selected hotbar slot. */
#include <stdlib.h>
#include <string.h>
#include "procedures.h"
#include "packets.h"
#include "tools.h"
#include "plates.h"
#include "registries.h"
#include "commands.h"
#include "worldgen.h"
#include "structures.h"
#include "items.h"
#include "inventory.h"
#include "doors.h"
#include "farming.h"
#include "circuits.h"
#include "musicbox.h"
#include "signs.h"

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
