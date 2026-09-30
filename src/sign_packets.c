#include <math.h>
#include <string.h>
#include "signs.h"
#include "sign_protocol.h"
#include "packets.h"
#include "procedures.h"
#include "tools.h"
#include "varnum.h"

typedef struct { uint8_t *data; size_t size, used; bool ok; } Writer;
static void bytes (Writer *w, const void *data, size_t n) {
  if (!w->ok || n > w->size-w->used) { w->ok = false; return; }
  memcpy(w->data+w->used,data,n); w->used += n;
}
static void byte (Writer *w, uint8_t b) { bytes(w,&b,1); }
static void short_be (Writer *w, size_t n) { byte(w,(uint8_t)(n>>8)); byte(w,(uint8_t)n); }
static void string (Writer *w, const char *s) { size_t n = strlen(s); short_be(w,n); bytes(w,s,n); }
static void tag (Writer *w, uint8_t type, const char *name) { byte(w,type); string(w,name); }
static void text (Writer *w, const char *s) {
  size_t n = strlen(s); uint8_t encoded[SIGN_LINE_MAX*2]; size_t used = 0;
  for (size_t i = 0; i < n;) {
    uint8_t ch = (uint8_t)s[i++];
    if (ch < 0xf0) { encoded[used++] = ch; continue; }
    uint32_t cp = (uint32_t)(ch&7u)<<18; cp |= (uint32_t)((uint8_t)s[i++]&63u)<<12;
    cp |= (uint32_t)((uint8_t)s[i++]&63u)<<6; cp |= (uint8_t)s[i++]&63u; cp -= 0x10000;
    uint16_t pair[2] = {(uint16_t)(0xd800u+(cp>>10)),(uint16_t)(0xdc00u+(cp&1023u))};
    for (unsigned j = 0; j < 2; j++) {
      encoded[used++] = (uint8_t)(0xe0u|(pair[j]>>12)); encoded[used++] = (uint8_t)(0x80u|((pair[j]>>6)&63u)); encoded[used++] = (uint8_t)(0x80u|(pair[j]&63u));
    }
  }
  short_be(w,used); bytes(w,encoded,used);
}
uint16_t signs_state (const Sign *s) {
  if (!s || s->orientation > 19) return 0;
  return s->orientation < 16 ? sign_standing_states[s->orientation] : sign_wall_states[s->orientation-16];
}
size_t signs_nbt (const Sign *s, uint8_t *out, size_t capacity) {
  if (!s || !out || !signs_coords_valid(s->x,s->y,s->z) || !signs_state(s)) return 0;
  for (unsigned side = 0; side < 2; side++) for (unsigned line = 0; line < 4; line++) {
    const char *end = memchr(s->lines[side][line],0,SIGN_LINE_MAX+1);
    if (!end || !signs_text_valid((const uint8_t *)s->lines[side][line],(size_t)(end-s->lines[side][line]))) return 0;
  }
  Writer w = {out,capacity,0,true}; byte(&w,10); /* anonymous network compound */
  tag(&w,8,"id"); string(&w,"minecraft:sign");
  tag(&w,1,"is_waxed"); byte(&w,0);
  for (unsigned side = 0; side < 2; side++) {
    tag(&w,10,side ? "back_text" : "front_text");
    tag(&w,8,"color"); string(&w,"black"); tag(&w,1,"has_glowing_text"); byte(&w,0);
    tag(&w,9,"messages"); byte(&w,8); /* modern text components: literal NBT strings */
    const uint8_t four[4] = {0,0,0,4}; bytes(&w,four,4);
    for (unsigned line = 0; line < 4; line++) text(&w,s->lines[side][line]);
    byte(&w,0);
  }
  byte(&w,0); return w.ok ? w.used : 0;
}
static uint64_t position (const Sign *s) {
  return (((uint64_t)(int64_t)s->x&0x3ffffffu)<<38) | (((uint64_t)(int64_t)s->z&0x3ffffffu)<<12) | s->y;
}
int sc_sign (int fd, const Sign *s) {
  uint8_t nbt[SIGN_NBT_MAX]; size_t n = signs_nbt(s,nbt,sizeof(nbt)); if (!n) return 1;
  uint16_t state = signs_state(s);
  writeVarInt(fd,9u+(uint32_t)sizeVarInt(state)); writeByte(fd,0x08); writeUint64(fd,position(s)); writeVarInt(fd,state);
  writeVarInt(fd,10u+(uint32_t)n); writeByte(fd,0x06); writeUint64(fd,position(s)); writeVarInt(fd,SIGN_ENTITY_TYPE); send_all(fd,nbt,(ssize_t)n);
  return 0;
}
int sc_signEditor (int fd, const Sign *s, bool front) {
  if (!s || !signs_state(s)) return 1;
  writeVarInt(fd,10); writeByte(fd,0x35); writeUint64(fd,position(s)); writeByte(fd,front ? 1 : 0); return 0;
}

