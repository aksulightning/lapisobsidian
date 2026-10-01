/* Protocol 772 predictions are parsed for framing only. All mutations below
 * are derived from the requested action and server-owned stacks. */
#include <string.h>
#include "items.h"
#include "inventory.h"
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
typedef struct { uint64_t drag_slots; uint16_t drag_item; uint8_t window,drag,drag_count; } Session;
static Session sessions[MAX_PLAYERS];
static Session *session (const PlayerData *p) {
  for (unsigned i = 0; i < MAX_PLAYERS; i++) if (p == &player_data[i]) return &sessions[i];
  return NULL;
}
void inventory_reset (PlayerData *p) { Session *s = session(p); if (s) memset(s,0,sizeof(*s)); }
uint8_t inventory_window (const PlayerData *p) { const Session *s = session(p); return s ? s->window : 0; }
static Stack cursor_stack (const PlayerData *p) { return (Stack){p->flagval_8 ? p->flagval_16 : 0,p->flagval_8}; }
static void set_cursor (PlayerData *p, Stack c) { p->flagval_16 = c.count ? c.item : 0; p->flagval_8 = c.count; }
static bool return_stack (PlayerData *p, Stack *s) {
  if (!s->count || !s->item) { *s = (Stack){0,0}; return true; }
  s->count = (uint8_t)(s->count-items_insert(p,s->item,s->count));
  if (s->count && items_drop_stack(p,s->item,s->count)) s->count = 0;
  if (!s->count) s->item = 0;
  return !s->count;
}
bool inventory_close (PlayerData *p) {
  Session *s = session(p); if (!s) return false;
  bool ok = true;
  for (unsigned i = 0; i < 9; i++) {
    Stack slot = {p->craft_items[i],p->craft_count[i]};
    if (p->flags&0x80) slot = (Stack){0,0}; /* Never interpret a chest pointer as items. */
    else if (!return_stack(p,&slot)) ok = false;
    p->craft_items[i] = slot.item; p->craft_count[i] = slot.count;
  }
  p->flags &= (uint8_t)~0x80u;
  if (!(p->flags&0x33)) {
    Stack c = cursor_stack(p); if (!return_stack(p,&c)) ok = false; set_cursor(p,c);
    sc_setCursorItem(p->client_fd,c.item,c.count);
  }
  memset(s,0,sizeof(*s)); return ok;
}
bool inventory_open (PlayerData *p, uint8_t window) {
  if (!p || (p->flags&0x33) || !p->health || commands_gamemode(p) == 3 ||
      (window != 2 && window != 12 && window != 14) || !inventory_close(p)) return false;
  session(p)->window = window; return true;
}
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
  if (!s.count) s.item = 0;
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
static unsigned end_slot (int w) { return w == 0 ? 46u : w == 12 ? 46u : w == 14 ? 39u : 63u; }
static bool output_slot (int w, uint16_t wire) { return ((w == 0 || w == 12) && !wire) || (w == 14 && wire == 2); }
static bool accepts (int w, uint16_t wire, Stack s) {
  if (!s.count) return true;
  if (output_slot(w,wire)) return false;
  if (w == 0 && wire >= 5 && wire <= 8) return s.count == 1 && getArmorItemSlot(s.item) == mapped(w,wire);
  return true;
}
static void sync_window (PlayerData *p, int w, const Stack *before, const Change *predictions, uint32_t n) {
  for (uint16_t wire = 0; wire < end_slot(w); wire++) {
    if (!accessible(p,w,wire)) continue;
    Stack now = read_slot(p,w,wire);
    bool changed = now.item != before[wire].item || now.count != before[wire].count;
    for (uint32_t i = 0; i < n; i++) if (predictions[i].slot == wire) changed = true;
    if (changed) sync_slot(p,w,wire);
  }
  if (w == 0 || w == 12) {
    uint8_t count; uint16_t item; getCraftingOutput(p,&count,&item); sc_setContainerSlot(p->client_fd,w,0,count,item);
  }
  Stack c = cursor_stack(p); sc_setCursorItem(p->client_fd,c.item,c.count);
}
static void ordinary_click (PlayerData *p, int w, uint16_t wire, uint8_t button) {
  Stack slot = read_slot(p,w,wire), c = cursor_stack(p);
  if (!slot.count) slot.item = 0;
  if (!c.count) {
    unsigned amount = button ? ((unsigned)slot.count+1)/2 : slot.count;
    c = (Stack){slot.item,(uint8_t)amount}; slot.count = (uint8_t)(slot.count-amount);
  } else if (output_slot(w,wire)) {
    if (slot.item == c.item && (unsigned)c.count+slot.count <= getItemStackSize(c.item)) { c.count = (uint8_t)(c.count+slot.count); slot.count = 0; }
  } else if (!slot.count || slot.item == c.item) {
    unsigned limit = (w == 0 && wire >= 5 && wire <= 8) ? 1u : getItemStackSize(c.item);
    unsigned amount = button ? 1u : c.count;
    if (slot.count > limit) return;
    if (amount > limit-slot.count) amount = limit-slot.count;
    Stack next = {c.item,(uint8_t)(slot.count+amount)};
    if (accepts(w,wire,next)) { slot = next; c.count = (uint8_t)(c.count-amount); }
  } else if (accepts(w,wire,c)) { Stack tmp = slot; slot = c; c = tmp; }
  put_slot(p,w,wire,slot); set_cursor(p,c);
}
static void transfer (PlayerData *p, int w, Stack *moving, unsigned first, unsigned end) {
  for (unsigned pass = 0; pass < 2 && moving->count; pass++) for (unsigned wire = first; wire < end && moving->count; wire++) {
    if (!accessible(p,w,(uint16_t)wire) || output_slot(w,(uint16_t)wire)) continue;
    Stack dst = read_slot(p,w,(uint16_t)wire);
    if ((pass == 0 && (!dst.count || dst.item != moving->item)) || (pass == 1 && dst.count)) continue;
    unsigned limit = getItemStackSize(moving->item);
    if (dst.count >= limit) continue;
    unsigned amount = limit-dst.count; if (amount > moving->count) amount = moving->count;
    Stack next = {moving->item,(uint8_t)(dst.count+amount)};
    if (!accepts(w,(uint16_t)wire,next)) continue;
    put_slot(p,w,(uint16_t)wire,next); moving->count = (uint8_t)(moving->count-amount);
  }
}
static void quick_move (PlayerData *p, int w, uint16_t wire) {
  Stack slot = read_slot(p,w,wire); if (!slot.count) return;
  if (w == 0) {
    if (wire >= 9 && wire < 36) transfer(p,w,&slot,36,45);
    else transfer(p,w,&slot,9,36);
  } else {
    unsigned first = w == 2 ? 27u : w == 12 ? 10u : 3u;
    if (wire < first) transfer(p,w,&slot,first,end_slot(w));
    else if (w == 2) transfer(p,w,&slot,0,27);
    else if (w == 14) transfer(p,w,&slot,0,2);
    else if (wire < 37) transfer(p,w,&slot,37,46);
    else transfer(p,w,&slot,10,37);
  }
  put_slot(p,w,wire,slot);
}
static bool craft_once (PlayerData *p, int w, bool shift) {
  uint8_t count; uint16_t item; getCraftingOutput(p,&count,&item);
  if (!count || !item || (p->flags&0x80)) return false;
  if (w == 0) for (unsigned i = 0; i < 9; i++) if (i != 0 && i != 1 && i != 3 && i != 4 && p->craft_items[i]) return false;
  for (unsigned i = 0; i < 9; i++) if (p->craft_items[i] && !p->craft_count[i]) return false;
  Stack c = cursor_stack(p);
  if (shift) {
    unsigned space = 0;
    for (unsigned i = 0; i < 36; i++) {
      unsigned n = p->inventory_count[i], limit = getItemStackSize(item);
      if (!n) space += limit;
      else if (p->inventory_items[i] == item && n < limit) space += limit-n;
    }
    if (space < count) return false;
  } else if ((c.count && c.item != item) || (unsigned)c.count+count > getItemStackSize(item)) return false;
  for (unsigned i = 0; i < 9; i++) if (p->craft_items[i]) {
    p->craft_count[i]--; if (!p->craft_count[i]) p->craft_items[i] = 0;
  }
  if (shift) items_insert(p,item,count);
  else set_cursor(p,(Stack){item,(uint8_t)(c.count+count)});
  return true;
}
static void drag_click (PlayerData *p, Session *s, int w, uint16_t wire, uint8_t button) {
  unsigned phase = button%4u, kind = button/4u; Stack c = cursor_stack(p);
  if (kind > 1) { s->drag = 0; return; } /* Creative creation uses Creative Slot. */
  if (phase == 0) {
    s->drag = (uint8_t)(kind+1); s->drag_slots = 0; s->drag_item = c.item; s->drag_count = c.count; return;
  }
  if (s->drag != kind+1 || c.item != s->drag_item || c.count != s->drag_count) { s->drag = 0; return; }
  if (phase == 1 && wire < 64 && accessible(p,w,wire) && !output_slot(w,wire)) {
    Stack dst = read_slot(p,w,wire);
    if ((!dst.count || dst.item == c.item) && dst.count < getItemStackSize(c.item) && accepts(w,wire,(Stack){c.item,1}))
      s->drag_slots |= UINT64_C(1)<<wire;
  } else if (phase == 2) {
    unsigned slots = 0; for (unsigned i = 0; i < 64; i++) if (s->drag_slots&(UINT64_C(1)<<i)) slots++;
    unsigned each = kind ? 1u : slots ? c.count/slots : 0;
    for (uint16_t i = 0; i < end_slot(w) && c.count; i++) if (s->drag_slots&(UINT64_C(1)<<i)) {
      Stack dst = read_slot(p,w,i); if (dst.count && dst.item != c.item) continue;
      unsigned limit = (w == 0 && i >= 5 && i <= 8) ? 1u : getItemStackSize(c.item);
      if (dst.count >= limit) continue;
      unsigned amount = each; if (amount > limit-dst.count) amount = limit-dst.count; if (amount > c.count) amount = c.count;
      put_slot(p,w,i,(Stack){c.item,(uint8_t)(dst.count+amount)}); c.count = (uint8_t)(c.count-amount);
    }
    set_cursor(p,c); s->drag = 0;
  }
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

  Session *s = session(p); if (!s || s->window != w) return 1;
  Stack before[63] = {{0,0}};
  for (uint16_t wire = 0; wire < end_slot(w); wire++) if (accessible(p,w,wire)) before[wire] = read_slot(p,w,wire);
  if (mode != 5) s->drag = 0;
  if (output) {
    if (mode <= 1) for (unsigned attempt = 0; attempt < (mode == 1 ? 64u : 1u); attempt++)
      if (!craft_once(p,w,mode == 1)) break;
  } else if (mode == 4 || (mode == 0 && clicked == 64537)) {
    if (mode == 4 && clicked != 64537) {
      Stack slot = read_slot(p,w,clicked); uint8_t amount = button ? slot.count : (slot.count ? 1 : 0);
      if (items_drop_stack(p,slot.item,amount)) { slot.count = (uint8_t)(slot.count-amount); put_slot(p,w,clicked,slot); }
    } else if (mode == 0) {
      Stack c = cursor_stack(p); uint8_t amount = button ? (c.count ? 1 : 0) : c.count;
      if (items_drop_stack(p,c.item,amount)) { c.count = (uint8_t)(c.count-amount); set_cursor(p,c); }
    }
  } else if (mode == 5) drag_click(p,s,w,clicked,button);
  else if (clicked != 64537) {
    if (mode == 0) ordinary_click(p,w,clicked,button);
    else if (mode == 1) quick_move(p,w,clicked);
    else if (mode == 2) {
      unsigned index = button == 40 ? 40u : button;
      Stack hotbar = {p->inventory_items[index],p->inventory_count[index]}, slot = read_slot(p,w,clicked);
      if (mapped(w,clicked) != (int)index && accepts(w,clicked,hotbar)) {
        put_slot(p,w,clicked,hotbar); p->inventory_items[index] = slot.count ? slot.item : 0; p->inventory_count[index] = slot.count;
      }
    } else if (mode == 6) {
      Stack c = cursor_stack(p);
      for (uint16_t wire = 0; c.count && wire < end_slot(w) && c.count < getItemStackSize(c.item); wire++) {
        if (!accessible(p,w,wire) || output_slot(w,wire)) continue;
        Stack slot = read_slot(p,w,wire); if (slot.item != c.item) continue;
        unsigned amount = getItemStackSize(c.item)-c.count; if (amount > slot.count) amount = slot.count;
        c.count = (uint8_t)(c.count+amount); slot.count = (uint8_t)(slot.count-amount); put_slot(p,w,wire,slot);
      }
      set_cursor(p,c);
    }
  }
  if (w == 14) getSmeltingOutput(p);
  sync_window(p,w,before,changes,n);
  return 0;
}
