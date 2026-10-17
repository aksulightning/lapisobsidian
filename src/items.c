#include "plates.h"
#include <math.h>
#include <float.h>
#include <stdio.h>
#include "durable.h"
#include "drop_store.h"
#include <stdlib.h>
#include <string.h>
#include "items.h"
#include "plate_contexts.h"
#include "commands.h"
#include "doors.h"
#include "circuits.h"
#include "packets.h"
#include "procedures.h"
#include "protocol.h"
#include "registry.h"
#include "registries.h"
#include "tools.h"
#include "varnum.h"
#include "worldgen.h"

_Static_assert(sizeof(float)==4 && FLT_MANT_DIG==24 && FLT_MAX_EXP==128,"Drop storage requires IEEE binary32 floats");
_Static_assert(LAPIS_PROTOCOL_VERSION == 772, "Review item entity type and metadata for this protocol");
_Static_assert(MAX_PLAYERS <= 32, "Item visibility mask holds at most 32 players");
_Static_assert(MAX_MOBS < 1022, "Mob and item entity IDs must not overlap");

typedef struct {
  DroppedItem items[ITEM_ENTITY_LIMIT];
  uint64_t next_id;
  char path[128];
} ItemsContext;
static ItemsContext legacy_context, *ctx = &legacy_context;
size_t items_context_size (void) { return sizeof(*ctx); }
void items_context_select (void *memory) { ctx = memory ? memory : &legacy_context; }


const DroppedItem *items_at (size_t i) { return i < ITEM_ENTITY_LIMIT ? &ctx->items[i] : NULL; }
static int entity_id (size_t i) { return ITEM_ENTITY_BASE-(int)i; }
static int player_index (const PlayerData *p) {
  for (int i = 0; i < MAX_PLAYERS; i++) if (plates_player_active(&player_data[i])) if (p == &player_data[i]) return i;
  return -1;
}
static bool valid (uint16_t item, uint8_t count, int x, int y, int z) {
  return item && registry_item_id_valid(item) && count && count <= getItemStackSize(item) &&
    x >= -32768 && x <= 32767 && z >= -32768 && z <= 32767 && y >= 0 && y <= 255;
}
/* Protocol 772: item entity 69; metadata index 8, serializer 7 (Slot).
 * NoGravity prevents client simulation from fighting our low-rate server physics. */
