#ifndef H_PACKET_INPUT
#define H_PACKET_INPUT
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <unistd.h>
/* Only implemented inbound packets are supported; no large plugin payloads. */
#define PACKET_INPUT_LIMIT 8192u
#define PACKET_INPUT_TIMEOUT_US INT64_C(15000000)
typedef struct { int64_t started; int fd; bool pending; } PacketInput;
void packet_input_reset (PacketInput *input, int fd);
/* Sockets must be nonblocking. -1: disconnect, 0: incomplete, 1: frame ready. */
int packet_input_poll (PacketInput *input, int64_t now);
/* A ready frame remains the only source for recv_all until end(). */
bool packet_input_read (int fd, void *buffer, size_t count, ssize_t *result);
size_t packet_input_remaining (void);
bool packet_input_end (void);
#endif
