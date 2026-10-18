/* Block collision, replacement and compost rules. */
#include "procedures.h"
#include "packets.h"
#include "tools.h"
#include "plates.h"
#include "registries.h"

/* Shared small-plant category for instant breaking, support and collision. */
static uint8_t isSmallPlant (uint8_t block) {
  return (block >= B_dandelion && block <= B_red_mushroom) || block == B_fern;
}

/* Whether the start-digging action can complete immediately with the held tool;
 * loot eligibility is a separate decision in getMiningResult. */
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

/* Blocks that require support for placement and collapse after support removal.
 * This compact rule includes sand but does not simulate falling-block entities. */
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
/* Spawn clearance excludes liquids even though entities can pass through them. */
uint8_t isPassableSpawnBlock (uint8_t block) {
    if ((block >= B_water && block < B_water + 8) ||
        (block >= B_lava && block < B_lava + 4))
    {
        return 0;
    }
    return isPassableBlock(block);
}

/* Placement replacement is narrower than collision passability: signs and
 * torches, for example, must be broken rather than overwritten. */
uint8_t isReplaceableBlock (uint8_t block) {
  return (
    block == B_air ||
    (block >= B_water && block < B_water + 8) ||
    (block >= B_lava && block < B_lava + 4) ||
    block == B_short_grass ||
    block == B_snow
  );
}

/* Approximate seven composting levels with one bone-meal roll, avoiding stored
 * fill state. Returns a threshold out of 2^32, or zero for unsupported items. */
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
