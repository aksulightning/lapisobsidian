#include "plates.h"
#include <math.h>
#include <string.h>
#include "notes.h"
#include "globals.h"
#include "protocol.h"
#include "registries.h"
#include "tools.h"
#include "varnum.h"
#include "worldgen.h"
_Static_assert(LAPIS_PROTOCOL_VERSION == 772,"Review note sound packets");
uint8_t notes_instrument (int x, int y, int z) {
  uint8_t b = getBlockAt(x,y-1,z);
  if (b == B_oak_planks || b == B_oak_log || b == B_bookshelf) return NOTE_BASS;
  if (b == B_sand || b == B_gravel) return NOTE_SNARE;
  if (b == B_glass) return NOTE_HAT;
  if (b == B_stone || b == B_cobblestone || b == B_bricks || b == B_obsidian) return NOTE_BASSDRUM;
  return NOTE_HARP;
}
void notes_play (int x, int y, int z, uint8_t instrument, uint8_t note, uint8_t velocity) {
  static const char *const sounds[NOTE_INSTRUMENTS] = {
    "minecraft:block.note_block.harp", "minecraft:block.note_block.basedrum",
    "minecraft:block.note_block.snare", "minecraft:block.note_block.hat", "minecraft:block.note_block.bass"
  };
  if (x < -32768 || x > 32767 || z < -32768 || z > 32767 || y < 0 || y > 255 ||
      instrument >= NOTE_INSTRUMENTS || note > 24 || !velocity || velocity > 127) return;
  const char *name = sounds[instrument]; size_t n = strlen(name);
  float pitch = (float)pow(2.0,((double)note-12.0)/12.0);
  for (int i = 0; i < MAX_PLAYERS; i++) if (plates_player_active(&player_data[i])) {
    const PlayerData *p = &player_data[i];
    int dx = (int)p->x-x, dy = (int)p->y-y, dz = (int)p->z-z;
    if (p->client_fd < 0 || (p->flags&0x22) || dx < -32 || dx > 32 || dz < -32 || dz > 32 || dy < -32 || dy > 32 ||
        dx*dx+dy*dy+dz*dz > 32*32) continue;
    int fd = p->client_fd;
    writeVarInt(fd,32u+(uint32_t)sizeVarInt((uint32_t)n)+(uint32_t)n); writeByte(fd,0x6e);
    writeByte(fd,0); writeVarInt(fd,(uint32_t)n); send_all(fd,name,(ssize_t)n); writeByte(fd,0);
    writeByte(fd,2); /* records category: client jukebox/note-block volume slider */
    writeUint32(fd,(uint32_t)(x*8+4)); writeUint32(fd,(uint32_t)(y*8+4)); writeUint32(fd,(uint32_t)(z*8+4));
    writeFloat(fd,(float)velocity/127.0f); writeFloat(fd,pitch); writeUint64(fd,0);
  }
}
