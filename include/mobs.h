#ifndef H_LAPIS_MOBS
#define H_LAPIS_MOBS
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "globals.h"

enum { MOB_CHICKEN=25, MOB_COW=28, MOB_CREEPER=30, MOB_PIG=95,
  MOB_GHAST=55, MOB_ZOMBIE_PIGMAN=148, MOB_SHEEP=106, MOB_SKELETON=110, MOB_SPIDER=119, MOB_ZOMBIE=145 };
#define MOB_ARROW_LIMIT 32
#define MOB_ARROW_BASE (-2048)
typedef struct { const char *name; uint8_t type, health; } MobType;
const MobType *mobs_by_name (const char *name);
bool mobs_spawn (uint8_t type, int x, int y, int z);
void mobs_spawn_exploration (int cx, int cz, int dx, int dz, int y, uint32_t random);
void mobs_tick (int64_t elapsed_us);
void mobs_tick_arrows (int64_t elapsed_us);
void mobs_tick_movement (int64_t elapsed_us);
void mobs_sync_player (PlayerData *player);
void mobs_forget_player (PlayerData *player);
void mobs_attacked (int entity_id, PlayerData *attacker);
void mobs_hurt_sound (int entity_id, bool death);
void mobs_clear (void);
size_t mobs_arrow_count (void);
/* Protocol-only helpers; sound names are fixed internal constants. */
void sc_mob_equipment (int fd, int id);
void sc_mob_move (int fd, int id, int16_t dx, int16_t dy, int16_t dz, uint8_t yaw, bool grounded);
void sc_creeper_fuse (int fd, int id, bool active);
void sc_mob_sound_category (int fd, const char *name, int x, int y, int z, uint8_t category);
void sc_mob_sound (int fd, const char *name, int x, int y, int z);
void sc_firecracker (int fd, int x, int y, int z);
void sc_arrow_metadata (int fd, int id);
#endif
