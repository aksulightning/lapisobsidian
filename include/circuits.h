#ifndef H_CIRCUITS
#define H_CIRCUITS
#include <stdbool.h>
#include <stdint.h>
#include "globals.h"
#define CIRCUIT_LIMIT 256
enum { CIRCUIT_DUST=1, CIRCUIT_TORCH, CIRCUIT_LEVER, CIRCUIT_NOTE,
  CIRCUIT_WALL_TORCH, CIRCUIT_STONE_PLATE, CIRCUIT_WOOD_PLATE, CIRCUIT_WOOD_TRAPDOOR, CIRCUIT_IRON_TRAPDOOR };
bool circuits_wall_torch_at (int x, int y, int z);
bool circuits_trapdoor_open (int x, int y, int z);
bool circuits_load (const char *path);
bool circuits_save (void);
bool circuits_place (PlayerData *p, int x, int y, int z, uint8_t face, uint16_t item);
bool circuits_interact (PlayerData *p, int x, int y, int z);
void circuits_strike (int x, int y, int z);
void circuits_block_changed (int x, int y, int z, uint8_t block);
void circuits_tick (void);
uint8_t circuits_power (int x, int y, int z);
bool circuits_powered (int x, int y, int z);
bool circuits_state_at (int x, int y, int z, uint8_t block, uint16_t *state);
uint16_t circuits_drop (int x, int y, int z, uint16_t fallback);
void circuits_send_chunk (int fd, int cx, int cz);
#endif
