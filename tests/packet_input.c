#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include "globals.h"
#include "tools.h"
#include "packets.h"
#include "varnum.h"

static int input (const uint8_t *data, size_t size) {
  int sockets[2]; assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
  assert(send(sockets[0], data, size, 0) == (ssize_t)size);
  close(sockets[0]);
  return sockets[1];
}
int main (void) {
  static const uint8_t valid_pack[] = {
    1,9,'m','i','n','e','c','r','a','f','t',4,'c','o','r','e',6,'1','.','2','1','.','8'
  };
  int fd = input(valid_pack, sizeof(valid_pack));
  assert(cs_knownPacks(fd) == 0); close(fd);
  for (size_t n = 0; n < sizeof(valid_pack); n ++) {
    fd = input(valid_pack, n); assert(cs_knownPacks(fd) != 0); close(fd);
  }
  uint8_t wrong[sizeof(valid_pack)]; memcpy(wrong, valid_pack, sizeof(wrong));
  wrong[sizeof(wrong)-1] = '7';
  fd = input(wrong, sizeof(wrong)); assert(cs_knownPacks(fd) != 0); close(fd);
  uint8_t huge[] = {0xff,0xff,0xff,0xff,0x7f};
  fd = input(huge, sizeof(huge)); assert(readVarInt(fd) == VARNUM_ERROR && recv_count == 0); close(fd);
  uint8_t overlong[] = {0x80,0x80,0x80,0x80,0x80,0};
  fd = input(overlong, sizeof(overlong)); assert(readVarInt(fd) == VARNUM_ERROR); close(fd);
  uint8_t truncated[] = {0x80};
  fd = input(truncated, sizeof(truncated)); assert(readVarInt(fd) == VARNUM_ERROR); close(fd);
  uint8_t negative[] = {0xff,0xff,0xff,0xff,0x0f};
  fd = input(negative, sizeof(negative)); assert(readVarInt(fd) == -1); close(fd);
  uint8_t maximum[] = {0xff,0xff,0xff,0xff,0x07};
  fd = input(maximum, sizeof(maximum)); assert(readVarInt(fd) == INT32_MAX); close(fd);
  uint8_t short_string[] = {4,'a','b'};
  fd = input(short_string, sizeof(short_string)); readString(fd); assert(recv_count < 0); close(fd);
  fd = input(negative, sizeof(negative)); readString(fd); assert(recv_count < 0); close(fd);
  uint8_t negative_long[8]; memset(negative_long, 255, sizeof(negative_long));
  fd = input(negative_long, sizeof(negative_long)); assert(readInt64(fd) == -1); close(fd);
  uint64_t invalid_position = (((uint64_t)(int64_t)-32769 & 0x3ffffffu) << 38) | 64u;
  uint8_t action[11] = {0};
  for (unsigned i = 0; i < 8; i ++) action[1+i] = (uint8_t)(invalid_position >> (56-8*i));
  fd = input(action, sizeof(action)); assert(cs_playerAction(fd, sizeof(action)) != 0); close(fd);
  puts("packet input: exact known-pack negotiation, malformed/truncated VarInts and strings passed");
  return 0;
}
