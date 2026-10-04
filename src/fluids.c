#include "plates.h"
#include <math.h>
#include <string.h>
#include "fluids.h"
#include "plate_contexts.h"
#include "commands.h"
#include "circuits.h"
#include "farming.h"
#include "items.h"
#include "mobs.h"
#include "packets.h"
#include "procedures.h"
#include "registries.h"
#include "worldgen.h"

/* Fixed pool with hash chains: no recursive updates or allocation. Each cell
 * is pending at most once. Overflow starts a bounded recovery scan of edits
 * and their neighbours, including natural fluids exposed by saved air edits. */
typedef struct {
  int16_t x,z,next;
  uint8_t y,used;
  uint32_t due;
} Update;

typedef struct {
  Update queue[FLUID_QUEUE_LIMIT];
  int16_t buckets[FLUID_QUEUE_LIMIT];
  int16_t free_head;
  unsigned pending, cursor;
  uint32_t clock_tick;
  int64_t tick_remainder;
  int recovery;
  bool ready, recovering, repeat_scan, changing, storage_blocked;
} FluidsContext;
static FluidsContext legacy_context, *ctx = &legacy_context;
size_t fluids_context_size (void) { return sizeof(*ctx); }
void fluids_context_select (void *memory) { ctx = memory ? memory : &legacy_context; }

