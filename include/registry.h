#ifndef H_LAPIS_REGISTRY
#define H_LAPIS_REGISTRY

#include <stdbool.h>
#include <stdint.h>

/* Block IDs are compact server palette indices, not network state IDs.
 * Item IDs are protocol IDs; valid does not imply implemented gameplay. */
bool registry_block_id_valid (uint32_t id);
bool registry_block_state (uint32_t id, uint16_t *state);
bool registry_item_id_valid (uint32_t id);
const char *registry_biome_by_id (uint32_t id);
const char *registry_dimension_by_id (uint32_t id);
bool registry_validate (void);

#endif
