#ifndef H_LAPIS_SIGNS
#define H_LAPIS_SIGNS
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "globals.h"

#define SIGN_LIMIT 128
#define SIGN_LINE_MAX 96
#define SIGN_PACKET_MAX (9 + 4 * (5 + SIGN_LINE_MAX))
#define SIGN_NBT_MAX 1600

typedef struct {
  int16_t x, z;
  uint8_t y, orientation; /* 0..15 standing rotation; 16..19 north/south/west/east */
  uint8_t used;
  char lines[2][4][SIGN_LINE_MAX + 1]; /* front, back; plain UTF-8 */
} Sign;

bool signs_coords_valid (int x, int y, int z);
bool signs_text_valid (const uint8_t *text, size_t length);
const Sign *signs_at (int x, int y, int z);
void signs_reset_player (PlayerData *player);
bool signs_open (PlayerData *player, int x, int y, int z, bool front);
bool signs_interact (PlayerData *player, int x, int y, int z);
bool signs_place (PlayerData *player, int x, int y, int z, uint8_t face);
bool signs_edit (PlayerData *player, int x, int y, int z, bool front,
  const char lines[4][SIGN_LINE_MAX + 1]);
void signs_block_changed (int x, int y, int z, uint8_t block);
bool signs_load (const char *path); /* NULL selects memory-only operation */
bool signs_save (void);
void signs_send_chunk (int fd, int cx, int cz);
uint16_t signs_state (const Sign *sign);
size_t signs_nbt (const Sign *sign, uint8_t *out, size_t capacity);
int sc_sign (int fd, const Sign *sign);
int sc_signEditor (int fd, const Sign *sign, bool front);
int cs_updateSign (int fd, int length);
#endif
