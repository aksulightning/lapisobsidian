/* World entry, chunk streaming and block update packet codecs. */
#include "doors.h"
#include "circuits.h"
#include "farming.h"
#include "procedures.h"
#include "packets.h"
#include "tools.h"
#include "plates.h"
#include "registries.h"
#include "varnum.h"
#include "worldgen.h"
#include "registry.h"
#include "signs.h"
#include "commands.h"
#include <string.h>

/* Advertise all configured Plate dimension keys and enter the selected one.
 * Keys share the pinned overworld dimension type; no chunks are sent here. */
int sc_loginPlay (int client_fd) {
  const char *dimension = plates_dimension(plate_current);
  uint32_t n = (uint32_t)strlen(dimension), count = plates_dimension_count(), list_bytes = 0;
  for (unsigned i = 0; i < count; i++) { uint32_t k = (uint32_t)strlen(plates_dimension_at(i)); list_bytes += (uint32_t)sizeVarInt(k)+k; }
  writeVarInt(client_fd,26u+(uint32_t)sizeVarInt(count)+list_bytes+(uint32_t)sizeVarInt(n)+n+
    (uint32_t)sizeVarInt(MAX_PLAYERS)+(uint32_t)sizeVarInt(VIEW_DISTANCE)*2u);
  writeByte(client_fd,0x2B); writeUint32(client_fd,(uint32_t)client_fd); writeByte(client_fd,false);
  writeVarInt(client_fd,count);
  for (unsigned i = 0; i < count; i++) {
    const char *key = plates_dimension_at(i); uint32_t k = (uint32_t)strlen(key);
    writeVarInt(client_fd,k); send_all(client_fd,key,k);
  }
  // maxplayers
  writeVarInt(client_fd, MAX_PLAYERS);
  // view distance
  writeVarInt(client_fd, VIEW_DISTANCE);
  // sim distance
  writeVarInt(client_fd, VIEW_DISTANCE);
  // reduced debug info
  writeByte(client_fd, 0);
  // respawn screen
  writeByte(client_fd, true);
  // limited crafting
  writeByte(client_fd, false);
  // dimension id (from server-sent registries)
  // the server only sends "overworld"
  writeVarInt(client_fd, 0);
  // dimension name
  writeVarInt(client_fd,n);
  send_all(client_fd,dimension,n);
  // hashed seed
  writeUint64(client_fd, 0x0123456789ABCDEF);
  // gamemode
  writeByte(client_fd, commands_mode_for_fd(client_fd));
  // previous gamemode
  writeByte(client_fd, 0xFF);
  // is debug
  writeByte(client_fd, 0);
  // is flat
  writeByte(client_fd, 0);
  // has death location
  writeByte(client_fd, 0);
  // portal cooldown
  writeVarInt(client_fd, 0);
  // sea level
  writeVarInt(client_fd, 63);
  // secure chat
  writeByte(client_fd, 0);

  return 0;

}

/* Enter client terrain-loading mode before the spawn sequence streams chunks. */
int sc_startWaitingForChunks (int client_fd) {
  writeVarInt(client_fd, 6);
  writeByte(client_fd, 0x22);
  writeByte(client_fd, 13);
  writeUint32(client_fd, 0);
  return 0;
}

/* Set the chunk-cache center in chunk units; the legacy y parameter is Z. */
int sc_setCenterChunk (int client_fd, int x, int y) {
  writeVarInt(client_fd, 1 + sizeVarInt(x) + sizeVarInt(y));
  writeByte(client_fd, 0x57);
  writeVarInt(client_fd, x);
  writeVarInt(client_fd, y);
  return 0;
}

/* Stream one selected-world chunk, reusing shared chunk_section scratch space.
 * Reject coordinates outside compact world storage before sending; follow the
 * base chunk with stateful block/sign overlays so clients see current state. */
