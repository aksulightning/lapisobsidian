#ifndef H_COMMANDS
#define H_COMMANDS
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "globals.h"

#define COMMAND_MAX_BYTES 256
#define COMMAND_MAX_ARGS 6
#define COMMAND_TOKEN_MAX 128

typedef enum { COMMAND_OK, COMMAND_USAGE, COMMAND_UNKNOWN, COMMAND_DENIED, COMMAND_INVALID } CommandResult;
bool commands_configure (const char *admin_token);
void commands_reset_player (PlayerData *player);
bool commands_is_admin (const PlayerData *player);
uint8_t commands_gamemode (const PlayerData *player);
uint8_t commands_mode_for_fd (int client_fd);
uint8_t commands_abilities (const PlayerData *player);
CommandResult commands_execute (PlayerData *player, const char *input, size_t length);
void commands_chat (PlayerData *player, const char *input, size_t length);
int cs_chatCommand (int client_fd, int length, bool signed_packet);
int sc_commands (int client_fd);
int sc_changeGameMode (PlayerData *player, uint8_t mode);
int cs_creativeSlot (int client_fd, int length);
#endif
