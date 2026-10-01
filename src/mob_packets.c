#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "mobs.h"
#include "packets.h"
#include "procedures.h"
#include "protocol.h"
#include "registries.h"
#include "tools.h"
#include "varnum.h"

_Static_assert(LAPIS_PROTOCOL_VERSION == 772, "Review mob packets, metadata and entity/particle IDs");
void sc_mob_equipment (int fd, int id) {
  writeVarInt(fd,5u+(uint32_t)sizeVarInt((uint32_t)id)+(uint32_t)sizeVarInt(I_bow)); writeByte(fd,0x5f);
  writeVarInt(fd,(uint32_t)id); writeByte(fd,0); /* main hand, last entry */
  writeByte(fd,1); writeVarInt(fd,I_bow); writeByte(fd,0); writeByte(fd,0);
}
void sc_creeper_fuse (int fd, int id, bool active) {
  uint32_t direction = active ? 1u : UINT32_MAX;
  writeVarInt(fd,4u+(uint32_t)sizeVarInt((uint32_t)id)+(uint32_t)sizeVarInt(direction)); writeByte(fd,0x5c);
  writeVarInt(fd,(uint32_t)id); writeByte(fd,16); writeByte(fd,1); writeVarInt(fd,direction); writeByte(fd,255);
}
void sc_arrow_metadata (int fd, int id) {
  writeVarInt(fd,5u+(uint32_t)sizeVarInt((uint32_t)id)); writeByte(fd,0x5c); writeVarInt(fd,(uint32_t)id);
  writeByte(fd,5); writeByte(fd,8); writeByte(fd,1); writeByte(fd,255);
}
void sc_mob_sound_category (int fd, const char *name, int x, int y, int z, uint8_t category) {
  if (!name || (category != 5 && category != 6)) return;
  size_t n = strlen(name); if (n > 96 || x < -32768 || x > 32767 || y < 0 || y > 255 || z < -32768 || z > 32767) return;
  /* Inline sound holder avoids adding the entire vanilla sound registry. */
  writeVarInt(fd,32u+(uint32_t)sizeVarInt((uint32_t)n)+(uint32_t)n); writeByte(fd,0x6e);
  writeByte(fd,0); writeVarInt(fd,(uint32_t)n); send_all(fd,name,(ssize_t)n); writeByte(fd,0);
  writeByte(fd,category); /* hostile=5, neutral=6 */
  writeUint32(fd,(uint32_t)(x*8+4)); writeUint32(fd,(uint32_t)(y*8)); writeUint32(fd,(uint32_t)(z*8+4));
  writeFloat(fd,1.0f); writeFloat(fd,1.0f); writeUint64(fd,0);
}
void sc_mob_sound (int fd, const char *name, int x, int y, int z) { sc_mob_sound_category(fd,name,x,y,z,5); }
void sc_firecracker (int fd, int x, int y, int z) {
  /* No Explosion packet: only visual firework particles, with no world mutation. */
  writeVarInt(fd,48); writeByte(fd,0x29); writeByte(fd,0); writeByte(fd,0);
  writeDouble(fd,x+0.5); writeDouble(fd,y+0.8); writeDouble(fd,z+0.5);
  writeFloat(fd,0.25f); writeFloat(fd,0.4f); writeFloat(fd,0.25f); writeFloat(fd,0.15f);
  writeUint32(fd,24); writeVarInt(fd,29); /* firework, no particle payload */
}

typedef struct { const uint8_t *data; size_t size, at; } Reader;
static bool integer (Reader *r, uint32_t *out) {
  uint32_t n = 0;
  for (unsigned shift = 0; shift < 35; shift += 7) {
    if (r->at == r->size) return false;
    uint8_t b = r->data[r->at++]; if (shift == 28 && (b&0xf0u)) return false;
    n |= (uint32_t)(b&127u)<<shift;
    if (!(b&128u)) { *out = n; return true; }
  }
  return false;
}
int cs_interact (int fd, int length) {
  uint8_t data[24];
  if (length <= 0 || (size_t)length > sizeof(data) || recv_all(fd,data,(size_t)length,false) != length) return 1;
  Reader r = {data,(size_t)length,0}; uint32_t raw,type,hand;
  if (!integer(&r,&raw) || !integer(&r,&type) || type > 2) return 1;
  if (type == 2) for (unsigned i = 0; i < 3; i++) {
    if (r.size-r.at < 4) return 1;
    uint32_t bits = 0; for (unsigned j = 0; j < 4; j++) bits = (bits<<8)|data[r.at++];
    float v; memcpy(&v,&bits,4); if (!isfinite(v) || fabsf(v) > 8) return 1;
  }
  if (type != 1 && (!integer(&r,&hand) || hand > 1)) return 1;
  if (r.size-r.at != 1 || data[r.at] > 1) return 1;
  int id = raw <= INT_MAX ? (int)raw : -1-(int)(UINT32_MAX-raw);
  PlayerData *p; if (getPlayerData(fd,&p)) return 1;
  if (!p->health || (p->flags&0x22) || p->hotbar >= 41) return 0;
  int x,y,z;
  if (id <= -2 && id >= -1-MAX_MOBS) {
    const MobData *m = &mob_data[-id-2]; if (!m->type || !(m->data&31)) return 0;
    x = m->x; y = m->y; z = m->z;
  } else if (id > 0) {
    PlayerData *target; if (getPlayerData(id,&target)) return 0;
    x = target->x; y = target->y; z = target->z;
  } else return 0;
  if (abs(x-p->x) > 4 || abs(y-p->y) > 4 || abs(z-p->z) > 4) return 0;
  if (type == 1) hurtEntity(id,fd,D_generic,1);
  else if (type == 0 && hand == 0) interactEntity(id,fd);
  return 0;
}
