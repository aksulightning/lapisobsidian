/* Private state shared by item simulation and its persistent file codec. */
#ifndef H_ITEMS_INTERNAL
#define H_ITEMS_INTERNAL
#include "items.h"
typedef struct {
  DroppedItem items[ITEM_ENTITY_LIMIT];
  uint64_t next_id;
  char path[128];
} ItemsContext;
/* Borrow the selected world's state; do not retain across a Plate switch. */
ItemsContext *items_context(void);
bool items_valid(uint16_t item, uint8_t count, int x, int y, int z);
#endif
