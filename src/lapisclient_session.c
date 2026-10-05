#ifdef LAPISCLIENT
#include "circuits.h"
#include "commands.h"
#include "doors.h"
#include "farming.h"
#include "globals.h"
#include "lapisclient.h"
#include "lapisclient_internal.h"
#include "packet_input.h"
#include "packets.h"
#include "plates.h"
#include "procedures.h"
#include "registries.h"
#include "registry.h"
#include "tools.h"
#include "worldgen.h"
#include <math.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* Adapter to the existing bounded controller entry point. Only this allowlist
 * constructs controller arguments. Neither raw bytes nor packet IDs are exposed
 * by lapisclient. Existing gameplay checks remain in the shared handlers. */
extern void handlePacket(int fd, int length, int id, int state);
typedef struct {
  uint8_t bytes[1024];
  size_t n;
} Args;
static void u8(Args *a, unsigned n) { a->bytes[a->n++] = (uint8_t)n; }
static void be(Args *a, uint64_t n, unsigned size) {
  for (unsigned i = size; i; i--)
    u8(a, (unsigned)(n >> ((i - 1) * 8)));
}
static void vi(Args *a, uint32_t n) {
  do {
    u8(a, (n & 127) | (n > 127 ? 128 : 0));
    n >>= 7;
  } while (n);
}
static void f32(Args *a, double n) {
  float v = (float)n;
  uint32_t raw;
  memcpy(&raw, &v, 4);
  be(a, raw, 4);
}
static void f64(Args *a, double n) {
  uint64_t raw;
  memcpy(&raw, &n, 8);
  be(a, raw, 8);
}
static void pos(Args *a, int x, int y, int z) {
  be(a,
     (((uint64_t)(int64_t)x & 0x3ffffff) << 38) |
         (((uint64_t)(int64_t)z & 0x3ffffff) << 12) | ((unsigned)y & 4095),
     8);
}
static void str(Args *a, const char *s) {
  size_t n = strlen(s);
  vi(a, (uint32_t)n);
  memcpy(a->bytes + a->n, s, n);
  a->n += n;
}
static bool dispatch(LcPeer *p, int id, Args *a) {
  if (!packet_input_begin(p->fd, a->bytes, a->n))
    return false;
  recv_count = 1;
  handlePacket(p->fd, (int)a->n, id, STATE_PLAY);
  bool ok = packet_input_end();
  return ok && recv_count > 0;
}
static bool schema(const LcObject *o, const char *keys) {
  for (unsigned i = 0; i < o->count; i++) {
    char pattern[40];
    snprintf(pattern, sizeof(pattern), " %s ", o->fields[i].key);
    if (!strstr(keys, pattern))
      return false;
  }
  return true;
}
int lc_position(int fd, double x, double y, double z, float yaw, float pitch) {
  LcPeer *p = lc_peer(fd);
  if (!p)
    return 1;
  p->x = x;
  p->y = y;
  p->z = z;
  p->last_move = get_program_time();
  p->movement_budget = 2;
  p->rise_budget = 2;
  p->fall_budget = 3;
  return lc_json(fd,
                 "{\"type\":\"player_position\",\"x\":%.9g,\"y\":%.9g,\"z\":%."
                 "9g,\"yaw\":%.7g,\"pitch\":%.7g}",
                 x, y, z, (double)yaw, (double)pitch);
}
int lc_chunk(int fd, int x, int z) {
  if (x < -2048 || x > 2047 || z < -2048 || z > 2047)
    return 1;
  /* v1: kind 1, version 1, x/z i32, minY i16, height u16, 256 u16
   * render-state IDs, then height*256 u8 palette indices; all little endian. */
  uint8_t *data = malloc(14 + 512 + 98304);
  if (!data)
    return 1;
  data[0] = 1;
  data[1] = 1;
  for (unsigned i = 0; i < 4; i++) {
    data[2 + i] = (uint8_t)((uint32_t)x >> (i * 8));
    data[6 + i] = (uint8_t)((uint32_t)z >> (i * 8));
  }
  data[10] = 192;
  data[11] = 255;
  data[12] = 128;
  data[13] = 1;
  for (unsigned i = 0; i < 256; i++) {
    uint16_t state = 0;
    registry_block_state(i, &state);
    data[14 + i * 2] = (uint8_t)state;
    data[15 + i * 2] = (uint8_t)(state >> 8);
  }
  uint8_t *blocks = data + 526;
  memset(blocks, B_bedrock, 16384);
  for (int section = 0; section < 20; section++) {
    buildChunkSection(x * 16, section * 16, z * 16);
    for (unsigned i = 0; i < 4096; i++)
      blocks[16384 + (unsigned)section * 4096 + i] = chunk_section[i ^ 7u];
  }
  int result = lc_frame(fd, 2, data, 98830);
  free(data);
  for (int i = 0; i < block_changes_count; i++) {
    BlockChange *b = &block_changes[i];
    if ((b->block == B_torch || b->block == B_chest) && b->x >= x * 16 &&
        b->x < x * 16 + 16 && b->z >= z * 16 && b->z < z * 16 + 16)
      sc_blockUpdate(fd, b->x, b->y, b->z, b->block);
    if (b->block == B_chest)
      i += 14;
  }
  doors_send_chunk(fd, x, z);
  circuits_send_chunk(fd, x, z);
  farming_send_chunk(fd, x, z);
  return result;
}
static void login(LcPeer *p, const LcObject *o) {
  const char *name = lc_string(o, "username"), *token = lc_string(o, "token"),
             *required = getenv("LAPIS_WS_TOKEN");
  if (!schema(o, " type  username  token ") || !name || !strlen(name) ||
      strlen(name) > 15 ||
      strspn(
          name,
          "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_") !=
          strlen(name)) {
    lc_fail(p, "invalid_login",
            "Username must be 1-15 ASCII letters, digits or underscores");
    return;
  }
  if (required && (!token || strlen(token) != strlen(required) ||
                   CRYPTO_memcmp(token, required, strlen(required)))) {
    lc_fail(p, "authentication_failed", "Invalid server access token");
    return;
  }
  unsigned char hash[EVP_MAX_MD_SIZE];
  unsigned size;
  char identity[64], username[16] = {0};
  snprintf(identity, sizeof(identity), "LapisObsidian:%s", name);
  memcpy(username, name, strlen(name));
  EVP_Digest(identity, strlen(identity), hash, &size, EVP_sha256(), NULL);
  hash[6] = (hash[6] & 15) | 64;
  hash[8] = (hash[8] & 63) | 128;
  for (int i = 0; i < MAX_PLAYERS; i++)
    if (player_data[i].client_fd >= 0 &&
        (!memcmp(hash, player_data[i].uuid, 16) ||
         !strcmp(name, player_data[i].name))) {
      lc_fail(p, "already_online", "This player is already connected");
      return;
    }
  if (reservePlayerData(p->fd, hash, username)) {
    lc_fail(p, "server_full", "No player slot is available");
    return;
  }
  setClientState(p->fd, STATE_PLAY);
  plates_select_for_fd(p->fd);
  PlayerData *player;
  if (getPlayerData(p->fd, &player)) {
    lc_fail(p, "session_error", "Unable to initialize player");
    return;
  }
  p->stage = 3;
  p->deadline = get_program_time() + 20000000;
  lc_json(p->fd,
          "{\"type\":\"login_ok\",\"entity\":%d,\"username\":\"%s\","
          "\"authentication\":\"offline\"}",
          p->fd, name);
  sc_loginPlay(p->fd);
  spawnPlayer(player);
  for (int i = 0; i < MAX_PLAYERS; i++)
    if (plates_player_active(&player_data[i]) &&
        player_data[i].client_fd >= 0 && !(player_data[i].flags & 0x20) &&
        &player_data[i] != player)
      sc_spawnEntityPlayer(p->fd, player_data[i]);
}
void lc_message(LcPeer *p, const char *data, size_t n) {
  int64_t now = get_program_time();
  if (now - p->window >= 1000000) {
    p->window = now;
    p->messages = 0;
    p->actions = 0;
  }
  if (++p->messages > 100) {
    lc_fail(p, "rate_limit", "Too many messages");
    return;
  }
  LcObject o;
  const char *type;
  if (!lc_parse(data, n, &o) || !(type = lc_string(&o, "type"))) {
    lc_fail(p, "invalid_message", "Expected a bounded JSON object with a type");
    return;
  }
  if (!strcmp(type, "disconnect") && schema(&o, " type ")) {
    lc_json(p->fd, "{\"type\":\"disconnect\",\"reason\":\"Client left\"}");
    uint8_t code[] = {3, 232};
    lc_frame(p->fd, 8, code, 2);
    p->closing = true;
    p->deadline = now + 1000000;
    return;
  }
  if (p->stage == 1) {
    int version;
    const char *protocol = lc_string(&o, "protocol");
    if (strcmp(type, "hello") || !schema(&o, " type  protocol  version ") ||
        !protocol || strcmp(protocol, "lapisclient") ||
        !lc_integer(&o, "version", 1, 1, &version)) {
      lc_fail(p, "protocol_mismatch", "Expected lapisclient version 1");
      return;
    }
    p->stage = 2;
    p->deadline = now + 10000000;
    lc_json(p->fd,
            "{\"type\":\"welcome\",\"protocol\":\"lapisclient\",\"version\":1,"
            "\"authentication\":\"offline\",\"tokenRequired\":%s}",
            getenv("LAPIS_WS_TOKEN") ? "true" : "false");
    return;
  }
  if (p->stage == 2) {
    if (strcmp(type, "login")) {
      lc_fail(p, "invalid_state", "Expected login");
      return;
    }
    login(p, &o);
    return;
  }
  if (!strcmp(type, "ping") && schema(&o, " type ")) {
    lc_json(p->fd, "{\"type\":\"pong\"}");
    return;
  }
  PlayerData *player;
  if (getPlayerData(p->fd, &player)) {
    lc_fail(p, "session_error", "Player session missing");
    return;
  }
  if (p->stage == 3) {
    if (strcmp(type, "ready") || !schema(&o, " type ")) {
      lc_fail(p, "invalid_state", "Expected ready after initial world");
      return;
    }
    p->stage = 4;
    handlePlayerJoin(player);
    lc_json(p->fd, "{\"type\":\"ready\"}");
    return;
  }
  if (!strcmp(type, "ready") && schema(&o, " type ")) {
    lc_json(p->fd, "{\"type\":\"ready\"}");
    return;
  }
  Args a = {0};
  int id = -1, x, y, z, face, slot, window, button, entity;
  bool flag;
  double dx, dy, dz, yaw, pitch;
  if (!strcmp(type, "player_move")) {
    if (!schema(&o, " type  x  y  z  yaw  pitch  onGround ") ||
        !lc_number(&o, "x", -32767, 32767, &dx) ||
        !lc_number(&o, "y", 0, 255.999, &dy) ||
        !lc_number(&o, "z", -32767, 32767, &dz) ||
        !lc_number(&o, "yaw", -360, 360, &yaw) ||
        !lc_number(&o, "pitch", -90, 90, &pitch) ||
        !lc_boolean(&o, "onGround", &flag))
      goto invalid;
    /* Time-based horizontal budget prevents unlimited motion by packet spam.
     * Vertical terminal velocity + jump bound; legacy border/plate/fall checks
     * run below. Collision prediction remains client-side as in TCP gameplay.
     */
    double dt = fmin(1.0, (double)(now - p->last_move) / 1000000.0);
    p->last_move = now;
    p->movement_budget = fmin(2.0, p->movement_budget + dt * 6.0);
    p->rise_budget = fmin(2.0, p->rise_budget + dt * 10.0);
    p->fall_budget = fmin(3.0, p->fall_budget + dt * 55.0);
    double distance = hypot(dx - p->x, dz - p->z);
    if (distance > p->movement_budget || dy - p->y > p->rise_budget ||
        p->y - dy > p->fall_budget || !player->health) {
      sc_synchronizePlayerPosition(p->fd, p->x, p->y, p->z,
                                   player->yaw * 180.0f / 127,
                                   player->pitch * 90.0f / 127);
      p->movement_budget = p->rise_budget = p->fall_budget = 0;
      return;
    }
    p->movement_budget -= distance;
    if (dy > p->y)
      p->rise_budget -= dy - p->y;
    else
      p->fall_budget -= p->y - dy;
    p->x = dx;
    p->y = dy;
    p->z = dz;
    id = 0x1e;
    f64(&a, dx);
    f64(&a, dy);
    f64(&a, dz);
    f32(&a, yaw);
    f32(&a, pitch);
    u8(&a, flag);
  } else if (!strcmp(type, "player_input")) {
    if (!schema(&o, " type  flags ") || !lc_integer(&o, "flags", 0, 63, &slot))
      goto invalid;
    id = 0x2a;
    u8(&a, (unsigned)slot);
  } else if (!strcmp(type, "selected_slot")) {
    if (!schema(&o, " type  slot ") || !lc_integer(&o, "slot", 0, 8, &slot))
      goto invalid;
    id = 0x34;
    be(&a, (unsigned)slot, 2);
  } else if (!strcmp(type, "chat_send")) {
    const char *text = lc_string(&o, "text");
    if (!schema(&o, " type  text ") || !text || !strlen(text) ||
        strlen(text) > 224)
      goto invalid;
    if (now - p->last_chat < 500000) {
      lc_fail(p, "rate_limit", "Chat is limited to two messages per second");
      return;
    }
    p->last_chat = now;
    /* Chat does not expose administrative commands, including /admin. */
    if (text[0] == '/') {
      lc_text(p->fd, "chat_message", "text",
              "Commands are unavailable on lapisclient v1.", 41);
      return;
    }
    id = 8;
    str(&a, text);
    be(&a, 0, 8);
    be(&a, 0, 8);
    u8(&a, 0);
    vi(&a, 0);
    be(&a, 0, 4);
  } else if (!strcmp(type, "action")) {
    const char *action = lc_string(&o, "action");
    if (!schema(&o, " type  action  x  y  z  face ") || !action)
      goto invalid;
    int action_id = !strcmp(action, "break_start")    ? 0
                    : !strcmp(action, "break_cancel") ? 1
                    : !strcmp(action, "break_finish") ? 2
                    : !strcmp(action, "drop_stack")   ? 3
                    : !strcmp(action, "drop_item")    ? 4
                    : !strcmp(action, "release_use")  ? 5
                                                      : -1;
    if (action_id < 0)
      goto invalid;
    x = y = z = face = 0;
    if (action_id < 3 && (!lc_integer(&o, "x", -32767, 32767, &x) ||
                          !lc_integer(&o, "y", 0, 255, &y) ||
                          !lc_integer(&o, "z", -32767, 32767, &z) ||
                          !lc_integer(&o, "face", 0, 5, &face)))
      goto invalid;
    id = 0x28;
    vi(&a, (unsigned)action_id);
    pos(&a, x, y, z);
    u8(&a, (unsigned)face);
    vi(&a, 0);
  } else if (!strcmp(type, "interact")) {
    if (!schema(&o, " type  x  y  z  face ") ||
        !lc_integer(&o, "x", -32767, 32767, &x) ||
        !lc_integer(&o, "y", 0, 255, &y) ||
        !lc_integer(&o, "z", -32767, 32767, &z) ||
        !lc_integer(&o, "face", 0, 5, &face))
      goto invalid;
    id = 0x3f;
    vi(&a, 0);
    pos(&a, x, y, z);
    vi(&a, (unsigned)face);
    f32(&a, .5);
    f32(&a, .5);
    f32(&a, .5);
    u8(&a, 0);
    u8(&a, 0);
    vi(&a, 0);
  } else if (!strcmp(type, "item_use")) {
    if (!schema(&o, " type  yaw  pitch ") ||
        !lc_number(&o, "yaw", -360, 360, &yaw) ||
        !lc_number(&o, "pitch", -90, 90, &pitch))
      goto invalid;
    id = 0x40;
    vi(&a, 0);
    vi(&a, 0);
    f32(&a, yaw);
    f32(&a, pitch);
  } else if (!strcmp(type, "entity_interact")) {
    const char *action = lc_string(&o, "action");
    if (!schema(&o, " type  entity  action  sneak ") || !action ||
        !lc_integer(&o, "entity", -1000000, 1000000, &entity) ||
        !lc_boolean(&o, "sneak", &flag))
      goto invalid;
    int attack = !strcmp(action, "attack");
    if (!attack && strcmp(action, "use"))
      goto invalid;
    id = 0x19;
    vi(&a, (uint32_t)entity);
    vi(&a, (unsigned)attack);
    if (!attack)
      vi(&a, 0);
    u8(&a, flag);
  } else if (!strcmp(type, "inventory_click")) {
    if (!schema(&o, " type  window  slot  button  shift ") ||
        !lc_integer(&o, "window", 0, 14, &window) ||
        !lc_integer(&o, "slot", 0, 62, &slot) ||
        !lc_integer(&o, "button", 0, 1, &button) ||
        !lc_boolean(&o, "shift", &flag))
      goto invalid;
    id = 0x11;
    vi(&a, (unsigned)window);
    vi(&a, 0);
    be(&a, (unsigned)slot, 2);
    u8(&a, (unsigned)button);
    vi(&a, flag ? 1 : 0);
    vi(&a, 0);
    u8(&a, 0);
  } else if (!strcmp(type, "inventory_close")) {
    if (!schema(&o, " type  window ") ||
        !lc_integer(&o, "window", 0, 14, &window))
      goto invalid;
    id = 0x12;
    vi(&a, (unsigned)window);
  } else if (!strcmp(type, "creative_slot")) {
    int item;
    if (!schema(&o, " type  slot  item ") ||
        !lc_integer(&o, "slot", 5, 45, &slot) ||
        !lc_integer(&o, "item", 1, 65535, &item) ||
        !registry_item_id_valid((unsigned)item))
      goto invalid;
    id = 0x37;
    be(&a, (unsigned)slot, 2);
    vi(&a, 64);
    vi(&a, (unsigned)item);
    vi(&a, 0);
    vi(&a, 0);
  } else if (!strcmp(type, "respawn")) {
    if (!schema(&o, " type "))
      goto invalid;
    if (player->health)
      return;
    id = 0x0b;
    vi(&a, 0);
  } else if (!strcmp(type, "swing")) {
    if (!schema(&o, " type "))
      goto invalid;
    id = 0x3c;
    vi(&a, 0);
  } else {
    lc_fail(p, "unknown_type", "Unknown message type");
    return;
  }
  if (id != 0x1e && id != 0x2a && ++p->actions > 30) {
    lc_fail(p, "rate_limit", "Too many actions");
    return;
  }
  if (!dispatch(p, id, &a))
    goto invalid;
  if (id == 0x34)
    sc_setHeldItem(p->fd, player->hotbar);
  if (id == 0x1e)
    lc_json(p->fd,
            "{\"type\":\"player_update\",\"x\":%.9g,\"y\":%.9g,\"z\":%.9g}",
            p->x, p->y, p->z);
  return;
invalid:
  lc_fail(p, "invalid_message",
          "Invalid fields or rejected controller arguments");
}
#endif
