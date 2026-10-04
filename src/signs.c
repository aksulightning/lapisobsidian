#include "plates.h"
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "signs.h"
#include "plate_contexts.h"
#include "commands.h"
#include "packets.h"
#include "procedures.h"
#include "registries.h"
#include "worldgen.h"


typedef struct { unsigned slot; bool active, front; uint32_t tick; } Editor;

typedef struct {
  Sign signs[SIGN_LIMIT];
  Editor editors[MAX_PLAYERS];
  char save_path[256];
  bool cleaning;
} SignsContext;
static SignsContext legacy_context, *ctx = &legacy_context;
size_t signs_context_size (void) { return sizeof(*ctx); }
void signs_context_select (void *memory) { ctx = memory ? memory : &legacy_context; }


bool signs_coords_valid (int x, int y, int z) {
  return x >= -32768 && x <= 32767 && z >= -32768 && z <= 32767 && y >= 0 && y <= 255;
}
bool signs_text_valid (const uint8_t *text, size_t length) {
  if (!text || length > SIGN_LINE_MAX) return false;
  size_t i = 0, units = 0;
  while (i < length) {
    uint32_t ch = text[i++], minimum = 0; unsigned extra = 0;
    if (ch >= 0xc2 && ch <= 0xdf) { ch &= 31; extra = 1; minimum = 0x80; }
    else if (ch >= 0xe0 && ch <= 0xef) { ch &= 15; extra = 2; minimum = 0x800; }
    else if (ch >= 0xf0 && ch <= 0xf4) { ch &= 7; extra = 3; minimum = 0x10000; }
    else if (ch >= 0x80) return false;
    if (extra > length-i) return false;
    while (extra--) { uint8_t b = text[i++]; if ((b & 0xc0u) != 0x80) return false; ch = (ch<<6) | (b&63u); }
    if (ch < minimum || ch > 0x10ffff || (ch >= 0xd800 && ch <= 0xdfff) ||
        ch < 32 || (ch >= 127 && ch <= 159) || ch == 0xa7) return false;
    units += ch > 0xffff ? 2u : 1u;
    if (units > 90) return false;
  }
  return true;
}
static int find (int x, int y, int z) {
  if (!signs_coords_valid(x,y,z)) return -1;
  for (int i = 0; i < SIGN_LIMIT; i++) if (ctx->signs[i].used && ctx->signs[i].x == x && ctx->signs[i].y == y && ctx->signs[i].z == z) return i;
  return -1;
}
const Sign *signs_at (int x, int y, int z) { int i = find(x,y,z); return i < 0 ? NULL : &ctx->signs[i]; }
static int player_index (const PlayerData *player) {
  for (int i = 0; i < MAX_PLAYERS; i++) if (plates_player_active(&player_data[i])) if (player == &player_data[i]) return i;
  return -1;
}
void signs_reset_player (PlayerData *player) { int i = player_index(player); if (i >= 0) ctx->editors[i].active = false; }
static bool near (const PlayerData *player, int x, int y, int z) {
  if (!player || player->client_fd < 0 || (player->flags & 0x22) || !player->health || commands_gamemode(player) >= 2) return false;
  double dx = (double)player->x-x, dy = (double)player->y-y, dz = (double)player->z-z;
  return dx*dx+dy*dy+dz*dz <= 64;
}
static bool active (Editor *e) {
  if (e->active && (double)(uint32_t)(server_ticks-e->tick) >= 60.0*(double)TICKS_PER_SECOND) e->active = false;
  return e->active;
}
static void invalidate (unsigned slot) {
  for (int i = 0; i < MAX_PLAYERS; i++) if (plates_player_active(&player_data[i])) if (ctx->editors[i].slot == slot) ctx->editors[i].active = false;
}
static void broadcast (const Sign *sign) {
  for (int i = 0; i < MAX_PLAYERS; i++) if (plates_player_active(&player_data[i])) if (player_data[i].client_fd >= 0 && !(player_data[i].flags & 0x22)) sc_sign(player_data[i].client_fd,sign);
}
bool signs_open (PlayerData *player, int x, int y, int z, bool front) {
  int p = player_index(player), s = find(x,y,z);
  if (p < 0 || s < 0 || !near(player,x,y,z) || getBlockAt(x,y,z) != B_oak_sign) return false;
  for (int i = 0; i < MAX_PLAYERS; i++) if (plates_player_active(&player_data[i])) if (i != p && active(&ctx->editors[i]) && ctx->editors[i].slot == (unsigned)s) return false;
  ctx->editors[p] = (Editor){(unsigned)s,true,front,server_ticks};
  sc_sign(player->client_fd,&ctx->signs[s]);
  sc_signEditor(player->client_fd,&ctx->signs[s],front);
  return true;
}
bool signs_interact (PlayerData *player, int x, int y, int z) {
  if (!signs_coords_valid(x,y,z) || !near(player,x,y,z) || getBlockAt(x,y,z) != B_oak_sign) return false;
  const Sign *s = signs_at(x,y,z);
  if (!s) {
    /* An old save or interrupted placement can have a blank sign block only. */
    for (unsigned i = 0; i < SIGN_LIMIT; i++) if (!ctx->signs[i].used) {
      memset(&ctx->signs[i],0,sizeof(ctx->signs[i]));
      ctx->signs[i].x = (int16_t)x; ctx->signs[i].y = (uint8_t)y; ctx->signs[i].z = (int16_t)z; ctx->signs[i].used = 1;
      if (!signs_save()) { ctx->signs[i].used = 0; return false; }
      s = &ctx->signs[i]; break;
    }
    if (!s) return false;
  }
  static const double wall_yaw[4] = {180,0,90,270};
  double angle = (s->orientation < 16 ? s->orientation*22.5 : wall_yaw[s->orientation-16])*0.017453292519943295;
  double dot = -(player->x-x)*sin(angle)+(player->z-z)*cos(angle);
  return signs_open(player,x,y,z,dot >= 0);
}
bool signs_place (PlayerData *player, int x, int y, int z, uint8_t face) {
  if (!signs_coords_valid(x,y,z) || face < 1 || face > 5 || !near(player,x,y,z) ||
      isPassableBlock(getBlockAt(x,y,z))) return false;
  static const int dx[6] = {0,0,0,0,-1,1}, dz[6] = {0,0,-1,1,0,0};
  x += dx[face]; z += dz[face]; if (face == 1) y++;
  if (!signs_coords_valid(x,y,z) || !near(player,x,y,z) || !isReplaceableBlock(getBlockAt(x,y,z))) return false;
  int slot = -1;
  for (int i = 0; i < SIGN_LIMIT; i++) if (!ctx->signs[i].used) { slot = i; break; }
  if (slot < 0) { sc_systemChat(player->client_fd,"Sign storage limit reached.",27); return false; }
  uint8_t old = getBlockAt(x,y,z);
  Sign *s = &ctx->signs[slot];
  memset(s,0,sizeof(*s)); s->used = 1; s->x = (int16_t)x; s->y = (uint8_t)y; s->z = (int16_t)z;
  s->orientation = face == 1 ? (uint8_t)((((unsigned)(uint8_t)player->yaw+128u+8u)/16u)&15u) : (uint8_t)(face+14);
  if (makeBlockChange((short)x,(uint8_t)y,(short)z,B_oak_sign)) { s->used = 0; return false; }
  if (!signs_save()) { s->used = 0; makeBlockChange((short)x,(uint8_t)y,(short)z,old); return false; }
  broadcast(s); signs_open(player,x,y,z,true); return true;
}
bool signs_edit (PlayerData *player, int x, int y, int z, bool front, const char lines[4][SIGN_LINE_MAX+1]) {
  int p = player_index(player), slot = find(x,y,z);
  if (p < 0 || slot < 0 || !lines) return false;
  Editor *e = &ctx->editors[p];
  if (!active(e) || e->slot != (unsigned)slot || e->front != front || !near(player,x,y,z) || getBlockAt(x,y,z) != B_oak_sign) return false;
  for (unsigned i = 0; i < 4; i++) {
    const char *end = memchr(lines[i],0,SIGN_LINE_MAX+1);
    if (!end || !signs_text_valid((const uint8_t *)lines[i],(size_t)(end-lines[i]))) return false;
  }
  Sign *s = &ctx->signs[slot]; unsigned side = front ? 0u : 1u;
  char previous[4][SIGN_LINE_MAX+1]; memcpy(previous,s->lines[side],sizeof(previous));
  memset(s->lines[side],0,sizeof(previous));
  for (unsigned i = 0; i < 4; i++) memcpy(s->lines[side][i],lines[i],strlen(lines[i]));
  e->active = false;
  if (!signs_save()) { memcpy(s->lines[side],previous,sizeof(previous)); sc_sign(player->client_fd,s); return false; }
  broadcast(s); return true;
}
static bool supported (const Sign *s) {
  int x = s->x, y = s->y, z = s->z;
  if (s->orientation < 16) y--;
  else if (s->orientation == 16) z++;
  else if (s->orientation == 17) z--;
  else if (s->orientation == 18) x++;
  else x--;
  return signs_coords_valid(x,y,z) && !isPassableBlock(getBlockAt(x,y,z));
}
void signs_block_changed (int x, int y, int z, uint8_t block) {
  if (ctx->cleaning) return;
  bool changed = false; ctx->cleaning = true;
  int s = find(x,y,z);
  if (s >= 0 && block != B_oak_sign) { ctx->signs[s].used = 0; invalidate((unsigned)s); changed = true; }
  for (unsigned i = 0; i < SIGN_LIMIT; i++) if (ctx->signs[i].used &&
      ctx->signs[i].x >= x-1 && ctx->signs[i].x <= x+1 && ctx->signs[i].y >= y-1 && ctx->signs[i].y <= y+1 &&
      ctx->signs[i].z >= z-1 && ctx->signs[i].z <= z+1 && !supported(&ctx->signs[i])) {
    Sign *sign = &ctx->signs[i]; sign->used = 0; invalidate(i); changed = true;
    makeBlockChange(sign->x,sign->y,sign->z,B_air);
  }
  ctx->cleaning = false;
  if (changed && !signs_save()) fputs("Could not save sign removal.\n",stderr);
}
void signs_send_chunk (int fd, int cx, int cz) {
  if (cx < -2048 || cx > 2047 || cz < -2048 || cz > 2047) return;
  for (unsigned i = 0; i < SIGN_LIMIT; i++) {
    const Sign *s = &ctx->signs[i];
    if (s->used && s->x >= cx*16 && s->x < cx*16+16 && s->z >= cz*16 && s->z < cz*16+16) sc_sign(fd,s);
  }
}
/* Versioned fixed-width records; no raw structs, pointers or protocol NBT. */
#define RECORD_BYTES (6 + 8*(SIGN_LINE_MAX+1))
static const uint8_t magic[8] = {'L','O','S','I','G','N',1,0};
bool signs_save (void) {
  if (!ctx->save_path[0]) return true;
  char temporary[260]; snprintf(temporary,sizeof(temporary),"%s.tmp",ctx->save_path);
  FILE *f = fopen(temporary,"wb"); if (!f) { perror("Saving signs"); return false; }
  bool ok = fwrite(magic,1,sizeof(magic),f) == sizeof(magic);
  for (unsigned i = 0; ok && i < SIGN_LIMIT; i++) if (ctx->signs[i].used) {
    const Sign *s = &ctx->signs[i]; uint8_t record[RECORD_BYTES];
    uint16_t x = (uint16_t)s->x, z = (uint16_t)s->z;
    record[0] = (uint8_t)(x>>8); record[1] = (uint8_t)x; record[2] = (uint8_t)(z>>8); record[3] = (uint8_t)z;
    record[4] = s->y; record[5] = s->orientation; memcpy(record+6,s->lines,sizeof(s->lines));
    ok = fwrite(record,1,sizeof(record),f) == sizeof(record);
  }
  if (fclose(f)) ok = false;
  if (ok && rename(temporary,ctx->save_path) == 0) return true;
  perror("Saving signs"); remove(temporary); return false;
}
bool signs_load (const char *path) {
  memset(ctx->signs,0,sizeof(ctx->signs)); memset(ctx->editors,0,sizeof(ctx->editors)); ctx->save_path[0] = 0;
  if (!path) return true;
  if (strlen(path) >= sizeof(ctx->save_path)) return false;
  memcpy(ctx->save_path,path,strlen(path)+1);
  FILE *f = fopen(path,"rb");
  if (!f) { if (errno == ENOENT) return true; perror("Loading signs"); return false; }
  uint8_t header[8]; bool ok = fread(header,1,8,f) == 8 && memcmp(header,magic,8) == 0;
  unsigned count = 0;
  while (ok) {
    uint8_t record[RECORD_BYTES]; size_t n = fread(record,1,sizeof(record),f);
    if (!n) { if (ferror(f)) ok = false; break; }
    if (n != sizeof(record) || count == SIGN_LIMIT) { ok = false; break; }
    unsigned rx = (unsigned)record[0]*256+record[1], rz = (unsigned)record[2]*256+record[3];
    int x = rx < 32768 ? (int)rx : (int)rx-65536, z = rz < 32768 ? (int)rz : (int)rz-65536;
    if (record[5] > 19 || find(x,record[4],z) >= 0) { ok = false; break; }
    Sign *s = &ctx->signs[count++]; s->used = 1; s->x = (int16_t)x; s->z = (int16_t)z; s->y = record[4]; s->orientation = record[5];
    memcpy(s->lines,record+6,sizeof(s->lines));
    for (unsigned side = 0; side < 2; side++) for (unsigned line = 0; line < 4; line++) {
      char *text = s->lines[side][line], *end = memchr(text,0,SIGN_LINE_MAX+1);
      if (!end || !signs_text_valid((const uint8_t *)text,(size_t)(end-text))) { ok = false; continue; }
      for (char *p = end; p < text+SIGN_LINE_MAX+1; p++) if (*p) ok = false;
    }
  }
  fclose(f);
  if (!ok) { memset(ctx->signs,0,sizeof(ctx->signs)); fputs("Invalid or oversized signs.bin; startup aborted.\n",stderr); return false; }
  bool changed = false;
  for (unsigned i = 0; i < count; i++) if (getBlockAt(ctx->signs[i].x,ctx->signs[i].y,ctx->signs[i].z) != B_oak_sign) { ctx->signs[i].used = 0; changed = true; }
  /* Missing records (old worlds or interrupted placement) are recovered blank on the next interaction. */
  for (unsigned i = 0; i < count; i++) if (ctx->signs[i].used && !supported(&ctx->signs[i])) {
    ctx->signs[i].used = 0; changed = true;
    makeBlockChange(ctx->signs[i].x,ctx->signs[i].y,ctx->signs[i].z,B_air);
  }
  return !changed || signs_save();
}
