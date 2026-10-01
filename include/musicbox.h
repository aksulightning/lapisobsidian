#ifndef H_MUSICBOX
#define H_MUSICBOX
#include "commands.h"
#define MUSICBOX_SONG_LIMIT 16
#define MUSICBOX_PLAYER_LIMIT 2
bool musicbox_init (const char *folder);
void musicbox_shutdown (void);
void musicbox_reset_player (PlayerData *p);
void musicbox_menu (PlayerData *p, int x, int y, int z);
CommandResult musicbox_command (PlayerData *p, int argc, char *const argv[]);
void musicbox_tick (int64_t elapsed_us);
void musicbox_block_changed (int x, int y, int z);
#endif
