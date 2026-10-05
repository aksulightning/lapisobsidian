#ifndef LAPISCLIENT_INTERNAL_H
#define LAPISCLIENT_INTERNAL_H
#ifdef LAPISCLIENT
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define LC_INPUT 4096
#define LC_OUTPUT (4u * 1024u * 1024u)
/* Bounded flat JSON objects: schemas intentionally need no recursive values. */
typedef struct {
  char key[32], text[1024];
  double number;
  char kind;
} LcField;
typedef struct {
  LcField fields[20];
  unsigned count;
} LcObject;
bool lc_utf8(const unsigned char *data, size_t size);
bool lc_parse(const char *data, size_t size, LcObject *out);
const LcField *lc_field(const LcObject *o, const char *key);
const char *lc_string(const LcObject *o, const char *key);
bool lc_number(const LcObject *o, const char *key, double min, double max,
               double *out);
bool lc_integer(const LcObject *o, const char *key, int min, int max, int *out);
bool lc_boolean(const LcObject *o, const char *key, bool *out);
typedef struct {
  int fd, stage; /* 0 HTTP, 1 hello, 2 login, 3 loading, 4 play */
  uint8_t input[LC_INPUT + 32], message[LC_INPUT];
  size_t used, message_size;
  uint8_t *output;
  size_t queued, sent;
  bool closing, fragmented;
  int64_t deadline, partial_since, last_rx, window, last_move, last_chat;
  unsigned messages, actions;
  double x, y, z, movement_budget, rise_budget, fall_budget;
} LcPeer;
extern LcPeer lc_peers[16];
LcPeer *lc_peer(int fd);
int lc_frame(int fd, unsigned opcode, const void *data, size_t n);
void lc_fail(LcPeer *p, const char *code, const char *message);
void lc_message(LcPeer *p, const char *data, size_t n);
#endif
#endif
