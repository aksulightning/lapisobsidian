#ifndef H_PLATES
#define H_PLATES
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "globals.h"
#include "commands.h"
#define PLATE_LIMIT 8
#define PLATE_NAME_MAX 24
typedef enum { PLATE_BETANIUM, PLATE_HUB, PLATE_FLATWORLD, PLATE_SKYBOX, PLATE_VOLCANIC } PlateType;
/* These small accessors are also linked by legacy unit tests. */
extern bool plates_enabled;
extern unsigned plate_current;
extern uint8_t player_plates[MAX_PLAYERS];
extern PlateType plate_types[PLATE_LIMIT];
bool plates_player_active (const PlayerData *p);
bool plates_fd_active (int fd);
PlateType plates_type (void);
bool plates_mob_allowed (uint8_t type);
uint8_t plates_terrain (int x, int y, int z);
bool plates_start (void);
void plates_shutdown (void);
bool plates_select (unsigned id);
void plates_select_for_fd (int fd);
void plates_player_reset (PlayerData *p);
bool plates_load (unsigned id);
bool plates_is_loaded (unsigned id);
bool plates_create (const char *name, uint64_t seed, PlateType type);
bool plates_remove (const char *name);
int plates_find (const char *name);
const char *plates_name (unsigned id);
const char *plates_dimension (unsigned id);
unsigned plates_dimension_count (void);
const char *plates_dimension_at (unsigned index);
const char *plates_world_path (void);
const char *plates_type_name (PlateType type);
bool plates_travel (PlayerData *p, const char *name);
bool plates_movement_guard (PlayerData *p, double x, double y, double z);
CommandResult plates_command (PlayerData *p, int argc, char *const argv[]);
#endif
