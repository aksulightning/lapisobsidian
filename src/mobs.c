#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mobs.h"
#include "commands.h"
#include "doors.h"
#include "circuits.h"
#include "items.h"
#include "packets.h"
#include "procedures.h"
#include "protocol.h"
#include "registries.h"
#include "tools.h"
#include "worldgen.h"

_Static_assert(LAPIS_PROTOCOL_VERSION == 772, "Review mob and arrow entity IDs");
_Static_assert(MAX_PLAYERS <= 32, "Mob viewer masks hold at most 32 players");
_Static_assert(MAX_MOBS < 1022 && ITEM_ENTITY_LIMIT < 1024, "Entity ID ranges must not overlap");
static const MobType types[] = {
  {"chicken",MOB_CHICKEN,4},{"cow",MOB_COW,10},{"pig",MOB_PIG,10},{"sheep",MOB_SHEEP,8},
  {"zombie",MOB_ZOMBIE,20},{"skeleton",MOB_SKELETON,20},{"spider",MOB_SPIDER,16},{"creeper",MOB_CREEPER,20}
};
static struct {
  uint32_t viewers;
  uint16_t anger_ms, cooldown_ms, fuse_ms, sound_ms, walk_ms;
  int8_t target, step_x, step_y, step_z;
  uint8_t yaw;
} state[MAX_MOBS];
/* Visual offsets from the validated integer destination, in protocol units.
 * Raise before advancing up a step; advance before dropping down a step. */
