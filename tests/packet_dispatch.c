/* Exercise real framed dispatch across movement's early-return boundaries. */
#include <assert.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include "commands.h"
#include "packet_dispatch.h"
#include "packet_input.h"
#include "procedures.h"
#include "tools.h"
#include "varnum.h"

static int sockets[2];
static PlayerData *player = &player_data[0];

/* These fixture IDs fit in one byte; length describes payload without that ID. */
static void begin(int id, int length) {
  writeVarInt(sockets[0], (uint32_t)(length + 1));
  assert(writeByte(sockets[0], (uint8_t)id) > 0);
}

/* Acceptance and full consumption are independent: invalid values may consume
 * a whole frame, while invalid lengths must reject before reading its payload. */
static void dispatch(int id, int accepted, int consumed) {
  PacketInput input;
  packet_input_reset(&input, sockets[1]);
  assert(packet_input_poll(&input, get_program_time()) == 1);
  recv_count = 1;
  assert(readVarInt(sockets[1]) == id);
  handlePacket(sockets[1], (int)packet_input_remaining(), id, STATE_PLAY);
  assert((recv_count > 0) == accepted);
  assert(packet_input_end() == consumed);
}

static void position(double x, double y, double z) {
  writeDouble(sockets[0], x);
  writeDouble(sockets[0], y);
  writeDouble(sockets[0], z);
}

int main(void) {
  assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, sockets));
  assert(!fcntl(sockets[1], F_SETFL, O_NONBLOCK));
  assert(commands_configure(NULL));
  for (unsigned i = 0; i < MAX_PLAYERS; i++) player_data[i].client_fd = -1;
  for (unsigned i = 0; i < MAX_BLOCK_CHANGES; i++) block_changes[i].block = 0xff;
  memset(player, 0, sizeof(*player));
  player->client_fd = sockets[1]; player->health = player->hunger = 20;
  player->x = player->z = 8; player->y = 80; player->saturation = 100;
  player->flags = 0x08; /* Sprinting consumes saturation only for position packets. */

  begin(0x20, 1); writeByte(sockets[0], 1); dispatch(0x20, 1, 1);
  assert(player->grounded_y == 80 && player->x == 8 && player->saturation == 100);
  begin(0x1F, 9); writeFloat(sockets[0], 90); writeFloat(sockets[0], 45);
  writeByte(sockets[0], 1); dispatch(0x1F, 1, 1);
  assert(player->yaw == 63 && player->pitch == 63);
  assert(player->x == 8 && player->y == 80 && player->z == 8 && player->saturation == 100);

  begin(0x1D, 25); position(9.5, 80, 8.5); writeByte(sockets[0], 1);
  dispatch(0x1D, 1, 1);
  assert(player->x == 9 && player->saturation == 99);
  begin(0x1E, 33); position(10.5, 80, 8.5);
  writeFloat(sockets[0], 0); writeFloat(sockets[0], 0); writeByte(sockets[0], 1);
  dispatch(0x1E, 1, 1);
  assert(player->x == 10 && player->yaw == 0 && player->saturation == 98);

  const double invalid[] = {NAN, INFINITY, -1, 256};
  for (unsigned i = 0; i < sizeof(invalid)/sizeof(invalid[0]); i++) {
    begin(0x1D, 25); position(12, invalid[i], 8); writeByte(sockets[0], 1);
    dispatch(0x1D, 0, 1);
    assert(player->x == 10 && player->y == 80 && player->saturation == 98);
  }
  begin(0x1F, 9); writeFloat(sockets[0], NAN); writeFloat(sockets[0], 0);
  writeByte(sockets[0], 1); dispatch(0x1F, 0, 1);
  assert(player->yaw == 0);
  /* Invalid lengths must return before dispatch discards the unread payload. */
  begin(0x20, 2); writeByte(sockets[0], 1); writeByte(sockets[0], 0);
  dispatch(0x20, 0, 0);
  /* A rejected frame cannot consume the next valid frame. */
  begin(0x20, 1); writeByte(sockets[0], 1); dispatch(0x20, 1, 1);
  close(sockets[0]); close(sockets[1]);
  puts("packet dispatch: movement variants, early returns and rejected frames passed");
  return 0;
}