static void metadata (int fd, size_t i) {
  const DroppedItem *d = &ctx->items[i];
  uint32_t n = 10u+(uint32_t)sizeVarInt((uint32_t)entity_id(i))+(uint32_t)sizeVarInt(d->item);
  writeVarInt(fd,n); writeByte(fd,0x5c); writeVarInt(fd,(uint32_t)entity_id(i));
  writeByte(fd,5); writeByte(fd,8); writeByte(fd,1);
  writeByte(fd,8); writeByte(fd,7); writeVarInt(fd,d->count);
  writeVarInt(fd,d->item); writeByte(fd,0); writeByte(fd,0); writeByte(fd,255);
}
static void changed (size_t i) {
  for (int p = 0; p < MAX_PLAYERS; p++) if (plates_player_active(&player_data[p])) if (ctx->items[i].viewers & (UINT32_C(1)<<p)) metadata(player_data[p].client_fd,i);
}
static void remove_item (size_t i) {
  for (int p = 0; p < MAX_PLAYERS; p++) if (plates_player_active(&player_data[p])) if (ctx->items[i].viewers & (UINT32_C(1)<<p)) sc_removeEntity(player_data[p].client_fd,entity_id(i));
  memset(&ctx->items[i],0,sizeof(ctx->items[i]));
}
void items_clear (void) { for (size_t i = 0; i < ITEM_ENTITY_LIMIT; i++) if (ctx->items[i].count) remove_item(i); }
void items_forget_player (PlayerData *p) {
  int index = player_index(p); if (index < 0) return;
  for (size_t i = 0; i < ITEM_ENTITY_LIMIT; i++) ctx->items[i].viewers &= ~(UINT32_C(1)<<index);
}
void items_sync_player (PlayerData *p) {
  int index = player_index(p); if (index < 0) return;
  uint32_t mask = UINT32_C(1)<<index;
  for (size_t i = 0; i < ITEM_ENTITY_LIMIT; i++) {
    DroppedItem *d = &ctx->items[i]; if (!d->count) continue;
    bool visible = p->client_fd >= 0 && !(p->flags&0x22) &&
      abs(div_floor(d->x,16)-div_floor(p->x,16)) <= VIEW_DISTANCE &&
      abs(div_floor(d->z,16)-div_floor(p->z,16)) <= VIEW_DISTANCE;
    if (visible && !(d->viewers&mask)) {
      uint8_t uuid[16] = {'L','a','p','i','s','I','t','e','m',0,0,0,0,0,0,0};
      uuid[14] = (uint8_t)(i>>8); uuid[15] = (uint8_t)i;
      sc_spawnEntity(p->client_fd,entity_id(i),uuid,69,d->x+0.5,d->y,d->z+0.5,0,0);
      metadata(p->client_fd,i); d->viewers |= mask;
    } else if (!visible && (d->viewers&mask)) {
      if (p->client_fd >= 0) sc_removeEntity(p->client_fd,entity_id(i));
      d->viewers &= ~mask;
    }
  }
}
static int destination (uint16_t item, uint8_t count, int x, int y, int z) {
  if (!valid(item,count,x,y,z)) return -1;
  int unused = -1;
  for (size_t i = 0; i < ITEM_ENTITY_LIMIT; i++) {
    const DroppedItem *d = &ctx->items[i];
    if (!d->count) { if (unused < 0) unused = (int)i; continue; }
    if (d->item == item && d->x == x && d->z == z && fabsf(d->y-((float)y+0.25f)) <= 0.5f &&
        (unsigned)d->count+count <= getItemStackSize(item)) return (int)i;
  }
  return ctx->next_id==UINT64_MAX ? -1 : unused;
}
bool items_can_spawn (uint16_t item, uint8_t count, int x, int y, int z) { return destination(item,count,x,y,z) >= 0; }
bool items_can_spawn_pair (uint16_t a, uint8_t ac, uint16_t b, uint8_t bc, int x, int y, int z) {
  if (a == b) return (unsigned)ac+bc <= 255 && items_can_spawn(a,(uint8_t)(ac+bc),x,y,z);
  int first = destination(a,ac,x,y,z), second = destination(b,bc,x,y,z);
  if (first < 0 || second < 0) return false;
  if (first != second) return true;
  /* Both selected the same empty slot: reserve one more before harvesting. */
  for (unsigned i = 0; i < ITEM_ENTITY_LIMIT; i++) if ((int)i != first && !ctx->items[i].count) return true;
  return false;
}
bool items_spawn (uint16_t item, uint8_t count, int x, int y, int z, uint32_t delay) {
  int index = destination(item,count,x,y,z); if (index < 0 || delay > ITEM_LIFETIME_MS) return false;
  size_t i = (size_t)index; DroppedItem *d = &ctx->items[i];
  if (!ctx->next_id) ctx->next_id = 1;
  if (!d->count && ctx->next_id == UINT64_MAX) return false;
  if (d->count) {
    d->count = (uint8_t)(d->count+count);
    if (d->delay_ms < delay) d->delay_ms = delay;
    changed(i); /* Keep the older age; merging cannot extend lifetime. */
  } else {
    *d = (DroppedItem){.persistent_id=ctx->next_id++,.x=(int16_t)x,.z=(int16_t)z,.y=(float)y+0.25f,.item=item,.count=count,.delay_ms=delay};
    for (int p = 0; p < MAX_PLAYERS; p++) if (plates_player_active(&player_data[p])) items_sync_player(&player_data[p]);
  }
  return true;
}
bool items_drop_stack (PlayerData *p, uint16_t item, uint8_t count) {
  if (!p || p->client_fd < 0 || !p->health || (p->flags&0x22) || commands_gamemode(p) == 3) return false;
  /* No speculative throw into ungenerated space: drop at the player's feet. */
  return items_spawn(item,count,p->x,p->y,p->z,1500);
}
bool items_drop_slot (PlayerData *p, uint8_t slot, bool whole) {
  if (!p || slot >= 41) return false;
  uint8_t count = whole ? p->inventory_count[slot] : (p->inventory_count[slot] ? 1 : 0);
  bool ok = items_drop_stack(p,p->inventory_items[slot],count);
  if (ok) {
    p->inventory_count[slot] = (uint8_t)(p->inventory_count[slot]-count);
    if (!p->inventory_count[slot]) p->inventory_items[slot] = 0;
  }
  sc_setContainerSlot(p->client_fd,0,serverSlotToClientSlot(0,slot),p->inventory_count[slot],p->inventory_items[slot]);
  return ok;
}
uint8_t items_insert (PlayerData *p, uint16_t item, uint8_t count) {
  if (!p || !item || !registry_item_id_valid(item) || !count) return 0;
  unsigned left = count, limit = getItemStackSize(item);
  /* Fill existing stacks first, then empty main/hotbar slots; never armor. */
  for (unsigned pass = 0; pass < 2 && left; pass++) for (uint8_t slot = 0; slot < 36 && left; slot++) {
    unsigned current = p->inventory_count[slot];
    if (pass == 0 ? (!current || p->inventory_items[slot] != item) : current != 0) continue;
    if (current >= limit) continue;
    unsigned n = limit-current; if (n > left) n = left;
    p->inventory_items[slot] = item; p->inventory_count[slot] = (uint8_t)(current+n); left -= n;
    sc_setContainerSlot(p->client_fd,0,serverSlotToClientSlot(0,slot),p->inventory_count[slot],item);
  }
  return (uint8_t)(count-left);
}
static bool solid (int x, int y, int z) {
  if (y < 0) return true;
  if (y > 255) return false;
  uint8_t block = getBlockAt((short)x,(uint8_t)y,(short)z);
  if (block == B_oak_door) { const Door *d = doors_at(x,y,z); if (d && d->open) return false; }
  if (block == B_oak_trapdoor || block == B_iron_trapdoor) return !circuits_trapdoor_open(x,y,z);
  return !isPassableBlock(block);
}
void items_tick (int64_t elapsed_us) {
  if (elapsed_us <= 0) return;
  uint32_t elapsed = elapsed_us/1000 > ITEM_LIFETIME_MS ? ITEM_LIFETIME_MS : (uint32_t)(elapsed_us/1000);
  for (int p = 0; p < MAX_PLAYERS; p++) if (plates_player_active(&player_data[p])) items_sync_player(&player_data[p]);
  for (size_t i = 0; i < ITEM_ENTITY_LIMIT; i++) {
    DroppedItem *d = &ctx->items[i]; if (!d->count) continue;
    if (elapsed >= ITEM_LIFETIME_MS-d->age_ms) { remove_item(i); continue; }
    d->age_ms += elapsed;
    d->delay_ms = elapsed >= d->delay_ms ? 0 : d->delay_ms-elapsed;
    float old_y = d->y;
    /* At most 20 steps after a stall. Each fall step is <= .8 blocks. */
    uint32_t remaining = elapsed > 1000 ? 1000 : elapsed;
    while (remaining) {
      uint32_t step = remaining > 50 ? 50 : remaining; remaining -= step;
      float dt = (float)step/1000.0f;
      d->velocity -= 20.0f*dt; if (d->velocity < -16.0f) d->velocity = -16.0f;
      float next = d->y+d->velocity*dt;
      if (next < 0) { d->y = 0; d->velocity = 0; break; }
      if (solid(d->x,(int)floorf(next),d->z)) {
        int surface = (int)floorf(next)+1;
        if ((float)surface <= d->y) d->y = (float)surface;
        d->velocity = 0; break;
      }
      d->y = next;
    }
    if (!d->count) continue;
    uint8_t block = getBlockAt(d->x,(uint8_t)d->y,d->z);
    if (block >= B_lava && block < B_lava+4) { remove_item(i); continue; }
    if (old_y != d->y) for (int p = 0; p < MAX_PLAYERS; p++) if (plates_player_active(&player_data[p])) if (d->viewers&(UINT32_C(1)<<p))
      sc_teleportEntity(player_data[p].client_fd,entity_id(i),d->x+0.5,d->y,d->z+0.5,0,0);
    if (d->delay_ms) continue;
    for (int p = 0; p < MAX_PLAYERS && d->count; p++) if (plates_player_active(&player_data[p])) {
      PlayerData *player = &player_data[p];
      if (player->client_fd < 0 || (player->flags&0x22) || !player->health || commands_gamemode(player) == 3 ||
          abs(player->x-d->x) > 1 || abs(player->z-d->z) > 1 || fabsf((float)player->y-d->y) > 1.5f) continue;
      /* Do not collect through a wall between adjacent block cells. */
      if (solid(d->x,(int)floorf(d->y),d->z) || solid(player->x,(int)floorf(d->y),d->z) ||
          solid(d->x,(int)floorf(d->y),player->z)) continue;
      uint8_t accepted = items_insert(player,d->item,d->count); if (!accepted) continue;
      for (int v = 0; v < MAX_PLAYERS; v++) if (plates_player_active(&player_data[v])) if (d->viewers&(UINT32_C(1)<<v))
        sc_pickupItem(player_data[v].client_fd,entity_id(i),player->client_fd,accepted);
      d->count = (uint8_t)(d->count-accepted);
      if (!d->count) remove_item(i); else changed(i);
    }
  }
  /* Bounded O(limit^2) merge, restricted to the same horizontal cell. */
  for (size_t i = 0; i < ITEM_ENTITY_LIMIT; i++) if (ctx->items[i].count) {
    DroppedItem *a = &ctx->items[i];
    for (size_t j = i+1; j < ITEM_ENTITY_LIMIT; j++) {
      DroppedItem *b = &ctx->items[j];
      if (!b->count || a->item != b->item || a->x != b->x || a->z != b->z || fabsf(a->y-b->y) > 0.5f) continue;
      unsigned room = (unsigned)getItemStackSize(a->item)-a->count;
      unsigned n = b->count < room ? b->count : room; if (!n) break;
      a->count = (uint8_t)(a->count+n); b->count = (uint8_t)(b->count-n);
      if (a->age_ms < b->age_ms) a->age_ms = b->age_ms;
      if (a->delay_ms < b->delay_ms) a->delay_ms = b->delay_ms;
      changed(i); if (!b->count) remove_item(j); else changed(j);
    }
  }
}

