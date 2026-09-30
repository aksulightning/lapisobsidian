#include <stddef.h>
#include "registry.h"
#include "registries.h"
#include "protocol.h"

_Static_assert(LAPIS_PROTOCOL_VERSION == REGISTRY_PROTOCOL_VERSION,
  "Update the registry snapshot together with the protocol");
_Static_assert(sizeof(block_palette) / sizeof(block_palette[0]) == 256,
  "Chunk serialization requires the 8-bit palette");

bool registry_block_id_valid (uint32_t id) {
  return id < 256;
}

bool registry_block_state (uint32_t id, uint16_t *state) {
  if (!state || !registry_block_id_valid(id)) return false;
  *state = block_palette[id];
  return true;
}

bool registry_item_id_valid (uint32_t id) {
  return id <= REGISTRY_ITEM_MAX_ID;
}

const char *registry_biome_by_id (uint32_t id) {
  static const char *const names[] = {
    "plains", "mangrove_swamp", "desert", "snowy_plains", "beach"
  };
  return id < sizeof(names) / sizeof(names[0]) ? names[id] : NULL;
}

const char *registry_dimension_by_id (uint32_t id) {
  return id == 0 ? "overworld" : NULL;
}

bool registry_validate (void) {
  size_t offset = 0;
  for (uint32_t id = 0; id < 256; id ++) {
    uint32_t state = 0;
    unsigned shift = 0;
    uint8_t byte;
    do {
      if (offset >= sizeof(network_block_palette) || shift >= 21) return false;
      byte = network_block_palette[offset ++];
      state |= (uint32_t)(byte & 127u) << shift;
      shift += 7;
    } while (byte & 128u);
    if (state != block_palette[id] || !registry_item_id_valid(registry_block_item(id))) return false;
  }
  return offset == sizeof(network_block_palette) && block_palette[B_air] == 0;
}