static void walk_offset (size_t i, int16_t *x, int16_t *y, int16_t *z) {
  unsigned horizontal = state[i].walk_ms, vertical = horizontal;
  if (state[i].step_y < 0) {
    horizontal = horizontal <= 500 ? 0 : (horizontal-500)*2;
    vertical = vertical < 500 ? vertical*2 : 1000;
  } else if (state[i].step_y > 0) {
    horizontal = horizontal < 500 ? horizontal*2 : 1000;
    vertical = vertical <= 500 ? 0 : (vertical-500)*2;
  }
  *x = (int16_t)((int)state[i].step_x*(int)(1000-horizontal)*4096/1000);
  *y = (int16_t)((int)state[i].step_y*(int)(1000-vertical)*4096/1000);
  *z = (int16_t)((int)state[i].step_z*(int)(1000-horizontal)*4096/1000);
}
static void advance_walk (size_t i, unsigned elapsed_ms) {
  if (state[i].walk_ms >= 1000) return;
  int16_t ax,ay,az,bx,by,bz; walk_offset(i,&ax,&ay,&az);
  unsigned next = state[i].walk_ms+elapsed_ms;
  state[i].walk_ms = (uint16_t)(next > 1000 ? 1000 : next);
  walk_offset(i,&bx,&by,&bz);
  if (ax == bx && ay == by && az == bz) return;
  for (int p = 0; p < MAX_PLAYERS; p++) if (state[i].viewers&(UINT32_C(1)<<p))
    sc_mob_move(player_data[p].client_fd,-2-(int)i,(int16_t)(bx-ax),(int16_t)(by-ay),(int16_t)(bz-az),
      state[i].yaw,state[i].step_y == 0 || state[i].walk_ms == 1000);
}
void mobs_tick_movement (int64_t elapsed_us) {
  if (elapsed_us <= 0) return;
  unsigned elapsed = elapsed_us/1000 > 1000 ? 1000u : (unsigned)(elapsed_us/1000);
  for (size_t i = 0; i < MAX_MOBS; i++)
    if (mob_data[i].type && (mob_data[i].data&31)) advance_walk(i,elapsed);
}
typedef struct { float x,y,z,vx,vy,vz; uint32_t viewers; uint16_t age_ms; uint8_t used; } Arrow;
static Arrow arrows[MOB_ARROW_LIMIT];
static uint32_t ai_ms;
const MobType *mobs_by_name (const char *name) {
  if (name) for (size_t i = 0; i < sizeof(types)/sizeof(types[0]); i++) if (!strcmp(types[i].name,name)) return &types[i];
  return NULL;
}
static const MobType *by_type (uint8_t type) {
  for (size_t i = 0; i < sizeof(types)/sizeof(types[0]); i++) if (types[i].type == type) return &types[i];
  return NULL;
}
static bool loaded (const PlayerData *p) { return p->client_fd >= 0 && !(p->flags&0x22); }
static bool targetable (const PlayerData *p) { return loaded(p) && p->health && commands_gamemode(p) != 1 && commands_gamemode(p) != 3; }
static int index_of (const PlayerData *p) { for (int i = 0; i < MAX_PLAYERS; i++) if (p == &player_data[i]) return i; return -1; }
static bool coords (int x, int y, int z) { return x >= -32768 && x <= 32767 && z >= -32768 && z <= 32767 && y >= 0 && y <= 255; }
static bool clear (int x, int y, int z) {
  if (!coords(x,y,z)) return false;
  uint8_t b = getBlockAt((short)x,(uint8_t)y,(short)z);
  if (b == B_oak_door) { const Door *d = doors_at(x,y,z); return d && d->open; }
  if (b == B_oak_trapdoor || b == B_iron_trapdoor) return circuits_trapdoor_open(x,y,z);
  return isPassableSpawnBlock(b) != 0;
}
static bool space (uint8_t type, int x, int y, int z) {
  if (type == MOB_SPIDER) {
    /* A 1.4-wide spider needs clearance in the four neighboring cells too. */
    return clear(x,y,z) && clear(x-1,y,z) && clear(x+1,y,z) && clear(x,y,z-1) && clear(x,y,z+1);
  }
  return clear(x,y,z) && clear(x,y+1,z);
}
static bool near (const PlayerData *p, int x, int z) {
  return loaded(p) && abs(div_floor(p->x,16)-div_floor(x,16)) <= VIEW_DISTANCE && abs(div_floor(p->z,16)-div_floor(z,16)) <= VIEW_DISTANCE;
}
static void spawn_to (int fd, size_t i) {
  const MobData *m = &mob_data[i];
  uint8_t uuid[16] = {'L','a','p','i','s','M','o','b',0,0,0,0,0,0,0,0}; uuid[14] = (uint8_t)(i>>8); uuid[15] = (uint8_t)i;
  int16_t x,y,z; walk_offset(i,&x,&y,&z);
  sc_spawnEntity(fd,-2-(int)i,uuid,m->type,m->x+0.5+x/4096.0,m->y+y/4096.0,m->z+0.5+z/4096.0,state[i].yaw,0);
  if (state[i].yaw) sc_setHeadRotation(fd,-2-(int)i,state[i].yaw);
  broadcastMobMetadata(fd,-2-(int)i);
  if (m->type == MOB_SKELETON) sc_mob_equipment(fd,-2-(int)i);
  if (m->type == MOB_CREEPER) sc_creeper_fuse(fd,-2-(int)i,state[i].fuse_ms != 0);
}
static void arrow_spawn_to (int fd, size_t i) {
  const Arrow *a = &arrows[i];
  uint8_t uuid[16] = {'L','a','p','i','s','A','r','r','o','w',0,0,0,0,0,0}; uuid[14] = (uint8_t)(i>>8); uuid[15] = (uint8_t)i;
  sc_spawnEntity(fd,MOB_ARROW_BASE-(int)i,uuid,6,a->x,a->y,a->z,0,0);
  sc_arrow_metadata(fd,MOB_ARROW_BASE-(int)i);
}
void mobs_sync_player (PlayerData *p) {
  int slot = index_of(p); if (slot < 0) return;
  uint32_t mask = UINT32_C(1)<<slot;
  for (size_t i = 0; i < MAX_MOBS; i++) {
    const MobData *m = &mob_data[i];
    bool visible = m->type && (m->data&31) && near(p,m->x,m->z);
    if (visible && !(state[i].viewers&mask)) { spawn_to(p->client_fd,i); state[i].viewers |= mask; }
    else if (!visible && (state[i].viewers&mask) && (!m->type || (m->data&31) || !loaded(p))) {
      if (p->client_fd >= 0) sc_removeEntity(p->client_fd,-2-(int)i);
      state[i].viewers &= ~mask;
    }
  }
  for (size_t i = 0; i < MOB_ARROW_LIMIT; i++) {
    Arrow *a = &arrows[i]; bool visible = a->used && near(p,(int)floorf(a->x),(int)floorf(a->z));
    if (visible && !(a->viewers&mask)) { arrow_spawn_to(p->client_fd,i); a->viewers |= mask; }
    else if (!visible && (a->viewers&mask)) {
      if (p->client_fd >= 0) sc_removeEntity(p->client_fd,MOB_ARROW_BASE-(int)i);
      a->viewers &= ~mask;
    }
  }
}
void mobs_forget_player (PlayerData *p) {
  int slot = index_of(p); if (slot < 0) return;
  uint32_t mask = ~(UINT32_C(1)<<slot);
  for (size_t i = 0; i < MAX_MOBS; i++) {
    state[i].viewers &= mask;
    if (state[i].target == slot) { state[i].target = -1; state[i].anger_ms = 0; }
  }
  for (size_t i = 0; i < MOB_ARROW_LIMIT; i++) arrows[i].viewers &= mask;
}
static void remove_mob (size_t i) {
  for (int p = 0; p < MAX_PLAYERS; p++) if (state[i].viewers&(UINT32_C(1)<<p)) sc_removeEntity(player_data[p].client_fd,-2-(int)i);
  memset(&mob_data[i],0,sizeof(mob_data[i])); memset(&state[i],0,sizeof(state[i])); state[i].target = -1;
}
static void remove_arrow (size_t i) {
  for (int p = 0; p < MAX_PLAYERS; p++) if (arrows[i].viewers&(UINT32_C(1)<<p)) sc_removeEntity(player_data[p].client_fd,MOB_ARROW_BASE-(int)i);
  memset(&arrows[i],0,sizeof(arrows[i]));
}
void mobs_clear (void) {
  for (size_t i = 0; i < MAX_MOBS; i++) remove_mob(i);
  for (size_t i = 0; i < MOB_ARROW_LIMIT; i++) remove_arrow(i);
  ai_ms = 0;
}
bool mobs_spawn (uint8_t type, int x, int y, int z) {
  const MobType *definition = by_type(type);
  if (!definition || !coords(x,y,z) || y < 1 || y > 253 || !space(type,x,y,z) || clear(x,y-1,z)) return false;
  uint8_t floor = getBlockAt((short)x,(uint8_t)(y-1),(short)z);
  if (isPassableBlock(floor) || floor == B_oak_door) return false;
  for (int p = 0; p < MAX_PLAYERS; p++) if (loaded(&player_data[p]) && player_data[p].x == x && player_data[p].z == z && abs(y-player_data[p].y) < 2) return false;
  int slot = -1;
  for (int i = 0; i < MAX_MOBS; i++) {
    if (!mob_data[i].type) { if (slot < 0) slot = i; continue; }
    if (abs(mob_data[i].x-x) < (type == MOB_SPIDER || mob_data[i].type == MOB_SPIDER ? 2 : 1) &&
        abs(mob_data[i].z-z) < (type == MOB_SPIDER || mob_data[i].type == MOB_SPIDER ? 2 : 1) && abs((int)mob_data[i].y-y) < 2) return false;
  }
  if (slot < 0) return false;
  memset(&state[slot],0,sizeof(state[slot])); state[slot].target = -1; state[slot].walk_ms = 1000;
  mob_data[slot] = (MobData){type,(short)x,(uint8_t)y,(short)z,definition->health};
  for (int p = 0; p < MAX_PLAYERS; p++) mobs_sync_player(&player_data[p]);
  return true;
}
/* Retain the inherited API for embedders; health is defined by the supported type. */
void spawnMob (uint8_t type, short x, uint8_t y, short z, uint8_t health) { (void)health; mobs_spawn(type,x,y,z); }
void mobs_spawn_exploration (int cx, int cz, int dx, int dz, int y, uint32_t r) {
  if ((r&3u) || cx < -2048 || cx > 2047 || cz < -2048 || cz > 2047) return;
  int x = (cx+(dx < 0 ? -VIEW_DISTANCE : dx > 0 ? VIEW_DISTANCE : 0))*16+(int)((r>>4)&15u);
  int z = (cz+(dz < 0 ? -VIEW_DISTANCE : dz > 0 ? VIEW_DISTANCE : 0))*16+(int)((r>>8)&15u);
  if (x < -32768 || x > 32767 || z < -32768 || z > 32767) return;
  int start = y > 8 ? y-8 : 1;
  for (int at = start; at <= 253; at++) {
    if (!clear(x,at,z) || !clear(x,at+1,z) || clear(x,at-1,z)) continue;
    bool day = world_time < 13000 || world_time > 23460;
    static const uint8_t passive[] = {MOB_CHICKEN,MOB_COW,MOB_PIG,MOB_SHEEP};
    static const uint8_t other[] = {MOB_ZOMBIE,MOB_SKELETON,MOB_SPIDER,MOB_CREEPER};
    mobs_spawn(day && at > 48 ? passive[(r>>12)&3u] : other[(r>>12)&3u],x,at,z); return;
  }
}
void mobs_attacked (int id, PlayerData *attacker) {
  if (id > -2 || id < -1-MAX_MOBS || !attacker || !targetable(attacker)) return;
  int index = index_of(attacker); if (index < 0) return;
  size_t i = (size_t)(-id-2);
  if (mob_data[i].type == MOB_SPIDER) { state[i].target = (int8_t)index; state[i].anger_ms = 15000; }
}
static void sound (const MobData *m, const char *name) {
  for (int p = 0; p < MAX_PLAYERS; p++) if (loaded(&player_data[p]) && abs(player_data[p].x-m->x) <= 32 && abs(player_data[p].z-m->z) <= 32 && abs(player_data[p].y-m->y) <= 32)
    sc_mob_sound_category(player_data[p].client_fd,name,m->x,m->y,m->z,
      m->type == MOB_COW || m->type == MOB_PIG || m->type == MOB_SHEEP || m->type == MOB_CHICKEN || m->type == MOB_SPIDER ? 6 : 5);
}
static void voice (const MobData *m, const char *event) {
  const MobType *type = by_type(m->type); if (!type) return;
  char name[64]; snprintf(name,sizeof(name),"minecraft:entity.%s.%s",type->name,event); sound(m,name);
}
void mobs_hurt_sound (int id, bool death) {
  if (id > -2 || id < -1-MAX_MOBS) return;
  const MobData *m = &mob_data[-id-2]; if (m->type && (m->data&31)) voice(m,death ? "death" : "hurt");
}
static void fuse (size_t i, bool active) {
  for (int p = 0; p < MAX_PLAYERS; p++) if (state[i].viewers&(UINT32_C(1)<<p)) sc_creeper_fuse(player_data[p].client_fd,-2-(int)i,active);
}
static bool sight (const MobData *m, const PlayerData *p) {
  float dx = (float)p->x-m->x, dy = (float)p->y-m->y, dz = (float)p->z-m->z;
  int steps = (int)(fmaxf(fabsf(dx),fmaxf(fabsf(dy),fabsf(dz)))*4.0f)+1;
  if (steps > 129) return false;
  for (int n = 1; n <= steps; n++) {
    float t = (float)n/(float)steps;
    if (!clear((int)floorf((float)m->x+0.5f+dx*t),(int)floorf((float)m->y+0.8f+dy*t),(int)floorf((float)m->z+0.5f+dz*t))) return false;
  }
  return true;
}
static void shoot (size_t mob, const PlayerData *p) {
  const MobData *m = &mob_data[mob];
  for (size_t i = 0; i < MOB_ARROW_LIMIT; i++) if (!arrows[i].used) {
    float dx = (float)p->x-m->x, dz = (float)p->z-m->z;
    float distance = sqrtf(dx*dx+dz*dz); if (distance < 0.1f) return;
    float vy = ((float)p->y+0.8f-((float)m->y+1.4f))*12.0f/distance + distance*0.12f;
    if (vy < -8) vy = -8;
    if (vy > 8) vy = 8;
    arrows[i] = (Arrow){.x=(float)m->x+0.5f,.y=(float)m->y+1.4f,.z=(float)m->z+0.5f,
      .vx=dx/distance*12.0f,.vy=vy,.vz=dz/distance*12.0f,.used=1};
    for (int player = 0; player < MAX_PLAYERS; player++) mobs_sync_player(&player_data[player]);
    uint8_t yaw = (uint8_t)((int)(atan2f(-dx,dz)*40.74367f)&255); state[mob].yaw = yaw;
    for (int player = 0; player < MAX_PLAYERS; player++) if (state[mob].viewers&(UINT32_C(1)<<player)) {
      sc_updateEntityRotation(player_data[player].client_fd,-2-(int)mob,yaw,0);
      sc_setHeadRotation(player_data[player].client_fd,-2-(int)mob,yaw);
    }
    sound(m,"minecraft:entity.skeleton.shoot"); return;
  }
}
size_t mobs_arrow_count (void) { size_t n = 0; for (size_t i = 0; i < MOB_ARROW_LIMIT; i++) n += arrows[i].used != 0; return n; }
void mobs_tick_arrows (int64_t elapsed_us) {
  if (elapsed_us <= 0) return;
  uint32_t elapsed = elapsed_us/1000 > 5000 ? 5000 : (uint32_t)(elapsed_us/1000);
  for (size_t i = 0; i < MOB_ARROW_LIMIT; i++) {
    Arrow *a = &arrows[i]; if (!a->used) continue;
    if ((unsigned)a->age_ms+elapsed >= 5000) { remove_arrow(i); continue; }
    a->age_ms = (uint16_t)(a->age_ms+elapsed);
    uint32_t remaining = elapsed > 1000 ? 1000 : elapsed;
    while (remaining && a->used) {
      uint32_t step = remaining > 25 ? 25 : remaining; remaining -= step;
      float dt = (float)step/1000.0f;
      a->vy -= 4.0f*dt; if (a->vy < -16) a->vy = -16;
      a->x += a->vx*dt; a->y += a->vy*dt; a->z += a->vz*dt;
      int x = (int)floorf(a->x), y = (int)floorf(a->y), z = (int)floorf(a->z);
      if (!clear(x,y,z)) { remove_arrow(i); break; }
      for (int p = 0; p < MAX_PLAYERS; p++) {
        PlayerData *target = &player_data[p];
        if (!targetable(target) || fabsf(a->x-((float)target->x+0.5f)) > 0.65f ||
            fabsf(a->z-((float)target->z+0.5f)) > 0.65f || a->y < target->y || a->y > (float)target->y+1.8f) continue;
        hurtEntity(target->client_fd,-1,D_arrow,4); remove_arrow(i); break;
      }
    }
    if (!a->used) continue;
    for (int p = 0; p < MAX_PLAYERS; p++) if (a->viewers&(UINT32_C(1)<<p)) {
      float yaw = atan2f(-a->vx,a->vz)*57.29578f, pitch = atan2f(a->vy,12.0f)*57.29578f;
      sc_teleportEntity(player_data[p].client_fd,MOB_ARROW_BASE-(int)i,a->x,a->y,a->z,yaw,pitch);
    }
  }
}
static bool walk (size_t i, int x, int z) {
  MobData *m = &mob_data[i]; int y = m->y;
  if (x < -32768 || x > 32767 || z < -32768 || z > 32767) return false;
  if (!space(m->type,x,y,z)) {
    if (y >= 253 || !space(m->type,x,y+1,z) || !space(m->type,m->x,y+1,m->z)) return false;
    y++;
  } else if (y > 0 && clear(x,y-1,z)) y--;
  if (!space(m->type,x,y,z)) return false;
  for (size_t j = 0; j < MAX_MOBS; j++) {
    if (j == i || !mob_data[j].type || !(mob_data[j].data&31)) continue;
    int radius = m->type == MOB_SPIDER || mob_data[j].type == MOB_SPIDER ? 2 : 1;
    if (abs(mob_data[j].x-x) < radius && abs(mob_data[j].z-z) < radius && abs(mob_data[j].y-y) < 2) return false;
  }
  for (int p = 0; p < MAX_PLAYERS; p++) if (loaded(&player_data[p]) && player_data[p].x == x && player_data[p].z == z && abs(player_data[p].y-y) < 2) return false;
  if (x == m->x && z == m->z && y == m->y) return true;
  float yaw = atan2f((float)m->x-(float)x,(float)z-(float)m->z)*57.29578f;
  state[i].step_x = (int8_t)(m->x-x); state[i].step_y = (int8_t)(m->y-y); state[i].step_z = (int8_t)(m->z-z);
  state[i].walk_ms = 0; state[i].yaw = (uint8_t)((int)(yaw*256.0f/360.0f)&255);
  m->x = (short)x; m->y = (uint8_t)y; m->z = (short)z;
  for (int p = 0; p < MAX_PLAYERS; p++) if (state[i].viewers&(UINT32_C(1)<<p)) {
    sc_updateEntityRotation(player_data[p].client_fd,-2-(int)i,state[i].yaw,0);
    sc_setHeadRotation(player_data[p].client_fd,-2-(int)i,state[i].yaw);
  }
  return true;
}
void mobs_tick (int64_t elapsed_us) {
  if (elapsed_us <= 0) return;
  uint32_t elapsed = elapsed_us/1000 > 1000 ? 1000 : (uint32_t)(elapsed_us/1000);
  ai_ms += elapsed; if (ai_ms < 1000) return; ai_ms -= 1000;
  for (int p = 0; p < MAX_PLAYERS; p++) mobs_sync_player(&player_data[p]);
  for (size_t i = 0; i < MAX_MOBS; i++) {
    MobData *m = &mob_data[i]; if (!m->type) continue;
    if (!(m->data&31)) { remove_mob(i); continue; }
    /* Finish the previous step before the next AI decision, including after lag. */
    advance_walk(i,1000);
    int nearest = -1, distance = INT32_MAX, present = INT32_MAX;
    for (int p = 0; p < MAX_PLAYERS; p++) if (loaded(&player_data[p])) {
      int d = abs(player_data[p].x-m->x)+abs(player_data[p].z-m->z)+abs(player_data[p].y-m->y);
      if (d < present) present = d;
      if (targetable(&player_data[p]) && d < distance) { nearest = p; distance = d; }
    }
    if (present > MOB_DESPAWN_DISTANCE) { remove_mob(i); continue; }
    /* Staggered voices use their own timer and never perturb gameplay RNG. */
    if (state[i].sound_ms < 1000) state[i].sound_ms = (uint16_t)(7000u+(i%6u)*1000u);
    else {
      state[i].sound_ms -= 1000;
      if (!state[i].sound_ms && m->type != MOB_CREEPER && present <= 32) voice(m,"ambient");
    }
    if (state[i].cooldown_ms >= 1000) state[i].cooldown_ms -= 1000; else state[i].cooldown_ms = 0;
    bool day = world_time < 13000 || world_time > 23460;
    if ((m->type == MOB_ZOMBIE || m->type == MOB_SKELETON) && day) {
      bool sky = true;
      for (int y = m->y+2; y <= 255; y++) if (!clear(m->x,y,m->z)) { sky = false; break; }
      if (sky) { hurtEntity(-2-(int)i,-1,D_on_fire,2); if (!(m->data&31)) continue; }
    }
    if (m->type == MOB_SPIDER) {
      int target = state[i].target;
      if (state[i].anger_ms <= 1000 || target < 0 || target >= MAX_PLAYERS || !targetable(&player_data[target])) {
        state[i].anger_ms = 0; state[i].target = -1; nearest = -1;
      } else { state[i].anger_ms -= 1000; nearest = target; distance = abs(player_data[target].x-m->x)+abs(player_data[target].z-m->z)+abs(player_data[target].y-m->y); }
    }
    bool hostile = m->type == MOB_ZOMBIE || m->type == MOB_SKELETON || m->type == MOB_CREEPER || (m->type == MOB_SPIDER && nearest >= 0);
    if (!hostile || nearest < 0 || distance > 24) {
      if (state[i].fuse_ms) { state[i].fuse_ms = 0; fuse(i,false); }
      uint32_t r = fast_rand();
      if ((m->data>>6) || (r&3u) == 0) {
        int dx = (r&4u) ? ((r&8u) ? 1 : -1) : 0;
        int dz = dx ? 0 : ((r&8u) ? 1 : -1); walk(i,m->x+dx,m->z+dz);
      }
      if (m->data>>6) m->data -= 64;
      continue;
    }
    PlayerData *p = &player_data[nearest]; bool visible = sight(m,p);
    if (m->type == MOB_CREEPER) {
      if (distance <= 3 && visible) {
        if (!state[i].fuse_ms) { state[i].fuse_ms = 2000; fuse(i,true); sound(m,"minecraft:entity.creeper.primed"); }
        else if (state[i].fuse_ms <= 1000) {
          sound(m,"minecraft:entity.firework_rocket.blast");
          for (int v = 0; v < MAX_PLAYERS; v++) if (state[i].viewers&(UINT32_C(1)<<v)) sc_firecracker(player_data[v].client_fd,m->x,m->y,m->z);
          remove_mob(i);
        } else state[i].fuse_ms -= 1000;
        continue;
      }
      if (state[i].fuse_ms) { state[i].fuse_ms = 0; fuse(i,false); }
    } else if (m->type == MOB_SKELETON && distance <= 16 && visible) {
      if (!state[i].cooldown_ms) { shoot(i,p); state[i].cooldown_ms = 2000; }
      continue;
    } else if (m->type != MOB_SKELETON && distance <= 2 && visible) {
      if (!state[i].cooldown_ms) { hurtEntity(p->client_fd,-2-(int)i,D_generic,m->type == MOB_SPIDER ? 2 : 6); state[i].cooldown_ms = 1000; }
      continue;
    }
    /* Cardinal steps avoid clipping through diagonal corners; no pathfinding heap. */
    int dx = p->x > m->x ? 1 : p->x < m->x ? -1 : 0;
    int dz = p->z > m->z ? 1 : p->z < m->z ? -1 : 0;
    if (abs(p->x-m->x) >= abs(p->z-m->z)) {
      if (!dx || !walk(i,m->x+dx,m->z)) if (dz) walk(i,m->x,m->z+dz);
    } else if (!dz || !walk(i,m->x,m->z+dz)) { if (dx) walk(i,m->x+dx,m->z); }
  }
}
