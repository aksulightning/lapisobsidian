/* Shared arithmetic, transport, wire codecs and process timing helpers. */
#ifndef H_TOOLS
#define H_TOOLS

#include <unistd.h>

#include "globals.h"

/* For positive b, keep wrapped coordinates in [0,b), including negative a.
 * The intermediate (a % b) + b must fit in int. */
static inline int mod_abs (int a, int b) {
  return ((a % b) + b) % b;
}
/* For positive b, use floor division so negative world coordinates select the
 * correct chunk. Callers must keep the intermediate a-b within int range. */
static inline int div_floor (int a, int b) {
  return a % b < 0 ? (a - b) / b : a / b;
}

extern uint64_t total_bytes_received;
ssize_t recv_all (int client_fd, void *buf, size_t n, uint8_t require_first);
ssize_t send_all (int client_fd, const void *buf, ssize_t len);
void discard_all (int client_fd, size_t remaining, uint8_t require_first);

/* Fixed-width writers emit big-endian wire values and return send_all's result.
 * Floating-point codecs preserve the value's bit representation via memcpy. */
ssize_t writeByte (int client_fd, uint8_t byte);
ssize_t writeUint16 (int client_fd, uint16_t num);
ssize_t writeUint32 (int client_fd, uint32_t num);
ssize_t writeUint64 (int client_fd, uint64_t num);
ssize_t writeFloat (int client_fd, float num);
ssize_t writeDouble (int client_fd, double num);

/* Primitive reads reuse recv_buffer and leave the latest read size in recv_count.
 * Returned numeric values alone cannot distinguish valid data from a short read. */
uint8_t readByte (int client_fd);
uint16_t readUint16 (int client_fd);
int16_t readInt16 (int client_fd);
uint32_t readUint32 (int client_fd);
uint64_t readUint64 (int client_fd);
int64_t readInt64 (int client_fd);
float readFloat (int client_fd);
double readDouble (int client_fd);

ssize_t readLengthPrefixedData (int client_fd);
void readString (int client_fd);
void readStringN (int client_fd, uint32_t max_length);

uint32_t fast_rand ();
uint64_t splitmix64 (uint64_t state);

#ifdef ESP_PLATFORM
  #include "esp_timer.h"
  #define get_program_time esp_timer_get_time
#else
  int64_t get_program_time ();
#endif

/* Saved player names occupy exactly 16 bytes and need not end in NUL. */
unsigned player_name_length(const char name[16]);
#endif
