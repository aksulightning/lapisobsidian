#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "doors.h"
#include "circuits.h"
#include "commands.h"
#include "packets.h"
#include "procedures.h"
#include "protocol.h"
#include "registries.h"
#include "worldgen.h"

/* Minimal maintained state snapshot: minecraft-data revision
 * f5d7d74604d8c6153fd086bfe035e0630a5207cc, pc/1.21.8/blocks.json.
 * Properties: facing N/S/W/E, half upper/lower, hinge left/right,
 * open true/false, powered true/false. Only left, unpowered oak is used. */
_Static_assert(LAPIS_PROTOCOL_VERSION == 772, "Update oak door states for this protocol");
static const uint16_t facing_base[4] = {4686,4702,4718,4734};
static Door doors[DOOR_LIMIT];
static bool powered[DOOR_LIMIT];
static bool changing;
static char save_path[256];
static bool coords (int x, int y, int z) {
  return x >= -32768 && x <= 32767 && z >= -32768 && z <= 32767 && y >= 0 && y <= 255;
}
static bool near (const PlayerData *p, int x, int y, int z, bool placing) {
  if (!p || p->client_fd < 0 || (p->flags&0x22) || !p->health ||
      commands_gamemode(p) == 3 || (placing && commands_gamemode(p) == 2)) return false;
  double dx = (double)p->x-x, dy = (double)p->y-y, dz = (double)p->z-z;
  return dx*dx+dy*dy+dz*dz <= 64;
}
const Door *doors_at (int x, int y, int z) {
  if (!coords(x,y,z)) return NULL;
  for (unsigned i = 0; i < DOOR_LIMIT; i++) {
    const Door *d = &doors[i];
    if (d->used && d->x == x && d->z == z && (d->y == y || d->y+1 == y)) return d;
  }
  return NULL;
}
bool doors_state_at (int x, int y, int z, uint16_t *state) {
  const Door *d = doors_at(x,y,z);
  if (!d || !state || d->facing > 3 || d->open > 1) return false;
  *state = (uint16_t)(facing_base[d->facing]+(d->y == y ? 8u : 0u)+(d->open ? 0u : 2u)+(powered[d-doors] ? 0u : 1u));
  return true;
}
static bool support (int x, int y, int z) {
  if (!coords(x,y,z)) return false;
  uint8_t block = getBlockAt(x,y,z);
  return block != B_oak_door && block != B_iron_door && !isPassableBlock(block);
}
static bool complete (const Door *d) {
  return getBlockAt(d->x,d->y,d->z) == B_oak_door && getBlockAt(d->x,d->y+1,d->z) == B_oak_door && support(d->x,d->y-1,d->z);
}
static void send_door (int fd, const Door *d) {
  sc_blockUpdate(fd,d->x,d->y,d->z,B_oak_door);
  sc_blockUpdate(fd,d->x,d->y+1,d->z,B_oak_door);
}
static void broadcast (const Door *d) {
  for (int i = 0; i < MAX_PLAYERS; i++) if (player_data[i].client_fd >= 0 && !(player_data[i].flags&0x22)) send_door(player_data[i].client_fd,d);
}
bool doors_place (PlayerData *p, int x, int y, int z, uint8_t face) {
  /* Beta-style doors are placed on a block's top surface. */
  if (!coords(x,y,z) || face != 1 || y > 253 || !near(p,x,y,z,true) || !support(x,y,z)) return false;
  y++;
  if (!near(p,x,y+1,z,true) || !isReplaceableBlock(getBlockAt(x,y,z)) || !isReplaceableBlock(getBlockAt(x,y+1,z)) ||
      doors_at(x,y,z) || doors_at(x,y+1,z)) return false;
  for (int i = 0; i < MAX_PLAYERS; i++) {
    const PlayerData *other = &player_data[i];
    if (other->client_fd >= 0 && !(other->flags&0x22) && other->x == x && other->z == z && other->y <= y+1 && other->y+1 >= y) return false;
  }
  Door *d = NULL;
  for (unsigned i = 0; i < DOOR_LIMIT; i++) if (!doors[i].used) { d = &doors[i]; break; }
  if (!d) { sc_systemChat(p->client_fd,"Door storage limit reached.",27); return false; }
  uint8_t lower = getBlockAt(x,y,z), upper = getBlockAt(x,y+1,z);
  changing = true;
  if (makeBlockChange((short)x,(uint8_t)y,(short)z,B_oak_door)) { changing = false; return false; }
  if (makeBlockChange((short)x,(uint8_t)(y+1),(short)z,B_oak_door)) {
    makeBlockChange((short)x,(uint8_t)y,(short)z,lower); changing = false; return false;
  }
  static const uint8_t yaw_facing[4] = {1,2,0,3};
  *d = (Door){(int16_t)x,(int16_t)z,(uint8_t)y,yaw_facing[(((unsigned)(uint8_t)p->yaw+32u)/64u)&3u],0,1};
  if (!doors_save()) {
    powered[d-doors] = false; d->used = 0;
    makeBlockChange((short)x,(uint8_t)y,(short)z,lower);
    makeBlockChange((short)x,(uint8_t)(y+1),(short)z,upper);
    changing = false; return false;
  }
  changing = false; broadcast(d); return true;
}
bool doors_interact (PlayerData *p, int x, int y, int z) {
  if (!coords(x,y,z) || !near(p,x,y,z,false)) return false;
  for (unsigned i = 0; i < DOOR_LIMIT; i++) {
    Door *d = &doors[i];
    if (!d->used || d->x != x || d->z != z || (d->y != y && d->y+1 != y)) continue;
    if (!complete(d)) return false;
    if (powered[i]) return true;
    d->open ^= 1u;
    if (!doors_save()) { d->open ^= 1u; broadcast(d); return false; }
    broadcast(d); return true;
  }
  return false;
}
static void remove_pair (Door *d) {
  d->used = 0;
  if (getBlockAt(d->x,d->y,d->z) == B_oak_door) makeBlockChange(d->x,d->y,d->z,B_air);
  if (getBlockAt(d->x,d->y+1,d->z) == B_oak_door) makeBlockChange(d->x,(uint8_t)(d->y+1),d->z,B_air);
}
void doors_block_changed (int x, int y, int z) {
  if (changing) return;
  bool dirty = false; changing = true;
  for (unsigned i = 0; i < DOOR_LIMIT; i++) {
    Door *d = &doors[i];
    if (d->used && d->x == x && d->z == z && y >= d->y-1 && y <= d->y+1 && !complete(d)) { remove_pair(d); dirty = true; }
  }
  changing = false;
  if (dirty && !doors_save()) fputs("Could not save door removal.\n",stderr);
}
void doors_send_chunk (int fd, int cx, int cz) {
  if (cx < -2048 || cx > 2047 || cz < -2048 || cz > 2047) return;
  for (unsigned i = 0; i < DOOR_LIMIT; i++) {
    const Door *d = &doors[i];
    if (d->used && d->x >= cx*16 && d->x < cx*16+16 && d->z >= cz*16 && d->z < cz*16+16) send_door(fd,d);
  }
}
static const uint8_t magic[8] = {'L','O','D','O','O','R',1,0};
bool doors_save (void) {
  if (!save_path[0]) return true;
  char temporary[260]; snprintf(temporary,sizeof(temporary),"%s.tmp",save_path);
  FILE *f = fopen(temporary,"wb"); if (!f) { perror("Saving doors"); return false; }
  bool ok = fwrite(magic,1,8,f) == 8;
  for (unsigned i = 0; ok && i < DOOR_LIMIT; i++) if (doors[i].used) {
    const Door *d = &doors[i]; uint16_t x = (uint16_t)d->x, z = (uint16_t)d->z;
    uint8_t record[7] = {(uint8_t)(x>>8),(uint8_t)x,(uint8_t)(z>>8),(uint8_t)z,d->y,d->facing,powered[i] ? 0 : d->open};
    ok = fwrite(record,1,7,f) == 7;
  }
  if (fclose(f)) ok = false;
  if (ok && rename(temporary,save_path) == 0) return true;
  perror("Saving doors"); remove(temporary); return false;
}
bool doors_load (const char *path) {
  memset(powered,0,sizeof(powered));
  memset(doors,0,sizeof(doors)); changing = false; save_path[0] = 0;
  if (!path) return true;
  if (strlen(path) >= sizeof(save_path)) return false;
  memcpy(save_path,path,strlen(path)+1);
  FILE *f = fopen(path,"rb");
  if (!f && errno != ENOENT) { perror("Loading doors"); return false; }
  bool ok = true; unsigned count = 0;
  if (f) {
    uint8_t header[8]; ok = fread(header,1,8,f) == 8 && memcmp(header,magic,8) == 0;
    while (ok) {
      uint8_t record[7]; size_t n = fread(record,1,7,f);
      if (!n) { if (ferror(f)) ok = false; break; }
      if (n != 7 || count == DOOR_LIMIT || !record[4] || record[4] > 254 || record[5] > 3 || record[6] > 1) { ok = false; break; }
      unsigned rx = (unsigned)record[0]*256+record[1], rz = (unsigned)record[2]*256+record[3];
      int x = rx < 32768 ? (int)rx : (int)rx-65536, z = rz < 32768 ? (int)rz : (int)rz-65536;
      if (doors_at(x,record[4],z) || doors_at(x,record[4]+1,z)) { ok = false; break; }
      doors[count++] = (Door){(int16_t)x,(int16_t)z,record[4],record[5],record[6],1};
    }
    fclose(f);
  }
  if (!ok) { memset(doors,0,sizeof(doors)); fputs("Invalid or oversized doors.bin; startup aborted.\n",stderr); return false; }
  bool dirty = false; changing = true;
  for (unsigned i = 0; i < count; i++) if (!complete(&doors[i])) { remove_pair(&doors[i]); dirty = true; }
  /* Drop unpaired legacy blocks and halves left by interrupted placement. */
  for (int i = 0; i < block_changes_count; i++) {
    BlockChange *b = &block_changes[i];
    if (b->block == B_chest) { i += 14; continue; }
    if (b->block == B_oak_door && !doors_at(b->x,b->y,b->z)) { makeBlockChange(b->x,b->y,b->z,B_air); dirty = true; }
  }
  changing = false; return !dirty || doors_save();
}

void doors_refresh_power (void) {
  for (unsigned i = 0; i < DOOR_LIMIT; i++) {
    Door *d = &doors[i]; if (!d->used) { powered[i] = false; continue; }
    bool on = circuits_powered(d->x,d->y,d->z) || circuits_powered(d->x,d->y+1,d->z);
    if (on == powered[i]) continue;
    powered[i] = on; d->open = on ? 1 : 0; broadcast(d);
  }
}