typedef struct { const uint8_t *data; size_t size, cursor; } Reader;
static bool integer (Reader *r, uint32_t *out) {
  uint32_t v = 0;
  for (unsigned shift = 0; shift < 35; shift += 7) {
    if (r->cursor == r->size) return false;
    uint8_t b = r->data[r->cursor++]; if (shift == 28 && (b&0xf8u)) return false;
    v |= (uint32_t)(b&127u)<<shift;
    if (!(b&128u)) { *out = v; return true; }
  }
  return false;
}
static bool coordinates (Reader *r, int *x, int *y, int *z) {
  if (r->size-r->cursor < 8) return false;
  uint64_t v = 0; for (unsigned i = 0; i < 8; i++) v = (v<<8)|r->data[r->cursor++];
  uint32_t rx = (uint32_t)(v>>38), rz = (uint32_t)((v>>12)&0x3ffffffu), ry = (uint32_t)(v&4095u);
  *x = rx < 0x2000000 ? (int)rx : (int)rx-0x4000000;
  *z = rz < 0x2000000 ? (int)rz : (int)rz-0x4000000;
  *y = ry < 2048 ? (int)ry : (int)ry-4096;
  return signs_coords_valid(*x,*y,*z);
}
int cs_updateSign (int fd, int length) {
  uint8_t payload[SIGN_PACKET_MAX];
  if (length < 13 || (size_t)length > sizeof(payload) || recv_all(fd,payload,(size_t)length,false) != length) return 1;
  Reader r = {payload,(size_t)length,0}; int x,y,z;
  if (!coordinates(&r,&x,&y,&z)) return 1;
  uint8_t front = payload[r.cursor++]; if (front > 1) return 1;
  char lines[4][SIGN_LINE_MAX+1] = {{0}};
  for (unsigned i = 0; i < 4; i++) {
    uint32_t n;
    if (!integer(&r,&n) || n > r.size-r.cursor || !signs_text_valid(payload+r.cursor,n)) return 1;
    memcpy(lines[i],payload+r.cursor,n); r.cursor += n;
  }
  if (r.cursor != r.size) return 1;
  PlayerData *player;
  if (getPlayerData(fd,&player)) return 1;
  if (!signs_edit(player,x,y,z,front != 0,lines)) {
    const Sign *s = signs_at(x,y,z); if (s) sc_sign(fd,s);
  }
  return 0;
}
/* Placement and mining mutate signs too: validate their whole frames first. */
int cs_useItemOn (int fd, int length) {
  uint8_t payload[40];
  if (length <= 0 || (size_t)length > sizeof(payload) || recv_all(fd,payload,(size_t)length,false) != length) return 1;
  Reader r = {payload,(size_t)length,0}; uint32_t hand, face, sequence; int x,y,z;
  if (!integer(&r,&hand) || hand > 1 || !coordinates(&r,&x,&y,&z) || !integer(&r,&face) || face > 5 || r.size-r.cursor < 14) return 1;
  for (unsigned i = 0; i < 3; i++) {
    uint32_t bits = 0; for (unsigned j = 0; j < 4; j++) bits = (bits<<8)|payload[r.cursor++];
    float value; memcpy(&value,&bits,4); if (!isfinite(value) || value < 0 || value > 1) return 1;
  }
  if (payload[r.cursor++] > 1 || payload[r.cursor++] > 1 || !integer(&r,&sequence) || r.cursor != r.size) return 1;
  PlayerData *player; if (getPlayerData(fd,&player)) return 1;
  sc_acknowledgeBlockChange(fd,(int)sequence);
  if (!hand && !(player->flags&0x22) && player->health) handlePlayerUseItem(player,(short)x,(short)y,(short)z,(uint8_t)face);
  return 0;
}
int cs_playerAction (int fd, int length) {
  uint8_t payload[24];
  if (length <= 0 || (size_t)length > sizeof(payload) || recv_all(fd,payload,(size_t)length,false) != length) return 1;
  Reader r = {payload,(size_t)length,0}; uint32_t action, sequence; int x,y,z;
  if (!integer(&r,&action) || action > 6 || !coordinates(&r,&x,&y,&z) || r.cursor == r.size) return 1;
  uint8_t face = payload[r.cursor++]; if ((face > 5 && face != 255) || !integer(&r,&sequence) || r.cursor != r.size) return 1;
  PlayerData *player; if (getPlayerData(fd,&player)) return 1;
  sc_acknowledgeBlockChange(fd,(int)sequence);
  if (!(player->flags&0x22) && player->health) handlePlayerAction(player,(int)action,(short)x,(short)y,(short)z);
  return 0;
}
