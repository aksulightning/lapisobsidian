#include <stdint.h>
#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
#else
  #include <arpa/inet.h>
#endif
#include <unistd.h>

#include "varnum.h"
#include "globals.h"
#include "tools.h"

int32_t readVarInt (int client_fd) {
  uint32_t value = 0;
  for (unsigned position = 0; position < 35; position += 7) {
    uint8_t byte = readByte(client_fd);
    if (recv_count != 1 || (position == 28 && (byte & 0xf0u))) {
      recv_count = 0;
      return VARNUM_ERROR;
    }
    value |= (uint32_t)(byte & SEGMENT_BITS) << position;
    if (!(byte & CONTINUE_BIT)) {
      /* Convert Java's signed bit pattern without an overflowing C cast. */
      return value <= INT32_MAX ? (int32_t)value : -1 - (int32_t)(UINT32_MAX - value);
    }
  }
  recv_count = 0;
  return VARNUM_ERROR;
}

int sizeVarInt (uint32_t value) {
  int size = 1;
  while ((value & ~SEGMENT_BITS) != 0) {
    value >>= 7;
    size ++;
  }
  return size;
}

void writeVarInt (int client_fd, uint32_t value) {
  while (true) {
    if ((value & ~SEGMENT_BITS) == 0) {
      writeByte(client_fd, value);
      return;
    }

    writeByte(client_fd, (value & SEGMENT_BITS) | CONTINUE_BIT);

    value >>= 7;
  }
}
