/* Big-endian primitive and bounded string codecs for the Java protocol. */
#include "tools.h"
#include "varnum.h"
#include <string.h>
#include <errno.h>
#ifdef ESP_PLATFORM
#include "lwip/sockets.h"
#elif defined(_WIN32)
#include <winsock2.h>
#else
#include <arpa/inet.h>
#endif

#ifndef htonll

static uint64_t htonll (uint64_t value) {
  #if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    return ((uint64_t)htonl((uint32_t)(value >> 32))) |
           ((uint64_t)htonl((uint32_t)(value & 0xFFFFFFFF)) << 32);
  #else
    return value;
  #endif
  }
#endif

ssize_t writeByte (int client_fd, uint8_t byte) {
  return send_all(client_fd, &byte, 1);
}
ssize_t writeUint16 (int client_fd, uint16_t num) {
  uint16_t be = htons(num);
  return send_all(client_fd, &be, sizeof(be));
}
ssize_t writeUint32 (int client_fd, uint32_t num) {
  uint32_t be = htonl(num);
  return send_all(client_fd, &be, sizeof(be));
}
ssize_t writeUint64 (int client_fd, uint64_t num) {
  uint64_t be = htonll(num);
  return send_all(client_fd, &be, sizeof(be));
}
ssize_t writeFloat (int client_fd, float num) {
  uint32_t bits;
  memcpy(&bits, &num, sizeof(bits));
  bits = htonl(bits);
  return send_all(client_fd, &bits, sizeof(bits));
}
ssize_t writeDouble (int client_fd, double num) {
  uint64_t bits;
  memcpy(&bits, &num, sizeof(bits));
  bits = htonll(bits);
  return send_all(client_fd, &bits, sizeof(bits));
}

uint8_t readByte (int client_fd) {
  memset(recv_buffer,0,1);
  recv_count = recv_all(client_fd, recv_buffer, 1, false);
  return recv_buffer[0];
}
uint16_t readUint16 (int client_fd) {
  memset(recv_buffer,0,2);
  recv_count = recv_all(client_fd, recv_buffer, 2, false);
  return ((uint16_t)recv_buffer[0] << 8) | recv_buffer[1];
}
int16_t readInt16 (int client_fd) {
  memset(recv_buffer,0,2);
  recv_count = recv_all(client_fd, recv_buffer, 2, false);
  return ((int16_t)recv_buffer[0] << 8) | (int16_t)recv_buffer[1];
}
uint32_t readUint32 (int client_fd) {
  memset(recv_buffer,0,4);
  recv_count = recv_all(client_fd, recv_buffer, 4, false);
  return ((uint32_t)recv_buffer[0] << 24) |
         ((uint32_t)recv_buffer[1] << 16) |
         ((uint32_t)recv_buffer[2] << 8) |
         ((uint32_t)recv_buffer[3]);
}
uint64_t readUint64 (int client_fd) {
  memset(recv_buffer,0,8);
  recv_count = recv_all(client_fd, recv_buffer, 8, false);
  return ((uint64_t)recv_buffer[0] << 56) |
         ((uint64_t)recv_buffer[1] << 48) |
         ((uint64_t)recv_buffer[2] << 40) |
         ((uint64_t)recv_buffer[3] << 32) |
         ((uint64_t)recv_buffer[4] << 24) |
         ((uint64_t)recv_buffer[5] << 16) |
         ((uint64_t)recv_buffer[6] << 8) |
         ((uint64_t)recv_buffer[7]);
}
int64_t readInt64 (int client_fd) {
  uint64_t value = readUint64(client_fd);
  return value <= INT64_MAX ? (int64_t)value : -1 - (int64_t)(UINT64_MAX - value);
}
float readFloat (int client_fd) {
  uint32_t bytes = readUint32(client_fd);
  float output;
  memcpy(&output, &bytes, sizeof(output));
  return output;
}
double readDouble (int client_fd) {
  uint64_t bytes = readUint64(client_fd);
  double output;
  memcpy(&output, &bytes, sizeof(output));
  return output;
}

// Receive length prefixed data with bounds checking
ssize_t readLengthPrefixedData (int client_fd) {
  int32_t length = readVarInt(client_fd);
  if (length < 0 || length >= MAX_RECV_BUF_LEN) {
    errno = EINVAL;
    recv_count = -1;
    return -1;
  }
  ssize_t received = recv_all(client_fd, recv_buffer, (size_t)length, false);
  if (received != length) {
    errno = EINVAL;
    return -1;
  }
  return received;
}

// Reads a networked string into recv_buffer
void readString (int client_fd) {
  recv_count = readLengthPrefixedData(client_fd);
  if (recv_count < 0 || (size_t)recv_count >= sizeof(recv_buffer)) {
    recv_buffer[0] = '\0';
    recv_count = -1;
    return;
  }
  recv_buffer[recv_count] = '\0';
}
// Reads a networked string of up to N bytes into recv_buffer
void readStringN (int client_fd, uint32_t max_length) {
  readString(client_fd);
  if (recv_count < 0) return;
  if (max_length < (uint32_t)recv_count) recv_buffer[max_length] = '\0';
}
