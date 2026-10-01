/* Bounded protocol 772 Click Container parser. Drop requests use server-owned
 * stacks; normal clicks retain the inherited inventory/crafting behavior. */
#include <string.h>
#include "items.h"
#include "commands.h"
#include "crafting.h"
#include "packets.h"
#include "procedures.h"
#include "registry.h"
#include "registries.h"
#include "tools.h"
#include "varnum.h"

typedef struct { const uint8_t *data; size_t size, at; } Reader;
typedef struct { uint16_t item; uint8_t count; } Stack;
typedef struct { uint16_t slot; Stack stack; } Change;
static bool integer (Reader *r, uint32_t *out) {
  uint32_t value = 0;
  for (unsigned shift = 0; shift < 35; shift += 7) {
    if (r->at == r->size) return false;
    uint8_t b = r->data[r->at++]; if (shift == 28 && (b&0xf8u)) return false;
    value |= (uint32_t)(b&127u)<<shift;
    if (!(b&128u)) { *out = value; return true; }
  }
  return false;
}
static bool short_be (Reader *r, uint16_t *out) {
  if (r->size-r->at < 2) return false;
  *out = (uint16_t)((unsigned)r->data[r->at]*256+r->data[r->at+1]); r->at += 2; return true;
}
static bool stack (Reader *r, Stack *out) {
  if (r->at == r->size) return false;
  uint8_t present = r->data[r->at++]; *out = (Stack){0,0};
  if (!present) return true;
  uint32_t item,count,n,id;
  if (present != 1 || !integer(r,&item) || !item || !registry_item_id_valid(item) ||
      !integer(r,&count) || !count || count > getItemStackSize((uint16_t)item)) return false;
  /* HashedSlot has component-count arrays, not byte-length-prefixed blobs. */
  if (!integer(r,&n) || n > 64) return false;
  for (uint32_t i = 0; i < n; i++) {
    if (!integer(r,&id) || r->size-r->at < 4) return false;
    r->at += 4;
  }
  if (!integer(r,&n) || n > 64) return false;
  for (uint32_t i = 0; i < n; i++) if (!integer(r,&id)) return false;
  *out = (Stack){(uint16_t)item,(uint8_t)count}; return true;
}
static uint8_t *chest_storage (PlayerData *p) {
  if (!(p->flags&0x80)) return NULL;
  uint8_t *candidate = NULL; memcpy(&candidate,p->craft_items,sizeof(candidate));
  /* Never trust a persisted/overwritten pointer as an address to dereference. */
  for (int i = 0; i+14 < block_changes_count && i+14 < MAX_BLOCK_CHANGES; i++) {
    if (block_changes[i].block != B_chest) continue;
    if (candidate == (uint8_t *)&block_changes[i+1]) return candidate;
    i += 14;
  }
  return NULL;
}
static int mapped (int window, uint16_t wire) { return wire > 255 ? 255 : clientSlotToServerSlot(window,(uint8_t)wire); }
static bool accessible (PlayerData *p, int window, uint16_t wire) {
  int slot = mapped(window,wire);
  if (slot == 255) return false;
  if (slot < 41) return true;
  return window == 2 ? slot < 68 && chest_storage(p) != NULL : slot < 50 && !(p->flags&0x80);
}
static Stack read_slot (PlayerData *p, int window, uint16_t wire) {
  int slot = mapped(window,wire);
  if (slot < 41) return (Stack){p->inventory_items[slot],p->inventory_count[slot]};
  if (window != 2) return (Stack){p->craft_items[slot-41],p->craft_count[slot-41]};
  uint8_t *ptr = chest_storage(p)+(slot-41)*3; Stack s;
  memcpy(&s.item,ptr,2); s.count = ptr[2]; return s;
}
static void put_slot (PlayerData *p, int window, uint16_t wire, Stack s) {
  int slot = mapped(window,wire);
  if (slot < 41) { p->inventory_items[slot] = s.item; p->inventory_count[slot] = s.count; }
  else if (window != 2) { p->craft_items[slot-41] = s.item; p->craft_count[slot-41] = s.count; }
  else {
    uint8_t *storage = chest_storage(p), *ptr = storage+(slot-41)*3;
    memcpy(ptr,&s.item,2); ptr[2] = s.count;
    broadcastChestUpdate(p->client_fd,storage,s.item,s.count,(uint8_t)(slot-41));
  }
}
static void sync_slot (PlayerData *p, int window, uint16_t wire) {
  if (!accessible(p,window,wire)) return;
  Stack s = read_slot(p,window,wire); sc_setContainerSlot(p->client_fd,window,wire,s.count,s.item);
}
int cs_clickContainer (int fd, int length) {
  /* Packet handling is single-threaded; keep this buffer off embedded stacks. */
  static uint8_t payload[4096];
  if (length <= 0 || (size_t)length > sizeof(payload) || recv_all(fd,payload,(size_t)length,false) != length) return 1;
  Reader r = {payload,(size_t)length,0}; uint32_t window,state,mode,n; uint16_t clicked;
  if (!integer(&r,&window) || (window != 0 && window != 2 && window != 12 && window != 14) ||
      !integer(&r,&state) || !short_be(&r,&clicked) || r.at == r.size) return 1;
  uint8_t button = payload[r.at++];
  if (!integer(&r,&mode) || mode > 6 || !integer(&r,&n) || n > 64) return 1;
  if ((mode <= 1 && button > 1) || (mode == 2 && button > 8 && button != 40) ||
      (mode == 3 && button != 2) || (mode == 4 && button > 1) ||
      (mode == 5 && (button > 10 || button%4 == 3)) || (mode == 6 && button != 0)) return 1;
  Change changes[64];
  for (uint32_t i = 0; i < n; i++) {
    if (!short_be(&r,&changes[i].slot) || !stack(&r,&changes[i].stack)) return 1;
    for (uint32_t j = 0; j < i; j++) if (changes[i].slot == changes[j].slot) return 1;
  }
  Stack cursor; if (!stack(&r,&cursor) || r.at != r.size) return 1;
  PlayerData *p; if (getPlayerData(fd,&p)) return 1;
  int w = (int)window;
  bool output = (w == 0 || w == 12) && clicked == 0;
  if (clicked != (uint16_t)64537 && !output && !accessible(p,w,clicked)) return 1; /* -999 */
  for (uint32_t i = 0; i < n; i++) if (!((w == 0 || w == 12) && changes[i].slot == 0) && !accessible(p,w,changes[i].slot)) return 1;
  if ((p->flags&0x33) || !p->health || commands_gamemode(p) == 3) return 0;

  if (output) {
    uint8_t result_count; uint16_t result_item;
    getCraftingOutput(p,&result_count,&result_item);
    if (!result_count || result_item == I_wheat_seeds || result_item == I_bread ||
        result_item == I_wooden_hoe || result_item == I_stone_hoe || result_item == I_iron_hoe || result_item == I_golden_hoe ||
        result_item == I_diamond_hoe || result_item == I_netherite_hoe || result_item == I_stone_pressure_plate ||
        result_item == I_oak_pressure_plate || result_item == I_oak_trapdoor || result_item == I_iron_trapdoor) {
      /* Consume verified ingredients; never accept predicted output stacks. */
      if (result_count && mode == 0 && (!p->flagval_8 || p->flagval_16 == result_item) &&
          (unsigned)p->flagval_8+result_count <= getItemStackSize(result_item)) {
        bool valid = true;
        if (w == 0) for (unsigned i = 0; i < 9; i++)
          if (i != 0 && i != 1 && i != 3 && i != 4 && p->craft_items[i]) valid = false;
        for (uint16_t wire = 1; wire <= (w == 0 ? 4 : 9); wire++) {
          Stack ingredient = read_slot(p,w,wire);
          if (ingredient.item && !ingredient.count) valid = false;
        }
        if (valid) {
          for (uint16_t wire = 1; wire <= (w == 0 ? 4 : 9); wire++) {
            Stack ingredient = read_slot(p,w,wire); if (!ingredient.item) continue;
            ingredient.count--; if (!ingredient.count) ingredient.item = 0;
            put_slot(p,w,wire,ingredient); sync_slot(p,w,wire);
          }
          p->flagval_16 = result_item; p->flagval_8 = (uint8_t)(p->flagval_8+result_count);
        }
      }
      for (uint32_t i = 0; i < n; i++) sync_slot(p,w,changes[i].slot);
      getCraftingOutput(p,&result_count,&result_item);
      sc_setContainerSlot(fd,w,0,result_count,result_item);
      sc_setCursorItem(fd,p->flagval_16,p->flagval_8);
      return 0;
    }
  }

  if (mode == 4 || (mode == 0 && clicked == 64537)) {
    if (button > 1) return 1;
    if (mode == 4 && accessible(p,w,clicked)) {
      Stack s = read_slot(p,w,clicked);
      uint8_t amount = button ? s.count : (s.count ? 1 : 0);
      if (items_drop_stack(p,s.item,amount)) {
        s.count = (uint8_t)(s.count-amount); if (!s.count) s.item = 0;
        put_slot(p,w,clicked,s);
      }
      sync_slot(p,w,clicked);
    } else if (mode == 0) {
      uint8_t amount = button ? (p->flagval_8 ? 1 : 0) : p->flagval_8;
      if (items_drop_stack(p,p->flagval_16,amount)) {
        p->flagval_8 = (uint8_t)(p->flagval_8-amount); if (!p->flagval_8) p->flagval_16 = 0;
      }
    }
    /* Client predictions never overwrite authoritative stacks after a drop. */
    for (uint32_t i = 0; i < n; i++) sync_slot(p,w,changes[i].slot);
    sc_setCursorItem(fd,p->flagval_16,p->flagval_8);
    return 0;
  }
  bool craft = false;
  for (uint32_t i = 0; i < n; i++) {
    if ((w == 0 || w == 12) && changes[i].slot == 0) continue;
    if (mapped(w,changes[i].slot) >= 41) craft = true;
    put_slot(p,w,changes[i].slot,changes[i].stack);
  }
  if (craft && (w == 0 || w == 12)) {
    uint8_t count; uint16_t item; getCraftingOutput(p,&count,&item); sc_setContainerSlot(fd,w,0,count,item);
  } else if (w == 14) {
    getSmeltingOutput(p);
    for (uint16_t i = 0; i < 3; i++) sc_setContainerSlot(fd,w,i,p->craft_count[i],p->craft_items[i]);
  }
  p->flagval_16 = cursor.item; p->flagval_8 = cursor.count;
  return 0;
}
