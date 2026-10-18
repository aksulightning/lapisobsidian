/* Container, held-item, health and client lifecycle packet codecs. */
#include "procedures.h"
#include "packets.h"
#include "tools.h"
#include "plates.h"
#include "registries.h"
#include "varnum.h"
#include "inventory.h"

/* slot is already in client-window coordinates. Empty stacks omit item and
 * components; this legacy codec always emits state ID zero. */
int sc_setContainerSlot (int client_fd, int window_id, uint16_t slot, uint8_t count, uint16_t item) {

  writeVarInt(client_fd,
    1 +
    sizeVarInt(window_id) +
    1 + 2 +
    sizeVarInt(count) +
    (count > 0 ? sizeVarInt(item) + 2 : 0)
  );
  writeByte(client_fd, 0x14);

  writeVarInt(client_fd, window_id);
  writeVarInt(client_fd, 0);
  writeUint16(client_fd, slot);

  writeVarInt(client_fd, count);
  if (count > 0) {
    writeVarInt(client_fd, item);
    writeVarInt(client_fd, 0);
    writeVarInt(client_fd, 0);
  }

  return 0;

}

/* Use the supported menu type as both window ID and type. title is an NBT
 * string payload with an explicit byte length, not a NUL-terminated wire string. */
int sc_openScreen (int client_fd, uint8_t window, const char *title, uint16_t length) {

  writeVarInt(client_fd, 1 + 2 * sizeVarInt(window) + 1 + 2 + length);
  writeByte(client_fd, 0x34);

  writeVarInt(client_fd, window);
  writeVarInt(client_fd, window);

  writeByte(client_fd, 8); // string nbt tag
  writeUint16(client_fd, length); // string length
  send_all(client_fd, title, length);

  return 0;
}

/* Synchronize the carried stack independently of container slots. count zero
 * clears the cursor and omits item/component fields. */
int sc_setCursorItem (int client_fd, uint16_t item, uint8_t count) {

  writeVarInt(client_fd, 1 + sizeVarInt(count) + (count != 0 ? sizeVarInt(item) + 2 : 0));
  writeByte(client_fd, 0x59);

  writeVarInt(client_fd, count);
  if (count == 0) return 0;

  writeVarInt(client_fd, item);

  // Skip components
  writeByte(client_fd, 0);
  writeByte(client_fd, 0);

  return 0;
}

/* Accept only a complete two-byte hotbar index 0..8; invalid input returns 1
 * before changing selection. Full frame length is checked by dispatch. */
int cs_setHeldItem (int client_fd) {

  PlayerData *player;
  if (getPlayerData(client_fd, &player)) return 1;

  uint16_t slot = readUint16(client_fd);
  if (recv_count != 2 || slot >= 9) return 1;

  player->hotbar = (uint8_t)slot;

  return 0;
}

/* Select a client hotbar index (0..8); this codec does not change server state. */
int sc_setHeldItem (int client_fd, uint8_t slot) {

  writeVarInt(client_fd, sizeVarInt(0x62) + 1);
  writeVarInt(client_fd, 0x62);

  writeByte(client_fd, slot);

  return 0;
}

/* Require the currently open window before returning transient stacks through
 * inventory_close. A malformed or stale window ID returns 1 without cleanup. */
int cs_closeContainer (int client_fd) {

  int32_t window_id = readVarInt(client_fd);
  PlayerData *player;
  if (recv_count != 1 || window_id < 0 || getPlayerData(client_fd,&player) || window_id != inventory_window(player)) return 1;
  inventory_close(player);
  return 0;
}

/* Convert the server's movement-budget saturation to the client's float scale.
 * The 200-unit offset is part of that approximation, not stored food points. */
int sc_setHealth (int client_fd, uint8_t health, uint8_t food, uint16_t saturation) {

  writeVarInt(client_fd, 9 + sizeVarInt(food));
  writeByte(client_fd, 0x61);

  writeFloat(client_fd, (float)health);
  writeVarInt(client_fd, food);
  writeFloat(client_fd, (float)(saturation - 200) / 500.0f);

  return 0;
}

/* Action zero runs the respawn/reset sequence; other actions are ignored.
 * This handler does not itself require the saved health value to be zero. */
int cs_clientStatus (int client_fd) {

  uint8_t id = readByte(client_fd);

  PlayerData *player;
  if (getPlayerData(client_fd, &player)) return 1;

  if (id == 0) {
    sc_respawn(client_fd);
    resetPlayerData(player);
    spawnPlayer(player);
  }

  return 0;
}

/* Hand readiness to the session lifecycle; the tick timeout uses the same path. */
int cs_playerLoaded (int client_fd) {

  PlayerData *player;
  if (getPlayerData(client_fd, &player)) return 1;

  // Redirect handling to player join procedure
  handlePlayerJoin(player);

  return 0;
}

/* Emit a fixed keep-alive token; dispatch consumes replies without RTT tracking. */
int sc_keepAlive (int client_fd) {

  writeVarInt(client_fd, 9);
  writeByte(client_fd, 0x26);

  writeUint64(client_fd, 0);

  return 0;
}

/* ticks is the world's day clock. World age derives from process timing and is
 * not a persisted count; the final flag enables client day-clock progression. */
int sc_updateTime (int client_fd, uint64_t ticks) {

  writeVarInt(client_fd, 18);
  writeVarInt(client_fd, 0x6A);

  uint64_t world_age = get_program_time() / 50000;
  writeUint64(client_fd, world_age);
  writeUint64(client_fd, ticks);
  writeByte(client_fd, true);

  return 0;
}

/* Send non-overlay system text. len counts input UTF-8 bytes (maximum 512).
 * Returns 1 for rejected length/four-byte sequences before emitting a packet;
 * shorter byte sequences are copied, so this is not a full UTF-8 validator. */
int sc_systemChat (int client_fd, char* message, uint16_t len) {
  /* NBT TAG_String uses Java modified UTF-8. Supplementary characters use
   * two encoded UTF-16 surrogates, unlike the incoming chat string's UTF-8. */
  uint8_t encoded[1024]; size_t used = 0;
  if (!message || len > 512) return 1;
  for (size_t i = 0; i < len; i ++) {
    uint8_t ch = (uint8_t)message[i];
    if (!ch) { encoded[used++] = 0xc0; encoded[used++] = 0x80; }
    else if ((ch & 0xf8u) == 0xf0u) {
      if (i+3 >= len) return 1;
      uint32_t cp = ch & 7u;
      for (unsigned j = 1; j <= 3; j ++) {
        uint8_t next = (uint8_t)message[i+j];
        if ((next & 0xc0u) != 0x80u) return 1;
        cp = (cp<<6) | (next & 63u);
      }
      if (cp < 0x10000 || cp > 0x10ffff) return 1;
      cp -= 0x10000;
      uint32_t halves[2] = {0xd800u+(cp>>10),0xdc00u+(cp & 1023u)};
      for (unsigned j = 0; j < 2; j ++) {
        encoded[used++] = (uint8_t)(0xe0u | (halves[j]>>12));
        encoded[used++] = (uint8_t)(0x80u | ((halves[j]>>6) & 63u));
        encoded[used++] = (uint8_t)(0x80u | (halves[j] & 63u));
      }
      i += 3;
    } else encoded[used++] = ch;
  }
  writeVarInt(client_fd,(uint32_t)(5+used)); writeByte(client_fd,0x72);
  writeByte(client_fd,8); writeUint16(client_fd,(uint16_t)used);
  send_all(client_fd,encoded,(ssize_t)used); writeByte(client_fd,false);
  return 0;
}
