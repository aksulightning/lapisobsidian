#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include "registry.h"
#include "registries.h"
#include "protocol.h"

int main (void) {
  uint16_t state = 0xffff;
  assert(LAPIS_PROTOCOL_VERSION == 772);
  assert(REGISTRY_PROTOCOL_VERSION == LAPIS_PROTOCOL_VERSION);
  assert(registry_validate());
  assert(registry_block_state(255, &state));
  assert(!registry_block_state(256, &state));
  assert(!registry_block_state(UINT32_MAX, &state));
  assert(!registry_block_state(B_air, NULL));
  const uint32_t required[] = { B_air, B_stone, B_grass_block, B_dirt,
    B_cobblestone, B_bedrock, B_water, B_lava, B_sand, B_gravel,
    B_oak_log, B_oak_leaves, B_glass, B_obsidian, B_lapis_ore };
  for (size_t i = 0; i < sizeof(required) / sizeof(required[0]); i ++) {
    assert(registry_block_state(required[i], &state));
  }
  assert(registry_block_state(B_stone, &state) && state == 1);
  assert(registry_block_state(B_bedrock, &state) && state == 85);
  assert(registry_item_id_valid(I_diamond_sword));
  assert(registry_item_id_valid(I_wooden_pickaxe));
  assert(registry_item_id_valid(I_oak_sign));
  assert(registry_item_id_valid(REGISTRY_ITEM_MAX_ID));
  assert(!registry_item_id_valid(REGISTRY_ITEM_MAX_ID + 1));
  assert(!registry_item_id_valid(UINT32_MAX));
  assert(I_to_B(UINT32_MAX) == B_air);
  assert(I_to_B(I_obsidian) == B_obsidian);
  assert(registry_block_item(B_stone) == I_cobblestone);
  assert(registry_block_item(UINT32_MAX) == 0);
  assert(!strcmp(registry_biome_by_id(W_plains), "plains"));
  assert(!strcmp(registry_biome_by_id(W_beach), "beach"));
  assert(registry_biome_by_id(5) == NULL);
  assert(registry_biome_by_id(UINT32_MAX) == NULL);
  assert(!strcmp(registry_dimension_by_id(0), "overworld"));
  assert(registry_dimension_by_id(1) == NULL);
  assert(registry_dimension_by_id(UINT32_MAX) == NULL);
  puts("registry: passed");
  return 0;
}
