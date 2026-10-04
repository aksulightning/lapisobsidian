#include "plates.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "circuits.h"
#include "plate_contexts.h"
#include "commands.h"
#include "doors.h"
#include "items.h"
#include "notes.h"
#include "packets.h"
#include "procedures.h"
#include "protocol.h"
#include "registries.h"
#include "worldgen.h"

_Static_assert(LAPIS_PROTOCOL_VERSION == 772,"Review circuit block states");
/* Dust shares the passable torch carrier in the unchanged 8-bit save palette.
 * Its distinct kind, lever setting and note tuning live in circuits.bin. */
typedef struct { int16_t x,z; uint8_t y,kind,value,power; } Node;

typedef struct {
  Node nodes[CIRCUIT_LIMIT];
  uint16_t table[CIRCUIT_LIMIT*2];
  bool dirty, changing, visual_dirty;
  char save_path[256];
} CircuitsContext;
static CircuitsContext legacy_context, *ctx = &legacy_context;
size_t circuits_context_size (void) { return sizeof(*ctx); }
void circuits_context_select (void *memory) { ctx = memory ? memory : &legacy_context; }

static const int8_t offsets[6][3] = {{1,0,0},{-1,0,0},{0,0,1},{0,0,-1},{0,1,0},{0,-1,0}};
static bool coords (int x, int y, int z) { return x >= -32768 && x <= 32767 && z >= -32768 && z <= 32767 && y >= 0 && y <= 255; }
static unsigned hash (int x, int y, int z) {
  return ((uint32_t)x*73856093u ^ (uint32_t)y*19349663u ^ (uint32_t)z*83492791u) % (CIRCUIT_LIMIT*2u);
}
static void index_nodes (void) {
  memset(ctx->table,0,sizeof(ctx->table));
  for (unsigned i = 0; i < CIRCUIT_LIMIT; i++) if (ctx->nodes[i].kind) {
    unsigned h = hash(ctx->nodes[i].x,ctx->nodes[i].y,ctx->nodes[i].z);
    while (ctx->table[h]) h = (h+1) % (CIRCUIT_LIMIT*2u);
    ctx->table[h] = (uint16_t)(i+1);
  }
}
static Node *at (int x, int y, int z) {
  if (!coords(x,y,z)) return NULL;
  unsigned h = hash(x,y,z);
  for (unsigned i = 0; i < CIRCUIT_LIMIT*2u && ctx->table[h]; i++, h = (h+1) % (CIRCUIT_LIMIT*2u)) {
    Node *n = &ctx->nodes[ctx->table[h]-1];
    if (n->x == x && n->y == y && n->z == z) return n;
  }
  return NULL;
}
static bool trapdoor (uint8_t kind) { return kind == CIRCUIT_WOOD_TRAPDOOR || kind == CIRCUIT_IRON_TRAPDOOR; }
static bool plate (uint8_t kind) { return kind == CIRCUIT_STONE_PLATE || kind == CIRCUIT_WOOD_PLATE; }
static bool source (uint8_t kind) { return kind && kind != CIRCUIT_NOTE && !trapdoor(kind); }
bool circuits_wall_torch_at (int x, int y, int z) { const Node *n = at(x,y,z); return n && n->kind == CIRCUIT_WALL_TORCH; }
bool circuits_trapdoor_open (int x, int y, int z) {
  const Node *n = at(x,y,z); return n && trapdoor(n->kind) && (n->power || (n->kind == CIRCUIT_WOOD_TRAPDOOR && (n->value&8u)));
}
static uint8_t carrier (uint8_t kind) {
  switch (kind) {
    case CIRCUIT_NOTE: return B_note_block;
    case CIRCUIT_LEVER: return B_lever;
    case CIRCUIT_STONE_PLATE: return B_stone_pressure_plate;
    case CIRCUIT_WOOD_PLATE: return B_oak_pressure_plate;
    case CIRCUIT_WOOD_TRAPDOOR: return B_oak_trapdoor;
    case CIRCUIT_IRON_TRAPDOOR: return B_iron_trapdoor;
    default: return B_redstone_torch;
  }
}
static uint16_t item (uint8_t kind) {
  switch (kind) {
    case CIRCUIT_NOTE: return I_note_block;
    case CIRCUIT_LEVER: return I_lever;
    case CIRCUIT_STONE_PLATE: return I_stone_pressure_plate;
    case CIRCUIT_WOOD_PLATE: return I_oak_pressure_plate;
    case CIRCUIT_WOOD_TRAPDOOR: return I_oak_trapdoor;
    case CIRCUIT_IRON_TRAPDOOR: return I_iron_trapdoor;
    case CIRCUIT_DUST: return I_redstone;
    default: return I_redstone_torch;
  }
}
static void support (const Node *n, int *x, int *y, int *z) {
  *x = n->x; *y = n->y; *z = n->z;
  if (n->kind == CIRCUIT_WALL_TORCH) {
    static const int8_t wall[4][2] = {{0,1},{0,-1},{1,0},{-1,0}};
    *x += wall[n->value][0]; *z += wall[n->value][1];
  } else (*y)--;
}
static uint8_t incoming (int x, int y, int z, const Node *skip) {
  uint8_t power = 0;
  for (unsigned d = 0; d < 6; d++) {
    Node *n = at(x+offsets[d][0],y+offsets[d][1],z+offsets[d][2]);
    if (n && n != skip && source(n->kind) && n->power > power) power = n->power;
  }
  return power;
}
uint8_t circuits_power (int x, int y, int z) { const Node *n = at(x,y,z); return n ? n->power : 0; }
bool circuits_powered (int x, int y, int z) { return incoming(x,y,z,NULL) != 0; }
uint16_t circuits_drop (int x, int y, int z, uint16_t fallback) { const Node *n = at(x,y,z); return n ? item(n->kind) : fallback; }
static bool supported (const Node *n) {
  if (n->kind == CIRCUIT_NOTE || trapdoor(n->kind)) return true;
  int x,y,z; support(n,&x,&y,&z);
  if (!coords(x,y,z)) return false;
  uint8_t b = getBlockAt(x,y,z);
  return !isPassableBlock(b) && b != B_oak_door && b != B_iron_door && b != B_oak_trapdoor && b != B_iron_trapdoor;
}
static bool near (const PlayerData *p, int x, int y, int z) {
  if (!p || p->client_fd < 0 || !p->health || (p->flags&0x22) || commands_gamemode(p) == 3 || !coords(x,y,z)) return false;
  int dx = (int)p->x-x, dy = (int)p->y-y, dz = (int)p->z-z;
  return dx >= -6 && dx <= 6 && dy >= -6 && dy <= 6 && dz >= -6 && dz <= 6;
}
static void broadcast (const Node *n) {
  for (int i = 0; i < MAX_PLAYERS; i++) if (plates_player_active(&player_data[i])) if (player_data[i].client_fd >= 0 && !(player_data[i].flags&0x22))
    sc_blockUpdate(player_data[i].client_fd,n->x,n->y,n->z,carrier(n->kind));
}
bool circuits_state_at (int x, int y, int z, uint8_t block, uint16_t *state) {
  const Node *n = at(x,y,z); if (!n || !state || carrier(n->kind) != block) return false;
  if (n->kind == CIRCUIT_DUST) {
    /* east,north,power,south,west; side=1, none=2. No stairs in this subset. */
    unsigned e = at(x+1,y,z) ? 1u : 2u, north = at(x,y,z-1) ? 1u : 2u;
    unsigned s = at(x,y,z+1) ? 1u : 2u, w = at(x-1,y,z) ? 1u : 2u;
    *state = (uint16_t)(3042u+(((e*3u+north)*16u+n->power)*3u+s)*3u+w);
  } else if (n->kind == CIRCUIT_TORCH) *state = (uint16_t)(5916u+(n->power ? 0u : 1u));
  else if (n->kind == CIRCUIT_LEVER) *state = (uint16_t)(5802u+(n->value ? 0u : 1u));
  else if (n->kind == CIRCUIT_WALL_TORCH) *state = (uint16_t)(5918u+n->value*2u+(n->power ? 0u : 1u));
  else if (plate(n->kind)) *state = (uint16_t)((n->kind == CIRCUIT_STONE_PLATE ? 5826u : 5892u)+(n->power ? 0u : 1u));
  else if (trapdoor(n->kind)) *state = (uint16_t)((n->kind == CIRCUIT_WOOD_TRAPDOOR ? 6140u : 11288u)+
    (n->value&3u)*16u+((n->value&4u) ? 0u : 8u)+(circuits_trapdoor_open(x,y,z) ? 0u : 4u)+(n->power ? 0u : 2u)+1u);
  else *state = (uint16_t)(581u+notes_instrument(x,y,z)*50u+n->value*2u+(n->power ? 0u : 1u));
  return true;
}
void circuits_send_chunk (int fd, int cx, int cz) {
  if (cx < -2048 || cx > 2047 || cz < -2048 || cz > 2047) return;
  for (unsigned i = 0; i < CIRCUIT_LIMIT; i++) {
    const Node *n = &ctx->nodes[i];
    if (n->kind && n->x >= cx*16 && n->x < cx*16+16 && n->z >= cz*16 && n->z < cz*16+16)
      sc_blockUpdate(fd,n->x,n->y,n->z,carrier(n->kind));
  }
}
void circuits_strike (int x, int y, int z) {
  const Node *n = at(x,y,z);
  if (n && n->kind == CIRCUIT_NOTE && getBlockAt(x,y+1,z) == B_air)
    notes_play(x,y,z,notes_instrument(x,y,z),n->value,127);
}
bool circuits_interact (PlayerData *p, int x, int y, int z) {
  Node *n = at(x,y,z);
  if (!n || !near(p,x,y,z) || (n->kind != CIRCUIT_LEVER && n->kind != CIRCUIT_NOTE && n->kind != CIRCUIT_WOOD_TRAPDOOR)) return false;
  if (trapdoor(n->kind) && n->power) return true;
  uint8_t old = n->value;
  n->value = n->kind == CIRCUIT_WOOD_TRAPDOOR ? (uint8_t)(n->value^8u) : n->kind == CIRCUIT_LEVER ? (uint8_t)(n->value^1u) : (uint8_t)((n->value+1u)%25u);
  if (!circuits_save()) { n->value = old; return false; }
  ctx->dirty = ctx->visual_dirty = true; broadcast(n); if (n->kind == CIRCUIT_NOTE) circuits_strike(x,y,z);
  return true;
}
bool circuits_place (PlayerData *p, int x, int y, int z, uint8_t face, uint16_t held) {
  uint8_t kind = held == I_redstone ? CIRCUIT_DUST : held == I_redstone_torch ? CIRCUIT_TORCH :
    held == I_lever ? CIRCUIT_LEVER : held == I_note_block ? CIRCUIT_NOTE :
    held == I_stone_pressure_plate ? CIRCUIT_STONE_PLATE : held == I_oak_pressure_plate ? CIRCUIT_WOOD_PLATE :
    held == I_oak_trapdoor ? CIRCUIT_WOOD_TRAPDOOR : held == I_iron_trapdoor ? CIRCUIT_IRON_TRAPDOOR : 0;
  if (kind == CIRCUIT_TORCH && face >= 2 && face <= 5) kind = CIRCUIT_WALL_TORCH;
  if (!kind || face > 5 || (kind != CIRCUIT_NOTE && !trapdoor(kind) && kind != CIRCUIT_WALL_TORCH && face != 1) || !near(p,x,y,z) || commands_gamemode(p) == 2) return false;
  static const int8_t step[6][3] = {{0,-1,0},{0,1,0},{0,0,-1},{0,0,1},{-1,0,0},{1,0,0}};
  x += step[face][0]; y += step[face][1]; z += step[face][2];
  if (!coords(x,y,z) || !near(p,x,y,z) || at(x,y,z) || !isReplaceableBlock(getBlockAt(x,y,z))) return false;
  if (kind == CIRCUIT_NOTE || trapdoor(kind)) for (int i = 0; i < MAX_PLAYERS; i++) if (plates_player_active(&player_data[i])) {
    const PlayerData *other = &player_data[i];
    if (other->client_fd >= 0 && !(other->flags&0x22) && other->x == x && other->z == z && (other->y == y || other->y+1 == y)) return false;
  }
  Node candidate = {(int16_t)x,(int16_t)z,(uint8_t)y,kind,0,kind == CIRCUIT_TORCH ? 15 : 0};
  if (kind == CIRCUIT_WALL_TORCH) { candidate.value = (uint8_t)(face-2); candidate.power = 15; }
  /* Side clicks place bottom halves; underside clicks place top halves. */
  if (trapdoor(kind)) candidate.value = (uint8_t)((face >= 2 ? face-2 : 0)+(face == 0 ? 4 : 0));
  if (!supported(&candidate)) return false;
  Node *n = NULL; for (unsigned i = 0; i < CIRCUIT_LIMIT; i++) if (!ctx->nodes[i].kind) { n = &ctx->nodes[i]; break; }
  if (!n) return false;
  uint8_t old = getBlockAt(x,y,z); *n = candidate; index_nodes(); ctx->changing = true;
  if (makeBlockChange((short)x,(uint8_t)y,(short)z,carrier(kind))) { n->kind = 0; index_nodes(); ctx->changing = false; return false; }
  if (!circuits_save()) {
    n->kind = 0; index_nodes(); makeBlockChange((short)x,(uint8_t)y,(short)z,old); ctx->changing = false; return false;
  }
  ctx->changing = false; ctx->dirty = ctx->visual_dirty = true; broadcast(n); return true;
}
void circuits_block_changed (int x, int y, int z, uint8_t block) {
  (void)block; if (ctx->changing) return;
  ctx->dirty = ctx->visual_dirty = true; bool saved = false; ctx->changing = true;
  for (unsigned i = 0; i < CIRCUIT_LIMIT; i++) {
    Node *n = &ctx->nodes[i]; if (!n->kind) continue;
    int sx,sy,sz; support(n,&sx,&sy,&sz);
    if (!(n->x == x && n->y == y && n->z == z) && !(sx == x && sy == y && sz == z)) continue;
    bool exists = getBlockAt(n->x,n->y,n->z) == carrier(n->kind);
    if (exists && supported(n)) continue;
    uint16_t drop = item(n->kind); n->kind = 0; index_nodes(); saved = true;
    if (exists && !makeBlockChange(n->x,n->y,n->z,B_air)) items_spawn(drop,1,n->x,n->y,n->z,500);
  }
  ctx->changing = false;
  if (saved && !circuits_save()) fputs("Could not save circuit removal.\n",stderr);
}
static bool occupied (const Node *n) {
  for (int i = 0; i < MAX_PLAYERS; i++) if (plates_player_active(&player_data[i])) {
    const PlayerData *p = &player_data[i];
    if (p->client_fd >= 0 && p->health && !(p->flags&0x22) && commands_gamemode(p) != 3 &&
        p->x == n->x && p->z == n->z && p->y == n->y) return true;
  }
  for (unsigned i = 0; i < MAX_MOBS; i++) {
    const MobData *m = &mob_data[i];
    if (m->type && (m->data&31) && m->x == n->x && m->z == n->z && m->y == n->y) return true;
  }
  if (n->kind == CIRCUIT_WOOD_PLATE) for (unsigned i = 0; i < ITEM_ENTITY_LIMIT; i++) {
    const DroppedItem *d = items_at(i);
    if (d->count && d->x == n->x && d->z == n->z && d->y >= n->y && d->y < (float)n->y+0.6f) return true;
  }
  return false;
}
void circuits_tick (void) {
  for (unsigned i = 0; i < CIRCUIT_LIMIT; i++) if (plate(ctx->nodes[i].kind) && occupied(&ctx->nodes[i]) != (ctx->nodes[i].power != 0)) ctx->dirty = true;
  if (!ctx->dirty) return;
  ctx->dirty = false; bool refresh = ctx->visual_dirty; ctx->visual_dirty = false; uint8_t before[CIRCUIT_LIMIT], next[CIRCUIT_LIMIT];
  for (unsigned i = 0; i < CIRCUIT_LIMIT; i++) {
    Node *n = &ctx->nodes[i]; before[i] = n->power; next[i] = n->power;
    if (n->kind == CIRCUIT_TORCH || n->kind == CIRCUIT_WALL_TORCH) {
      int x,y,z; support(n,&x,&y,&z); next[i] = incoming(x,y,z,n) ? 0 : 15;
    }
    if (plate(n->kind)) next[i] = occupied(n) ? 15 : 0;
    if (n->kind == CIRCUIT_LEVER) next[i] = n->value ? 15 : 0;
    if (n->kind == CIRCUIT_DUST) next[i] = 0;
  }
  for (unsigned i = 0; i < CIRCUIT_LIMIT; i++) ctx->nodes[i].power = next[i];
  /* Rebuild power from sources, so loops cannot sustain phantom power. */
  for (unsigned pass = 0; pass < 15; pass++) for (unsigned i = 0; i < CIRCUIT_LIMIT; i++) {
    Node *n = &ctx->nodes[i]; if (n->kind != CIRCUIT_DUST) continue;
    for (unsigned d = 0; d < 4; d++) {
      const Node *source = at(n->x+offsets[d][0],n->y,n->z+offsets[d][2]);
      if (!source || source->kind == CIRCUIT_NOTE || trapdoor(source->kind)) continue;
      uint8_t power = source->kind == CIRCUIT_DUST ? (source->power ? (uint8_t)(source->power-1) : 0) : source->power;
      if (power > n->power) n->power = power;
    }
  }
  bool save = false;
  for (unsigned i = 0; i < CIRCUIT_LIMIT; i++) {
    Node *n = &ctx->nodes[i]; if (!n->kind) continue;
    if (trapdoor(n->kind)) {
      n->power = incoming(n->x,n->y,n->z,n) ? 15 : 0;
      if (n->power != before[i] && (n->value&8u)) { n->value &= 7u; save = true; }
    }
    if (n->kind == CIRCUIT_NOTE) {
      n->power = incoming(n->x,n->y,n->z,n) ? 15 : 0;
      if (n->power && !before[i]) circuits_strike(n->x,n->y,n->z);
    }
    if (before[i] != n->power) ctx->dirty = true; /* Let inverter inputs settle next tick. */
    if (refresh || before[i] != n->power) broadcast(n);
  }
  if (save && !circuits_save()) fputs("Could not save trapdoor state.\n",stderr);
  doors_refresh_power();
}
static const uint8_t magic[8] = {'L','O','C','I','R','C',2,0};
bool circuits_save (void) {
  if (!ctx->save_path[0]) return true;
  char temp[260]; snprintf(temp,sizeof(temp),"%s.tmp",ctx->save_path);
  FILE *f = fopen(temp,"wb"); if (!f) return false;
  bool ok = fwrite(magic,1,8,f) == 8;
  for (unsigned i = 0; ok && i < CIRCUIT_LIMIT; i++) if (ctx->nodes[i].kind) {
    const Node *n = &ctx->nodes[i]; uint16_t x = (uint16_t)n->x, z = (uint16_t)n->z;
    uint8_t r[7] = {(uint8_t)(x>>8),(uint8_t)x,(uint8_t)(z>>8),(uint8_t)z,n->y,n->kind,n->value};
    ok = fwrite(r,1,7,f) == 7;
  }
  if (fclose(f)) ok = false;
  if (ok && rename(temp,ctx->save_path) == 0) return true;
  remove(temp); return false;
}
bool circuits_load (const char *path) {
  memset(ctx->nodes,0,sizeof(ctx->nodes)); index_nodes(); ctx->changing = false; ctx->dirty = ctx->visual_dirty = true; ctx->save_path[0] = 0;
  if (!path) return true;
  if (strlen(path) >= sizeof(ctx->save_path)) return false;
  memcpy(ctx->save_path,path,strlen(path)+1);
  FILE *f = fopen(path,"rb"); if (!f && errno != ENOENT) return false;
  bool ok = true; unsigned count = 0;
  if (f) {
    uint8_t h[8]; ok = fread(h,1,8,f) == 8 && !memcmp(h,magic,6) && (h[6] == 1 || h[6] == 2) && h[7] == 0;
    while (ok) {
      uint8_t r[7]; size_t len = fread(r,1,7,f);
      if (!len) { if (ferror(f)) ok = false; break; }
      if (len != 7 || count == CIRCUIT_LIMIT || r[5] < 1 || r[5] > (h[6] == 1 ? CIRCUIT_NOTE : CIRCUIT_IRON_TRAPDOOR) ||
          (r[5] != CIRCUIT_NOTE && !trapdoor(r[5]) && r[5] != CIRCUIT_WALL_TORCH && !r[4]) ||
          (r[5] == CIRCUIT_NOTE ? r[6] > 24 : r[5] == CIRCUIT_LEVER ? r[6] > 1 : r[5] == CIRCUIT_WALL_TORCH ? r[6] > 3 : trapdoor(r[5]) ? r[6] > (r[5] == CIRCUIT_WOOD_TRAPDOOR ? 15 : 7) : r[6] != 0)) { ok = false; break; }
      int x = (int)r[0]*256+r[1], z = (int)r[2]*256+r[3]; if (x >= 32768) x -= 65536; if (z >= 32768) z -= 65536;
      if (at(x,r[4],z)) { ok = false; break; }
      ctx->nodes[count++] = (Node){(int16_t)x,(int16_t)z,r[4],r[5],r[6],0}; index_nodes();
    }
    if (fclose(f)) ok = false;
  }
  if (!ok) { memset(ctx->nodes,0,sizeof(ctx->nodes)); index_nodes(); return false; }
  bool changed = false;
  for (unsigned i = 0; i < count; i++) if (getBlockAt(ctx->nodes[i].x,ctx->nodes[i].y,ctx->nodes[i].z) != carrier(ctx->nodes[i].kind) || !supported(&ctx->nodes[i])) {
    ctx->nodes[i].kind = 0; changed = true;
  }
  index_nodes();
  /* Adopt previously decorative torches, levers and note blocks in old worlds. */
  for (int i = 0; i < block_changes_count; i++) {
    const BlockChange *b = &block_changes[i]; if (b->block == B_chest) { i += 14; continue; }
    uint8_t kind = b->block == B_redstone_torch ? CIRCUIT_TORCH : b->block == B_lever ? CIRCUIT_LEVER : b->block == B_note_block ? CIRCUIT_NOTE : 0;
    if (!kind || at(b->x,b->y,b->z)) continue;
    unsigned slot = 0; while (slot < CIRCUIT_LIMIT && ctx->nodes[slot].kind) slot++;
    if (slot == CIRCUIT_LIMIT) return false;
    Node candidate = {b->x,b->z,b->y,kind,0,0};
    if (!supported(&candidate)) continue;
    ctx->nodes[slot] = candidate; index_nodes(); changed = true;
  }
  return !changed || circuits_save();
}
