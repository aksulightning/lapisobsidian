#ifndef H_LAPIS_ITEMS
#define H_LAPIS_ITEMS
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "globals.h"

#define ITEM_ENTITY_LIMIT 128
#define ITEM_ENTITY_BASE (-1024)
#define ITEM_LIFETIME_MS 300000u

typedef struct {
  float y, velocity;
  uint32_t age_ms, delay_ms, viewers;
  int16_t x, z;
  uint16_t item;
  uint8_t count;
} DroppedItem;

/* A zero count is an unused slot. Coordinates are server block coordinates. */
const DroppedItem *items_at (size_t index);
void items_clear (void);
bool items_can_spawn (uint16_t item, uint8_t count, int x, int y, int z);
bool items_can_spawn_pair (uint16_t a, uint8_t ac, uint16_t b, uint8_t bc, int x, int y, int z);
bool items_spawn (uint16_t item, uint8_t count, int x, int y, int z, uint32_t delay_ms);
bool items_drop_slot (PlayerData *player, uint8_t slot, bool whole_stack);
bool items_drop_stack (PlayerData *player, uint16_t item, uint8_t count);
uint8_t items_insert (PlayerData *player, uint16_t item, uint8_t count);
void items_tick (int64_t elapsed_us);
void items_sync_player (PlayerData *player);
void items_forget_player (PlayerData *player);
#endif
