#include "plates.h"
#include "mobs.h"
bool plates_enabled;
unsigned plate_current;
uint8_t player_plates[MAX_PLAYERS];
PlateType plate_types[PLATE_LIMIT];
bool plates_player_active (const PlayerData *p) {
  if (!plates_enabled) return true;
  for (unsigned i = 0; i < MAX_PLAYERS; i++) if (p == &player_data[i]) return player_plates[i] == plate_current;
  return false;
}
bool plates_fd_active (int fd) {
  if (!plates_enabled) return true;
  for (unsigned i = 0; i < MAX_PLAYERS; i++) if (player_data[i].client_fd == fd) return player_plates[i] == plate_current;
  return true; /* Status/login connections do not have a player slot yet. */
}
PlateType plates_type (void) { return plates_enabled ? plate_types[plate_current] : PLATE_BETANIUM; }
bool plates_mob_allowed (uint8_t type) {
  PlateType t = plates_type();
  if (t == PLATE_HUB || t == PLATE_FLATWORLD) return false;
  if (t == PLATE_VOLCANIC) return type == MOB_ZOMBIE_PIGMAN || type == MOB_GHAST;
  return type != MOB_ZOMBIE_PIGMAN && type != MOB_GHAST;
}
