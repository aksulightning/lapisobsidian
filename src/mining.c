/* Mining rewards, tool wear and player digging actions. */
#include <stdlib.h>
#include "circuits.h"
#include "procedures.h"
#include "packets.h"
#include "tools.h"
#include "plates.h"
#include "registries.h"
#include "commands.h"
#include "items.h"
#include "worldgen.h"
#include "doors.h"
#include "farming.h"
#include "inventory.h"

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