int sc_chunkDataAndUpdateLight (int client_fd, int _x, int _z) {
  if (_x < -2048 || _x > 2047 || _z < -2048 || _z > 2047) return 1;

  const int chunk_data_size = (4101 + sizeVarInt(256) + sizeof(network_block_palette)) * 20 + 6 * 4;
  const int light_data_size = 14 + (sizeVarInt(2048) + 2048) * 26;

  writeVarInt(client_fd, 11 + sizeVarInt(chunk_data_size) + chunk_data_size + light_data_size);
  writeByte(client_fd, 0x27);

  writeUint32(client_fd, _x);
  writeUint32(client_fd, _z);

  writeVarInt(client_fd, 0); // omit heightmaps

  writeVarInt(client_fd, chunk_data_size);

  int x = _x * 16, z = _z * 16, y;

  // Fill the four sections below Y=0 with bedrock to seal the compact world.
  for (int i = 0; i < 4; i ++) {
    writeUint16(client_fd, 4096); // block count
    writeByte(client_fd, 0); // block bits
    writeVarInt(client_fd, 85); // block palette (bedrock)
    writeByte(client_fd, 0); // biome bits
    writeByte(client_fd, 0); // biome palette
  }
  // yield to idle task
  task_yield();

  // send chunk sections
  for (int i = 0; i < 20; i ++) {
    y = i * 16;
    uint8_t biome = buildChunkSection(x, y, z);
    uint16_t block_count = 0;
    for (size_t j = 0; j < sizeof(chunk_section); j ++) {
      if (chunk_section[j] != B_air) block_count ++;
    }
    writeUint16(client_fd, block_count);
    writeByte(client_fd, 8); // bits per entry
    writeVarInt(client_fd, 256); // block palette length
    // block palette as varint buffer
    send_all(client_fd, network_block_palette, sizeof(network_block_palette));
    // chunk section buffer
    send_all(client_fd, chunk_section, 4096);
    // biome data
    writeByte(client_fd, 0); // bits per entry
    writeByte(client_fd, biome); // biome palette
    // yield to idle task
    task_yield();
  }

  writeVarInt(client_fd, 0); // omit block entities

  // light data
  writeVarInt(client_fd, 1);
  writeUint64(client_fd, 0b11111111111111111111111111);
  writeVarInt(client_fd, 0);
  writeVarInt(client_fd, 0);
  writeVarInt(client_fd, 0);

  // sky light array
  writeVarInt(client_fd, 26);
  for (int i = 0; i < 2048; i ++) chunk_section[i] = 0xFF;
  for (int i = 2048; i < 4096; i ++) chunk_section[i] = 0;
  for (int i = 0; i < 8; i ++) {
    writeVarInt(client_fd, 2048);
    send_all(client_fd, chunk_section + 2048, 2048);
  }
  for (int i = 0; i < 18; i ++) {
    writeVarInt(client_fd, 2048);
    send_all(client_fd, chunk_section, 2048);
  }
  // don't send block light
  writeVarInt(client_fd, 0);

  // Sending block updates changes light prediction on the client.
  // Light-emitting blocks are omitted from chunk data so that they can
  // be overlayed here. This seems to be cheaper than sending actual
  // block light data.
  for (int i = 0; i < block_changes_count; i ++) {
    #ifdef ALLOW_CHESTS
      if (block_changes[i].block != B_torch && block_changes[i].block != B_chest) continue;
    #else
      if (block_changes[i].block != B_torch) continue;
    #endif
    if (block_changes[i].x < x || block_changes[i].x >= x + 16) continue;
    if (block_changes[i].z < z || block_changes[i].z >= z + 16) continue;
    sc_blockUpdate(client_fd, block_changes[i].x, block_changes[i].y, block_changes[i].z, block_changes[i].block);
  }

  signs_send_chunk(client_fd, _x, _z);
  doors_send_chunk(client_fd, _x, _z);
  circuits_send_chunk(client_fd,_x,_z);
  farming_send_chunk(client_fd,_x,_z);
  return 0;

}

/* Translate a compact block ID to its protocol state, consulting door/circuit/
 * crop sidecars in this Plate. Returns 1 before writing for an unmapped block. */
int sc_blockUpdate (int client_fd, int64_t x, int64_t y, int64_t z, uint8_t block) {
  uint16_t state;
  if (!registry_block_state(block, &state)) return 1;
  if (block == B_oak_door && x >= -32768 && x <= 32767 && y >= 0 && y <= 255 && z >= -32768 && z <= 32767)
    doors_state_at((int)x,(int)y,(int)z,&state);
  if (x >= -32768 && x <= 32767 && y >= 0 && y <= 255 && z >= -32768 && z <= 32767)
    { circuits_state_at((int)x,(int)y,(int)z,block,&state);
      farming_state_at((int)x,(int)y,(int)z,block,&state); }
  writeVarInt(client_fd, 9 + sizeVarInt(state));
  writeByte(client_fd, 0x08);
  writeUint64(client_fd, (((uint64_t)x & 0x3FFFFFFu) << 38) | (((uint64_t)z & 0x3FFFFFFu) << 12) | ((uint64_t)y & 0xFFFu));
  writeVarInt(client_fd, state);
  return 0;
}

/* Echo the action sequence so the client can retire prediction; this is not a
 * success flag. Corrections and durable output ordering are handled by callers. */
int sc_acknowledgeBlockChange (int client_fd, int sequence) {
  writeVarInt(client_fd, 1 + sizeVarInt(sequence));
  writeByte(client_fd, 0x04);
  writeVarInt(client_fd, sequence);
  return 0;
}

/* Re-enter the selected dimension with no retained client data. Callers must
 * reset/restore server state and send the subsequent spawn/chunk sequence. */
int sc_respawn (int client_fd) {

  const char *dimension = plates_dimension(plate_current);
  uint32_t n = (uint32_t)strlen(dimension);
  writeVarInt(client_fd,18u+(uint32_t)sizeVarInt(n)+n);
  writeByte(client_fd, 0x4B);

  // dimension id (from server-sent registries)
  writeVarInt(client_fd, 0);
  // dimension name
  writeVarInt(client_fd,n);
  send_all(client_fd,dimension,n);
  // hashed seed
  writeUint64(client_fd, 0x0123456789ABCDEF);
  // gamemode
  writeByte(client_fd, commands_mode_for_fd(client_fd));
  // previous gamemode
  writeByte(client_fd, 0xFF);
  // is debug
  writeByte(client_fd, 0);
  // is flat
  writeByte(client_fd, 0);
  // has death location
  writeByte(client_fd, 0);
  // portal cooldown
  writeVarInt(client_fd, 0);
  // sea level
  writeVarInt(client_fd, 63);
  // data kept
  writeByte(client_fd, 0);

  return 0;
}