static const int step[6][3] = {{0,-1,0},{0,1,0},{0,0,-1},{0,0,1},{-1,0,0},{1,0,0}};
static bool coords (int x, int y, int z) {
  return x >= -32768 && x <= 32767 && z >= -32768 && z <= 32767 && y >= 0 && y <= 255;
}
static uint8_t kind (uint8_t b) {
  if (b >= B_water && b <= B_water_7) return B_water;
  if (b >= B_lava && b <= B_lava_6) return B_lava;
  return 0;
}
static unsigned hash (int x, int y, int z) {
  return ((uint32_t)x*73856093u ^ (uint32_t)z*19349663u ^ (uint32_t)y*83492791u) & (FLUID_QUEUE_LIMIT-1u);
}
static void schedule (int x, int y, int z, unsigned delay) {
  if (!coords(x,y,z)) return;
  unsigned h = hash(x,y,z);
  for (int i = ctx->buckets[h]; i >= 0; i = ctx->queue[i].next)
    if (ctx->queue[i].x == x && ctx->queue[i].y == y && ctx->queue[i].z == z) return;
  if (ctx->free_head < 0) { ctx->recovering = true; ctx->repeat_scan = true; return; }
  int i = ctx->free_head; ctx->free_head = ctx->queue[i].next;
  ctx->queue[i] = (Update){.x=(int16_t)x,.z=(int16_t)z,.y=(uint8_t)y,.used=delay >= 6 ? 2 : 1,
    .due=ctx->clock_tick+delay,.next=ctx->buckets[h]};
  ctx->buckets[h] = (int16_t)i; ctx->pending++;
}
static void unschedule (unsigned i) {
  unsigned h = hash(ctx->queue[i].x,ctx->queue[i].y,ctx->queue[i].z);
  int16_t *link = &ctx->buckets[h];
  while (*link != (int)i) link = &ctx->queue[*link].next;
  *link = ctx->queue[i].next;
  ctx->queue[i].used = 0; ctx->queue[i].next = ctx->free_head; ctx->free_head = (int16_t)i; ctx->pending--;
}
void fluids_block_changed (int x, int y, int z) {
  if (!ctx->ready || !coords(x,y,z)) return;
  if (!ctx->changing) ctx->storage_blocked = false;
  unsigned delay = kind(getBlockAt(x,y,z)) == B_lava ? 6u : 2u;
  schedule(x,y,z,delay);
  for (unsigned i = 0; i < 6; i++) schedule(x+step[i][0],y+step[i][1],z+step[i][2],delay);
}
void fluids_init (void) {
  memset(ctx->queue,0,sizeof(ctx->queue));
  for (unsigned i = 0; i < FLUID_QUEUE_LIMIT; i++) {
    ctx->buckets[i] = -1; ctx->queue[i].next = (int16_t)(i+1);
  }
  ctx->queue[FLUID_QUEUE_LIMIT-1].next = -1; ctx->free_head = 0;
  ctx->pending = ctx->cursor = ctx->clock_tick = 0; ctx->tick_remainder = 0; ctx->recovery = 0;
  ctx->ready = ctx->recovering = true; ctx->repeat_scan = ctx->changing = ctx->storage_blocked = false;
}
static bool fragile (uint8_t b) {
  return b == B_short_grass || b == B_fern || b == B_dead_bush || b == B_snow ||
    b == B_oak_sapling || (b >= B_dandelion && b <= B_red_mushroom) ||
    b == B_torch || b == B_redstone_torch || b == B_wheat ||
    b == B_stone_pressure_plate || b == B_oak_pressure_plate;
}
static bool open_cell (uint8_t b) { return b == B_air || kind(b) || fragile(b); }
static uint8_t at (int x, int y, int z) {
  /* Out-of-range cells are sealed. Never narrow an unchecked coordinate. */
  return coords(x,y,z) ? getBlockAt(x,y,z) : B_bedrock;
}
static void sound (const char *name, int x, int y, int z) {
  for (int i = 0; i < MAX_PLAYERS; i++) if (plates_player_active(&player_data[i])) {
    const PlayerData *p = &player_data[i];
    int dx = (int)p->x-x, dy = (int)p->y-y, dz = (int)p->z-z;
    if (p->client_fd < 0 || (p->flags&0x22) || dx < -32 || dx > 32 || dy < -32 || dy > 32 || dz < -32 || dz > 32 ||
        dx*dx+dy*dy+dz*dz > 32*32) continue;
    sc_mob_sound_category(p->client_fd,name,x,y,z,4);
  }
}
static bool replace (int x, int y, int z, uint8_t old, uint8_t b) {
  if (old == b) return true;
  uint16_t drop = 0;
  if (fragile(old)) {
    if (old == B_wheat && farming_harvest(NULL,x,y,z)) {
      if (getBlockAt(x,y,z) != B_air) return false;
    } else {
      /* Circuit carriers use their own drop mapping (dust is a torch carrier). */
      drop = circuits_drop(x,y,z,getMiningResult(0,old));
      if (drop && !items_can_spawn(drop,1,x,y,z)) return false;
    }
  }
  ctx->changing = true;
  bool ok = !makeBlockChange((short)x,(uint8_t)y,(short)z,b);
  ctx->changing = false;
  if (!ok) { ctx->storage_blocked = true; return false; }
  ctx->storage_blocked = false;
  if (drop) items_spawn(drop,1,x,y,z,500);
  return true;
}
static uint8_t desired (int x, int y, int z, uint8_t current) {
  uint8_t f = kind(current), above = at(x,y+1,z);
  if (f == B_lava) {
    /* Water from above or a side cools lava; water beneath it makes stone. */
    for (unsigned i = 1; i < 6; i++) if (kind(at(x+step[i][0],y+step[i][1],z+step[i][2])) == B_water)
      return current == B_lava ? B_obsidian : B_cobblestone;
  }
  if (f == B_water && kind(above) == B_lava) return B_stone;
  if (current == B_water || current == B_lava) return current;
  if (!open_cell(current)) return current;
  /* Level one is also the compact falling representation. Sources are never
   * created by flow. A falling column therefore drains when its supply goes. */
  if (kind(above) && (!f || f == kind(above))) return (uint8_t)(kind(above)+1);
  unsigned best = 255; uint8_t result = B_air;
  for (unsigned i = 2; i < 6; i++) {
    int nx = x+step[i][0], nz = z+step[i][2];
    uint8_t neighbour = at(nx,y,nz), k = kind(neighbour);
    if (!k || (f && f != k)) continue;
    /* Prefer falling to sideways spreading. A filled channel has support. */
    uint8_t below = at(nx,y-1,nz);
    if (below == B_air || fragile(below) || (kind(below) == k && below != k)) continue;
    unsigned level = (unsigned)(neighbour-k);
    if (kind(at(nx,y+1,nz)) == k) level = 0; /* foot of a waterfall */
    level++;
    unsigned limit = k == B_water ? 7u : 3u;
    if (level <= limit && level < best) { best = level; result = (uint8_t)(k+level); }
  }
  return result == B_air && !f ? current : result;
}
void fluids_tick (int64_t elapsed_us) {
  if (!ctx->ready || elapsed_us <= 0 || ctx->storage_blocked) return;
  /* No catch-up flood after a stalled tick. One call does at most 64 cells. */
  ctx->tick_remainder += elapsed_us > 100000 ? 100000 : elapsed_us;
  if (ctx->tick_remainder < 100000) return;
  ctx->tick_remainder -= 100000; ctx->clock_tick++;
  unsigned work = 0;
  for (unsigned scanned = 0; scanned < FLUID_QUEUE_LIMIT && work < FLUID_TICK_BUDGET && ctx->pending; scanned++) {
    unsigned i = ctx->cursor; ctx->cursor = (ctx->cursor+1u)%FLUID_QUEUE_LIMIT;
    if (!ctx->queue[i].used || (uint32_t)(ctx->clock_tick-ctx->queue[i].due) >= UINT32_C(0x80000000)) continue;
    int x = ctx->queue[i].x, y = ctx->queue[i].y, z = ctx->queue[i].z;
    uint8_t before = at(x,y,z), after = desired(x,y,z,before);
    work++;
    /* Water changes every 200 ms; lava changes on the slower 600 ms cadence. */
    if ((kind(before) == B_lava || kind(after) == B_lava) && ctx->queue[i].used != 2) {
      ctx->queue[i].used = 2; ctx->queue[i].due = ctx->clock_tick+4u; continue;
    }
    unschedule(i);
    if (after != before) {
      if (!replace(x,y,z,before,after)) schedule(x,y,z,6);
      else if (kind(before) && !kind(after) && after != B_air) sound("minecraft:block.lava.extinguish",x,y,z);
    }
    if (ctx->storage_blocked) return;
  }
  /* Restore pending flow after restart without scanning generated chunks. */
  if (ctx->recovering && ctx->pending < FLUID_QUEUE_LIMIT/2) {
    for (unsigned n = 0; n < 16 && ctx->recovery < block_changes_count; n++, ctx->recovery++) {
      const BlockChange *b = &block_changes[ctx->recovery];
      if (b->block == 255) continue;
      if (b->block == B_chest) { ctx->recovery += 14; continue; }
      fluids_block_changed(b->x,b->y,b->z);
    }
    if (ctx->recovery >= block_changes_count) {
      ctx->recovering = ctx->repeat_scan; ctx->repeat_scan = false; ctx->recovery = 0;
    }
  }
}

