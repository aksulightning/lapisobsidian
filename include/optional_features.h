#ifndef H_OPTIONAL_FEATURES
#define H_OPTIONAL_FEATURES
#include <stdbool.h>

/* Build scripts opt in only for an environment value of exactly 1.
 * Direct compiler invocations may use -DNAME=1. Never read runtime getenv(). */
#ifndef LAPIS_WORLD_EDIT
#define LAPIS_WORLD_EDIT 0
#endif
#ifndef LAPIS_TREE_CHOPPER
#define LAPIS_TREE_CHOPPER 0
#endif

#if LAPIS_WORLD_EDIT == 1
#include "commands.h"
void world_edit_reset_player (PlayerData *player);
CommandResult world_edit_command (PlayerData *player, int argc, char *const argv[]);
#endif
#if LAPIS_TREE_CHOPPER == 1
#include "globals.h"
bool tree_chopper_try (PlayerData *player, int x, int y, int z);
#endif
#endif
