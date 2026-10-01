#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "farming.h"
#include "commands.h"
#include "items.h"
#include "packets.h"
#include "procedures.h"
#include "protocol.h"
#include "registries.h"
#include "worldgen.h"

_Static_assert(LAPIS_PROTOCOL_VERSION == 772,"Review farmland and wheat states");
/* y is the soil, age=8 means no crop. Four slots per 100 ms bounds water scans;
 * five hydrated visits per stage gives about 32 seconds between growth stages. */
typedef struct { int16_t x,z; uint8_t y,used,age,wet,steps; } Plot;
static Plot plots[FARM_LIMIT];
static char save_path[256];
static bool changing;
static uint32_t elapsed_ms;
static unsigned cursor;
static bool coords (int x, int y, int z) { return x >= -32768 && x <= 32767 && z >= -32768 && z <= 32767 && y >= 0 && y < 255; }
static Plot *at (int x, int y, int z) {
  if (!coords(x,y,z)) return NULL;
  for (unsigned i = 0; i < FARM_LIMIT; i++) if (plots[i].used && plots[i].x == x && plots[i].y == y && plots[i].z == z) return &plots[i];
  return NULL;
}
static bool water (const Plot *p) {
  for (int x = p->x-4; x <= p->x+4; x++) for (int z = p->z-4; z <= p->z+4; z++) {
    if (!coords(x,p->y,z)) continue;
    for (int y = p->y; y <= p->y+1; y++) {
      uint8_t b = getBlockAt(x,y,z); if (b >= B_water && b < B_water+8) return true;
    }
  }
  return false;
}
static void broadcast (const Plot *p) {
  for (int i = 0; i < MAX_PLAYERS; i++) if (player_data[i].client_fd >= 0 && !(player_data[i].flags&0x22)) {
    sc_blockUpdate(player_data[i].client_fd,p->x,p->y,p->z,getBlockAt(p->x,p->y,p->z));
    sc_blockUpdate(player_data[i].client_fd,p->x,p->y+1,p->z,getBlockAt(p->x,p->y+1,p->z));
  }
}
bool farming_state_at (int x, int y, int z, uint8_t block, uint16_t *state) {
  if (!state) return false;
  const Plot *p = at(x,block == B_wheat ? y-1 : y,z);
  if (!p) return false;
  if (block == B_farmland) *state = (uint16_t)(4350+(p->wet ? 7 : 0));
  else if (block == B_wheat && p->age < 8) *state = (uint16_t)(4342+p->age);
  else return false;
  return true;
}
void farming_send_chunk (int fd, int cx, int cz) {
  if (cx < -2048 || cx > 2047 || cz < -2048 || cz > 2047) return;
  for (unsigned i = 0; i < FARM_LIMIT; i++) {
    const Plot *p = &plots[i];
    if (!p->used || p->x < cx*16 || p->x >= cx*16+16 || p->z < cz*16 || p->z >= cz*16+16) continue;
    sc_blockUpdate(fd,p->x,p->y,p->z,getBlockAt(p->x,p->y,p->z));
    if (p->age < 8) sc_blockUpdate(fd,p->x,p->y+1,p->z,getBlockAt(p->x,p->y+1,p->z));
  }
}
static bool hoe (uint16_t item) {
  return item == I_wooden_hoe || item == I_stone_hoe || item == I_iron_hoe || item == I_golden_hoe || item == I_diamond_hoe || item == I_netherite_hoe;
}
bool farming_use (PlayerData *p, int x, int y, int z, uint8_t face) {
  if (!p || p->hotbar >= 41) return false;
  uint16_t held = p->inventory_items[p->hotbar];
  if (!hoe(held) && held != I_wheat_seeds) return false;
  if (p->client_fd < 0 || !p->health || (p->flags&0x22) || commands_gamemode(p) >= 2 ||
      !p->inventory_count[p->hotbar] || face != 1 || !coords(x,y,z) ||
      abs(x-p->x) > 6 || abs(y-p->y) > 6 || abs(z-p->z) > 6) return true;
  uint8_t soil = getBlockAt(x,y,z), above = getBlockAt(x,y+1,z);
  bool till = hoe(held);
  if (above != B_air || (till ? soil != B_dirt && soil != B_grass_block && soil != B_snowy_grass_block : soil != B_farmland)) return true;
  Plot *plot = at(x,y,z);
  if (!plot) for (unsigned i = 0; i < FARM_LIMIT; i++) if (!plots[i].used) { plot = &plots[i]; break; }
  if (!plot) { sc_systemChat(p->client_fd,"Farm plot limit reached.",24); return true; }
  Plot old = *plot;
  *plot = (Plot){.x=(int16_t)x,.z=(int16_t)z,.y=(uint8_t)y,.used=1,.age=till ? 8 : 0}; plot->wet = water(plot);
  changing = true;
  int by = till ? y : y+1;
  if (makeBlockChange((short)x,(uint8_t)by,(short)z,till ? B_farmland : B_wheat)) { *plot = old; changing = false; return true; }
  if (!farming_save()) {
    *plot = old; makeBlockChange((short)x,(uint8_t)by,(short)z,till ? soil : above); changing = false; return true;
  }
  changing = false;
  if (commands_gamemode(p) != 1) {
    if (till) bumpToolDurability(p);
    else { p->inventory_count[p->hotbar]--; if (!p->inventory_count[p->hotbar]) p->inventory_items[p->hotbar] = 0; }
  }
  sc_setContainerSlot(p->client_fd,0,serverSlotToClientSlot(0,p->hotbar),p->inventory_count[p->hotbar],p->inventory_items[p->hotbar]);
  broadcast(plot); return true;
}
bool farming_harvest (PlayerData *p, int x, int y, int z) {
  Plot *plot = at(x,y-1,z);
  if (!plot || plot->age == 8 || getBlockAt(x,y,z) != B_wheat) return false;
  bool drops = !p || commands_gamemode(p) != 1, mature = plot->age == 7;
  if (drops && !(mature ? items_can_spawn_pair(I_wheat,1,I_wheat_seeds,2,x,y,z) : items_can_spawn(I_wheat_seeds,1,x,y,z))) {
    if (p) sc_blockUpdate(p->client_fd,x,y,z,B_wheat);
    return true;
  }
  Plot old = *plot; plot->age = 8; plot->steps = 0;
  changing = true;
  if (makeBlockChange((short)x,(uint8_t)y,(short)z,B_air)) { *plot = old; changing = false; return true; }
  if (!farming_save()) {
    *plot = old; makeBlockChange((short)x,(uint8_t)y,(short)z,B_wheat); changing = false; return true;
  }
  changing = false;
  if (drops) {
    items_spawn(I_wheat_seeds,mature ? 2 : 1,x,y,z,500);
    if (mature) items_spawn(I_wheat,1,x,y,z,500);
  }
  return true;
}
void farming_block_changed (int x, int y, int z) {
  if (changing) return;
  Plot *p = at(x,y,z); if (!p) p = at(x,y-1,z); if (!p) return;
  bool changed = false;
  if (getBlockAt(p->x,p->y,p->z) != B_farmland) {
    if (p->age < 8 && getBlockAt(p->x,p->y+1,p->z) == B_wheat) {
      farming_harvest(NULL,p->x,p->y+1,p->z);
      if (p->age < 8) return; /* Retry when the item pool or storage has room. */
    }
    p->used = 0; changed = true;
  } else if (p->age < 8 && getBlockAt(p->x,p->y+1,p->z) != B_wheat) { p->age = 8; p->steps = 0; changed = true; }
  if (changed && !farming_save()) fputs("Could not save farm removal.\n",stderr);
}
void farming_tick (int64_t elapsed_us) {
  if (elapsed_us <= 0) return;
  elapsed_ms += elapsed_us/1000 > 1000 ? 1000u : (uint32_t)(elapsed_us/1000);
  while (elapsed_ms >= 100) {
    elapsed_ms -= 100;
    for (unsigned i = 0; i < 4; i++) {
      Plot *p = &plots[cursor]; cursor = (cursor+1)%FARM_LIMIT;
      if (!p->used) continue;
      farming_block_changed(p->x,p->y,p->z); if (!p->used) continue;
      Plot old = *p; p->wet = water(p);
      if (p->wet && p->age < 7 && getBlockAt(p->x,p->y,p->z) == B_farmland && getBlockAt(p->x,p->y+1,p->z) == B_wheat && ++p->steps == 5) {
        p->steps = 0; p->age++;
        if (!farming_save()) { *p = old; fputs("Could not save crop growth.\n",stderr); }
      }
      if (p->wet != old.wet || p->age != old.age) broadcast(p);
    }
  }
}
static const uint8_t magic[8] = {'L','O','F','A','R','M',1,0};
bool farming_save (void) {
  if (!save_path[0]) return true;
  char temp[260]; snprintf(temp,sizeof(temp),"%s.tmp",save_path);
  FILE *f = fopen(temp,"wb"); if (!f) return false;
  bool ok = fwrite(magic,1,8,f) == 8;
  for (unsigned i = 0; ok && i < FARM_LIMIT; i++) if (plots[i].used) {
    const Plot *p = &plots[i]; uint16_t x = (uint16_t)p->x, z = (uint16_t)p->z;
    uint8_t r[7] = {(uint8_t)(x>>8),(uint8_t)x,(uint8_t)(z>>8),(uint8_t)z,p->y,p->age,p->steps};
    ok = fwrite(r,1,7,f) == 7;
  }
  if (fclose(f)) ok = false;
  if (ok && rename(temp,save_path) == 0) return true;
  remove(temp); return false;
}
bool farming_load (const char *path) {
  memset(plots,0,sizeof(plots)); save_path[0] = 0; changing = false; cursor = 0; elapsed_ms = 0;
  if (!path) return true;
  if (strlen(path) >= sizeof(save_path)) return false;
  memcpy(save_path,path,strlen(path)+1);
  FILE *f = fopen(path,"rb"); if (!f && errno != ENOENT) return false;
  bool ok = true; unsigned count = 0;
  if (f) {
    uint8_t h[8]; ok = fread(h,1,8,f) == 8 && !memcmp(h,magic,8);
    while (ok) {
      uint8_t r[7]; size_t n = fread(r,1,7,f);
      if (!n) { if (ferror(f)) ok = false; break; }
      if (n != 7 || count == FARM_LIMIT || r[4] == 255 || r[5] > 8 || r[6] > 4 || (r[5] >= 7 && r[6])) { ok = false; break; }
      int x = r[0]*256+r[1], z = r[2]*256+r[3]; if (x >= 32768) x -= 65536; if (z >= 32768) z -= 65536;
      if (at(x,r[4],z)) { ok = false; break; }
      plots[count++] = (Plot){.x=(int16_t)x,.z=(int16_t)z,.y=r[4],.used=1,.age=r[5],.steps=r[6]};
    }
    if (fclose(f)) ok = false;
  }
  if (!ok) { memset(plots,0,sizeof(plots)); return false; }
  for (unsigned i = 0; i < count; i++) {
    Plot *p = &plots[i]; farming_block_changed(p->x,p->y,p->z);
    if (p->used) p->wet = water(p);
  }
  /* Adopt farmland from older worlds without modifying the compact palette. */
  for (int i = 0; i < block_changes_count; i++) {
    const BlockChange *b = &block_changes[i]; if (b->block == B_chest) { i += 14; continue; }
    if (b->block != B_farmland || b->y == 255 || at(b->x,b->y,b->z)) continue;
    unsigned slot = 0; while (slot < FARM_LIMIT && plots[slot].used) slot++;
    if (slot == FARM_LIMIT) return false;
    plots[slot] = (Plot){.x=b->x,.z=b->z,.y=b->y,.used=1,.age=getBlockAt(b->x,b->y+1,b->z) == B_wheat ? 0 : 8};
    plots[slot].wet = water(&plots[slot]);
  }
  return farming_save();
}