static void use_bucket (PlayerData *p, float yaw, float pitch) {
  unsigned slot = p->hotbar;
  uint16_t held = p->inventory_items[slot];
  if (p->client_fd < 0 || !p->health || (p->flags&0x22) || commands_gamemode(p) >= 2 ||
      !p->inventory_count[slot] || p->inventory_count[slot] > getItemStackSize(held) ||
      !isfinite(yaw) || !isfinite(pitch) || pitch < -90 || pitch > 90) return;
  double a = fmod((double)yaw,360.0)*0.017453292519943295;
  double b = (double)pitch*0.017453292519943295;
  double dx = -sin(a)*cos(b), dy = -sin(b), dz = cos(a)*cos(b);
  int x = p->x, y = p->y+1, z = p->z, px = x, py = y, pz = z;
  uint8_t target = B_air;
  bool hit = false;
  /* Positions are block precision in the inherited player struct. Cast from
   * that block's centre using a bounded six-block sampling ray. */
  for (unsigned i = 0; i <= 96; i++) {
    double distance = (double)i/16.0;
    x = (int)floor(p->x+0.5+dx*distance);
    y = (int)floor(p->y+1.62+dy*distance);
    z = (int)floor(p->z+0.5+dz*distance);
    if (!coords(x,y,z)) break;
    target = getBlockAt(x,y,z);
    if (held == I_bucket && (target == B_water || target == B_lava)) { hit = true; break; }
    if (!kind(target) && target != B_air) { hit = true; break; }
    px = x; py = y; pz = z;
  }
  if (!hit) return;
  uint8_t placed;
  uint16_t result;
  if (held == I_bucket) {
    if (target != B_water && target != B_lava) return;
    placed = B_air; result = target == B_water ? I_water_bucket : I_lava_bucket;
  } else {
    placed = held == I_water_bucket ? B_water : B_lava; result = I_bucket;
    if (!fragile(target)) { x = px; y = py; z = pz; }
    if (!coords(x,y,z)) return;
    target = getBlockAt(x,y,z);
    if (kind(target)) {
      if (target == placed) return;
      if (kind(target) != placed)
        placed = placed == B_lava ? B_stone : (target == B_lava ? B_obsidian : B_cobblestone);
    } else if (target != B_air && !fragile(target)) return;
  }
  sc_blockUpdate(p->client_fd,x,y,z,target); /* correct rejected client prediction too */
  bool creative = commands_gamemode(p) == 1;
  unsigned output = slot;
  if (!creative && p->inventory_count[slot] > 1) {
    for (output = 0; output < 36; output++) if (!p->inventory_count[output]) break;
    if (output == 36) return;
  }
  if (!replace(x,y,z,target,placed)) return;
  if (!creative || held == I_bucket) {
    if (output != slot) {
      p->inventory_count[slot]--;
      sc_setContainerSlot(p->client_fd,0,serverSlotToClientSlot(0,(uint8_t)slot),p->inventory_count[slot],held);
    }
    p->inventory_items[output] = result; p->inventory_count[output] = 1;
    sc_setContainerSlot(p->client_fd,0,serverSlotToClientSlot(0,(uint8_t)output),1,result);
  }
  sound(held == I_bucket ? (result == I_water_bucket ? "minecraft:item.bucket.fill" : "minecraft:item.bucket.fill_lava") :
    (held == I_water_bucket ? "minecraft:item.bucket.empty" : "minecraft:item.bucket.empty_lava"),x,y,z);
  return;
}

bool fluids_use_bucket (PlayerData *p, float yaw, float pitch) {
  if (!p || p->hotbar >= 9) return false;
  unsigned slot = p->hotbar;
  uint16_t held = p->inventory_items[slot];
  if (held != I_bucket && held != I_water_bucket && held != I_lava_bucket) return false;
  use_bucket(p,yaw,pitch);
  if (p->client_fd >= 0) sc_setContainerSlot(p->client_fd,0,serverSlotToClientSlot(0,(uint8_t)slot),
    p->inventory_count[slot],p->inventory_items[slot]);
  return true;
}
