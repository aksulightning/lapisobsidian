#ifndef H_SERVER_CONFIG
#define H_SERVER_CONFIG
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "globals.h"
#define CONFIG_MOTD_MAX 120
#define SERVER_CONFIG_DEFAULTS { .port=PORT, .gamemode=GAMEMODE, .seed=INITIAL_WORLD_SEED, .wheat_growth_seconds=30, .motd="Lapis Obsidian" }
typedef struct {
  uint64_t seed;
  uint16_t port, wheat_growth_seconds;
  uint8_t gamemode;
  bool seed_set, mirror_horizontal, experimental_enable_plates;
  char motd[CONFIG_MOTD_MAX+1];
} ServerConfig;
extern ServerConfig server_config;
/* Load atomically; create defaults only if the file does not exist. */
bool server_config_load (const char *path, ServerConfig *out, char *error, size_t capacity);
bool server_config_arguments (ServerConfig *config, int argc, char **argv, char *error, size_t capacity);
#endif