/* Portable drop format: 32-byte header, 32-byte records, trailing CRC32.
 * No pointers, connection IDs, padding or viewer bits enter this file. */
#define DROP_HEADER 32u
#define DROP_RECORD 32u
#define DROP_MAX (DROP_HEADER + ITEM_ENTITY_LIMIT*DROP_RECORD + 4u)
const char *items_path(void) { return ctx->path; }
bool items_load(const char *path) {
  memset(ctx,0,sizeof(*ctx)); ctx->next_id=1;
  if(!path) return true;
  if(strlen(path)>=sizeof(ctx->path)) return false;
  uint8_t *data=NULL; size_t n=0;
  if(!durable_read(path,&data,&n,DROP_MAX)) return false;
#if !LAPIS_ENABLE_EXPERIMENTAL_DROPSYSTEM
  /* Never let a transient build spend inventory while a stale registry waits
   * to resurrect it. An operator must explicitly archive the registry. */
  if(data) { free(data); fprintf(stderr,"%s requires LAPIS_ENABLE_EXPERIMENTAL_DROPSYSTEM=1 (or archive it explicitly).\n",path); return false; }
  return true;
#else
  strcpy(ctx->path,path);
  if(!data) return items_save();
  bool ok=n>=DROP_HEADER+4 && !memcmp(data,"LAPDROP1",8) &&
    durable_get32(data+8)==1 && durable_get32(data+12)==LAPIS_PROTOCOL_VERSION &&
    durable_get32(data+16)==ITEM_ENTITY_LIMIT && durable_get32(data+20)<=ITEM_ENTITY_LIMIT;
  unsigned count=ok?durable_get32(data+20):0;
  if(ok) ok=n==DROP_HEADER+(size_t)count*DROP_RECORD+4 && durable_get32(data+n-4)==durable_crc(data,n-4);
  uint64_t next=ok?durable_get64(data+24):0;
  if(!next) ok=false;
  for(unsigned i=0;ok && i<count;i++) {
    const uint8_t *r=data+DROP_HEADER+i*DROP_RECORD; DroppedItem *d=&ctx->items[i];
    d->persistent_id=durable_get64(r);
    uint32_t y=durable_get32(r+8), v=durable_get32(r+12);
    memcpy(&d->y,&y,4); memcpy(&d->velocity,&v,4);
    unsigned x=(unsigned)r[16]|((unsigned)r[17]<<8), z=(unsigned)r[18]|((unsigned)r[19]<<8);
    d->x=(int16_t)(x<32768? (int)x : (int)x-65536); d->z=(int16_t)(z<32768? (int)z : (int)z-65536);
    d->item=(uint16_t)((unsigned)r[20]|((unsigned)r[21]<<8)); d->count=r[22];
    d->age_ms=durable_get32(r+24); d->delay_ms=durable_get32(r+28);
    ok=d->persistent_id && d->persistent_id<next && !r[23] && isfinite(d->y) &&
      d->y>=0 && d->y<=255.25f && isfinite(d->velocity) && d->velocity>=-16 && d->velocity<=0 &&
      valid(d->item,d->count,d->x,(int)d->y,d->z) && d->age_ms<ITEM_LIFETIME_MS && d->delay_ms<=ITEM_LIFETIME_MS;
    for(unsigned j=0;ok && j<i;j++) if(ctx->items[j].persistent_id==d->persistent_id) ok=false;
  }
  free(data);
  if(!ok) { memset(ctx->items,0,sizeof(ctx->items)); fprintf(stderr,"Invalid drop registry: %s\n",path); return false; }
  ctx->next_id=next; return true;
#endif
}
bool items_save(void) {
#if LAPIS_ENABLE_EXPERIMENTAL_DROPSYSTEM
  if(!ctx->path[0]) return true;
  uint8_t data[DROP_MAX]={0}; memcpy(data,"LAPDROP1",8);
  durable_put32(data+8,1); durable_put32(data+12,LAPIS_PROTOCOL_VERSION);
  durable_put32(data+16,ITEM_ENTITY_LIMIT); durable_put64(data+24,ctx->next_id);
  unsigned count=0;
  for(unsigned i=0;i<ITEM_ENTITY_LIMIT;i++) {
    const DroppedItem *d=&ctx->items[i]; if(!d->count) continue;
    uint8_t *r=data+DROP_HEADER+count++*DROP_RECORD; uint32_t y,v;
    memcpy(&y,&d->y,4); memcpy(&v,&d->velocity,4);
    durable_put64(r,d->persistent_id); durable_put32(r+8,y); durable_put32(r+12,v);
    r[16]=(uint8_t)d->x; r[17]=(uint8_t)((uint16_t)d->x>>8);
    r[18]=(uint8_t)d->z; r[19]=(uint8_t)((uint16_t)d->z>>8);
    r[20]=(uint8_t)d->item; r[21]=(uint8_t)(d->item>>8); r[22]=d->count;
    durable_put32(r+24,d->age_ms); durable_put32(r+28,d->delay_ms);
  }
  durable_put32(data+20,count); size_t n=DROP_HEADER+(size_t)count*DROP_RECORD;
  durable_put32(data+n,durable_crc(data,n));
  return durable_replace(ctx->path,data,n+4);
#else
  return true;
#endif
}
