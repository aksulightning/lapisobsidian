/* Minecraft Java 1.21.8 / protocol 772 command and game-mode packets. */
#include <string.h>
#include "commands.h"
#include "optional_features.h"
#include "items.h"
#include "packets.h"
#include "procedures.h"
#include "registry.h"
#include "tools.h"
#include "varnum.h"

typedef struct { const uint8_t *data; size_t length, cursor; } CommandReader;
static bool skip (CommandReader *reader, size_t n) {
  if (n > reader->length-reader->cursor) return false;
  reader->cursor += n; return true;
}
static bool integer (CommandReader *reader, uint32_t *out) {
  uint32_t value = 0;
  for (unsigned shift = 0; shift < 35; shift += 7) {
    if (!skip(reader,1)) return false;
    uint8_t byte = reader->data[reader->cursor-1];
    if (shift == 28 && (byte & 0xf8u)) return false;
    value |= (uint32_t)(byte & 127u) << shift;
    if (!(byte & 128u)) { *out = value; return true; }
  }
  return false;
}
int cs_chatCommand (int fd, int length, bool signed_packet) {
  uint8_t payload[COMMAND_MAX_BYTES+32];
  if (length <= 0 || (size_t)length > sizeof(payload)) return 1;
  if (recv_all(fd,payload,(size_t)length,false) != length) return 1;
  CommandReader reader = {payload,(size_t)length,0}; uint32_t size, signatures, count;
  if (!integer(&reader,&size) || !size || size > COMMAND_MAX_BYTES) return 1;
  size_t start = reader.cursor;
  if (!skip(&reader,size)) return 1;
  if (signed_packet) {
    /* Our tree uses brigadier:string, never signed minecraft:message arguments. */
    if (!skip(&reader,16) || !integer(&reader,&signatures) || signatures != 0 ||
        !integer(&reader,&count) || !skip(&reader,4)) return 1;
  }
  if (reader.cursor != reader.length) return 1;
  PlayerData *player;
  if (getPlayerData(fd,&player) || (player->flags & 0x20)) return 1;
  commands_execute(player,(const char *)payload+start,size);
  return 0;
}
int sc_commands (int fd) {
  static const char *const names[] = {"help","seed","worldinfo","spawn","tp","time","gamemode","admin","spawnmob","music","plate","tps",
#if LAPIS_WORLD_EDIT == 1
    "we",
#endif
  };
  const unsigned count = sizeof(names)/sizeof(names[0]);
  uint8_t data[512]; size_t used = 0;
  data[used++] = (uint8_t)(1+count*2); /* root, literals and greedy argument nodes */
  data[used++] = 0; data[used++] = (uint8_t)count;
  for (unsigned i = 1; i <= count; i ++) data[used++] = (uint8_t)i;
  for (unsigned i = 0; i < count; i ++) {
    size_t n = strlen(names[i]);
    data[used++] = 5; data[used++] = 1; data[used++] = (uint8_t)(1+count+i);
    data[used++] = (uint8_t)n; memcpy(data+used,names[i],n); used += n;
  }
  for (unsigned i = 0; i < count; i ++) {
    data[used++] = 6; data[used++] = 0; data[used++] = 9;
    memcpy(data+used,"arguments",9); used += 9;
    data[used++] = 5; /* brigadier:string parser ID for protocol 772 */
    data[used++] = 2; /* greedy phrase, server validates individual arguments */
  }
  data[used++] = 0; /* root index */
  writeVarInt(fd,(uint32_t)(used+1)); writeByte(fd,0x10);
  return send_all(fd,data,(ssize_t)used) == (ssize_t)used ? 0 : 1;
}
int sc_changeGameMode (PlayerData *player, uint8_t mode) {
  if (!player || mode > 3) return 1;
  writeVarInt(player->client_fd,6); writeByte(player->client_fd,0x22);
  writeByte(player->client_fd,3); writeFloat(player->client_fd,(float)mode);
  sc_playerAbilities(player->client_fd,commands_abilities(player));
  for (int i = 0; i < MAX_PLAYERS; i ++) {
    int fd = player_data[i].client_fd;
    if (fd < 0 || (player_data[i].flags & 0x20)) continue;
    writeVarInt(fd,20); writeByte(fd,0x3f); writeByte(fd,4); writeByte(fd,1);
    send_all(fd,player->uuid,16); writeVarInt(fd,mode);
  }
  return 0;
}
int cs_creativeSlot (int fd, int length) {
  uint8_t payload[512];
  if (length < 3 || (size_t)length > sizeof(payload)) return 1;
  if (recv_all(fd,payload,(size_t)length,false) != length) return 1;
  unsigned slot = (unsigned)payload[0]*256+payload[1];
  CommandReader reader = {payload,(size_t)length,2}; uint32_t count, item = 0, added = 0, removed = 0;
  if (!integer(&reader,&count) || count > 64) return 1;
  if (count && (!integer(&reader,&item) || !item || !registry_item_id_valid(item) ||
      !integer(&reader,&added) || !integer(&reader,&removed))) return 1;
  PlayerData *player; if (getPlayerData(fd,&player)) return 1;
  if (commands_gamemode(player) != 1 || (player->flags & 0x22) || !player->health) return 0;
  if (slot == 65535) {
    if (added || removed || reader.cursor != reader.length) return 1;
    if (count) items_drop_stack(player,(uint16_t)item,(uint8_t)count);
    return 0;
  }
  if (slot < 5 || slot > 45) return 0;
  uint8_t index = clientSlotToServerSlot(0,(uint8_t)slot);
  if (index >= 41) return 1;
  if (added || removed) {
    sc_setContainerSlot(fd,0,(uint16_t)slot,player->inventory_count[index],player->inventory_items[index]);
    char message[] = "Custom item components are not supported.";
    sc_systemChat(fd,message,(uint16_t)(sizeof(message)-1));
    return 0;
  }
  if (reader.cursor != reader.length) return 1;
  player->inventory_items[index] = (uint16_t)item; player->inventory_count[index] = (uint8_t)count;
  sc_setContainerSlot(fd,0,(uint16_t)slot,(uint8_t)count,(uint16_t)item);
  return 0;
}

static bool chat_utf8 (const uint8_t *text, size_t size) {
  for (size_t i = 0; i < size;) {
    uint32_t ch = text[i++], minimum; unsigned continuation;
    if (ch < 128) { if (ch < 32 || ch == 127) return false; continue; }
    if (ch >= 0xc2 && ch <= 0xdf) { ch &= 31; continuation = 1; minimum = 0x80; }
    else if (ch >= 0xe0 && ch <= 0xef) { ch &= 15; continuation = 2; minimum = 0x800; }
    else if (ch >= 0xf0 && ch <= 0xf4) { ch &= 7; continuation = 3; minimum = 0x10000; }
    else return false;
    if (continuation > size-i) return false;
    while (continuation--) { uint8_t b = text[i++]; if ((b & 0xc0) != 0x80) return false; ch = (ch<<6) | (b & 63u); }
    if (ch < minimum || ch > 0x10ffff || (ch >= 0xd800 && ch <= 0xdfff)) return false;
  }
  return true;
}
int cs_chat (int fd, int length) {
  uint8_t payload[512];
  if (length <= 0 || (size_t)length > sizeof(payload)) return 1;
  if (recv_all(fd,payload,(size_t)length,false) != length) return 1;
  CommandReader reader = {payload,(size_t)length,0}; uint32_t size, count;
  if (!integer(&reader,&size) || !size || size > 224) return 1;
  size_t start = reader.cursor;
  if (!skip(&reader,size) || !chat_utf8(payload+start,size) || !skip(&reader,17)) return 1;
  uint8_t signature = payload[reader.cursor-1];
  if (signature > 1 || (signature && !skip(&reader,256))) return 1;
  if (!integer(&reader,&count) || !skip(&reader,4) || reader.cursor != reader.length) return 1;
  PlayerData *player;
  if (getPlayerData(fd,&player) || (player->flags & 0x20)) return 1;
  commands_chat(player,(const char *)payload+start,size);
  return 0;
}
