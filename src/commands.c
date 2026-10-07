#include "plates.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include "commands.h"
#include "server_stats.h"
#include "server_config.h"
#include "world_border.h"
#include "mobs.h"
#include "musicbox.h"
#include "packets.h"
#include "procedures.h"
#include "worldgen.h"
#include "beta173_worldgen.h"
#include "protocol.h"
#include "tools.h"

_Static_assert(GAMEMODE >= 0 && GAMEMODE <= 3,"GAMEMODE must be 0..3");

/* Runtime state only: keep the inherited on-disk PlayerData layout unchanged. */
static struct { uint8_t admin, attempts, mode, initialized, teleported; uint32_t teleport_tick; } sessions[MAX_PLAYERS];
static char admin_token[COMMAND_TOKEN_MAX];
static size_t admin_length;

static int player_index (const PlayerData *player) {
  for (int i = 0; i < MAX_PLAYERS; i ++) if (player == &player_data[i]) return i;
  return -1;
}
bool commands_configure (const char *token) {
  memset(admin_token,0,sizeof(admin_token)); admin_length = 0;
  memset(sessions,0,sizeof(sessions));
  if (!token) return true;
  size_t n = 0;
  while (n <= COMMAND_TOKEN_MAX && token[n]) {
    if ((unsigned char)token[n] < 33 || (unsigned char)token[n] > 126) return false;
    n ++;
  }
  if (n < 32 || n > COMMAND_TOKEN_MAX) return false;
  memcpy(admin_token,token,n); admin_length = n; return true;
}
void commands_reset_player (PlayerData *player) {
  int i = player_index(player); if (i < 0) return;
  memset(&sessions[i],0,sizeof(sessions[i]));
  world_border_reset(player); musicbox_reset_player(player);
  sessions[i].mode = server_config.gamemode; sessions[i].initialized = 1;
}
bool commands_is_admin (const PlayerData *player) {
  int i = player_index(player); return i >= 0 && sessions[i].admin;
}
uint8_t commands_gamemode (const PlayerData *player) {
  int i = player_index(player);
  return i >= 0 && sessions[i].initialized ? sessions[i].mode : server_config.gamemode;
}
uint8_t commands_mode_for_fd (int fd) {
  PlayerData *player; return getPlayerData(fd,&player) ? server_config.gamemode : commands_gamemode(player);
}
uint8_t commands_abilities (const PlayerData *player) {
  uint8_t mode = commands_gamemode(player);
  if (mode == 1) return 0x0d; /* invulnerable, may fly, instant build */
  if (mode == 3) return 0x07; /* invulnerable, flying, may fly */
  #ifdef ENABLE_PLAYER_FLIGHT
  return 0x04;
  #else
  return 0;
  #endif
}
static CommandResult reply (PlayerData *player, CommandResult result, const char *text) {
  sc_systemChat(player->client_fd,(char *)text,(uint16_t)strlen(text)); return result;
}
static PlayerData *named_player (const char *name) {
  size_t n = strlen(name); if (!n || n > 16) return NULL;
  for (int i = 0; i < MAX_PLAYERS; i ++) {
    PlayerData *player = &player_data[i];
    if (!plates_player_active(player)) continue;
    if (player->client_fd < 0 || (player->flags & 0x20)) continue;
    size_t len = 0; while (len < sizeof(player->name) && player->name[len]) len ++;
    if (len == n && memcmp(player->name,name,n) == 0) return player;
  }
  return NULL;
}
static bool number (const char *s, int low, int high, int *result) {
  bool negative = *s == '-'; if (negative || *s == '+') s ++;
  if (!*s) return false;
  unsigned value = 0;
  for (; *s; s ++) {
    if (*s < '0' || *s > '9' || value > 3276) return false;
    value = value*10+(unsigned)(*s-'0');
    if (value > 32768) return false;
  }
  int n = negative ? -(int)value : (int)value;
  if (n < low || n > high) return false;
  *result = n; return true;
}
static bool teleport_ready (int slot) {
  if (sessions[slot].teleported && (uint32_t)(server_ticks-sessions[slot].teleport_tick) < (uint32_t)(2*TICKS_PER_SECOND)) return false;
  sessions[slot].teleported = 1; sessions[slot].teleport_tick = server_ticks; return true;
}
CommandResult commands_execute (PlayerData *player, const char *input, size_t length) {
  int slot = player_index(player);
  if (slot < 0 || player->client_fd < 0) return COMMAND_INVALID;
  if (!input || !length || length > COMMAND_MAX_BYTES) return reply(player,COMMAND_INVALID,"Invalid command length.");
  char buffer[COMMAND_MAX_BYTES+1]; char *argv[COMMAND_MAX_ARGS]; size_t argc = 0;
  for (size_t i = 0; i < length; i ++) {
    unsigned char ch = (unsigned char)input[i];
    if (ch < 32 || ch > 126) return reply(player,COMMAND_INVALID,"Commands require printable ASCII text.");
    buffer[i] = (char)ch;
  }
  buffer[length] = 0; char *at = buffer;
  if (*at == '/') at ++;
  while (*at) {
    while (*at == ' ') at ++;
    if (!*at) break;
    if (argc == COMMAND_MAX_ARGS) return reply(player,COMMAND_USAGE,"Too many command arguments.");
    argv[argc++] = at;
    while (*at && *at != ' ') at ++;
    if (*at) *at++ = 0;
  }
  if (!argc) return reply(player,COMMAND_INVALID,"Empty command.");
  char output[256];
  if (!strcmp(argv[0],"plate")) return plates_command(player,(int)argc,argv);
  if (!strcmp(argv[0],"music")) return musicbox_command(player,(int)argc,argv);
  if (!strcmp(argv[0],"help")) {
    if (argc != 1) return reply(player,COMMAND_USAGE,"Usage: /help");
    return reply(player,COMMAND_OK,"Commands: /plate list|go <name>, /help, /tps, /seed, /worldinfo, /spawn, /music, /time query, /admin <token>. Admin: /tp <player|x y z>, /time set <day|night|0..23999>, /gamemode <mode> [player], /spawnmob <type> [x y z].");
  }
  if (!strcmp(argv[0],"tps")) {
    if (argc != 1) return reply(player,COMMAND_USAGE,"Usage: /tps");
    server_stats_format(output,sizeof(output));
    return reply(player,COMMAND_OK,output);
  }
  if (!strcmp(argv[0],"admin")) {
    if (argc != 2) return reply(player,COMMAND_USAGE,"Usage: /admin <token>");
    if (!admin_length || sessions[slot].attempts >= 3) return reply(player,COMMAND_DENIED,"Administrator login unavailable.");
    sessions[slot].attempts ++;
    size_t n = strlen(argv[1]); unsigned difference = (unsigned)(n ^ admin_length);
    for (size_t i = 0; i < COMMAND_TOKEN_MAX; i ++) difference |= (unsigned char)admin_token[i] ^ (i < n ? (unsigned char)argv[1][i] : 0u);
    if (difference) return reply(player,COMMAND_DENIED,"Administrator login failed.");
    sessions[slot].admin = 1;
    return reply(player,COMMAND_OK,"Administrator access enabled for this connection.");
  }
  if (!strcmp(argv[0],"seed")) {
    if (argc != 1) return reply(player,COMMAND_USAGE,"Usage: /seed");
    if (world_seed >> 63) snprintf(output,sizeof(output),"Seed: -%" PRIu64,UINT64_C(0)-world_seed);
    else snprintf(output,sizeof(output),"Seed: %" PRIu64,world_seed);
    return reply(player,COMMAND_OK,output);
  }
  if (!strcmp(argv[0],"worldinfo")) {
    if (argc != 1) return reply(player,COMMAND_USAGE,"Usage: /worldinfo");
    if (plates_enabled) {
      snprintf(output,sizeof(output),"Plate: %.24s | type: %s | time: %u | edits: %d/%d",plates_name(plate_current),plates_type_name(plates_type()),world_time,block_changes_count,MAX_BLOCK_CHANGES);
      return reply(player,COMMAND_OK,output);
    }
    snprintf(output,sizeof(output),"Lapis Obsidian | generator %u | protocol %u | mirror X: %s | terrain Y: 0..127 | border: +/-4068 | Far Lands: +/-3940 | time: %u",BETA173_GENERATOR_VERSION,LAPIS_PROTOCOL_VERSION,world_mirror_horizontal ? "on" : "off",world_time);
    return reply(player,COMMAND_OK,output);
  }
  if (!strcmp(argv[0],"spawn")) {
    if (argc != 1) return reply(player,COMMAND_USAGE,"Usage: /spawn");
    if (!player->health) return reply(player,COMMAND_DENIED,"Respawn before teleporting.");
    if (!teleport_ready(slot)) return reply(player,COMMAND_DENIED,"Wait two seconds between teleports.");
    world_teleport(player,8,(int)getHeightAt(8,8)+1,8);
    return reply(player,COMMAND_OK,"Teleported to spawn.");
  }
  if (!strcmp(argv[0],"time")) {
    if (argc == 1 || (argc == 2 && !strcmp(argv[1],"query"))) {
      snprintf(output,sizeof(output),"Time: %u",world_time); return reply(player,COMMAND_OK,output);
    }
    if (!commands_is_admin(player)) return reply(player,COMMAND_DENIED,"Administrator permission required.");
    int time;
    if (argc != 3 || strcmp(argv[1],"set")) return reply(player,COMMAND_USAGE,"Usage: /time set <day|night|0..23999>");
    if (!strcmp(argv[2],"day")) time = 1000;
    else if (!strcmp(argv[2],"night")) time = 13000;
    else if (!number(argv[2],0,23999,&time)) return reply(player,COMMAND_USAGE,"Invalid time (expected 0..23999).");
    world_time = (uint16_t)time;
    for (int i = 0; i < MAX_PLAYERS; i ++) if (player_data[i].client_fd >= 0 && !(player_data[i].flags & 0x20)) sc_updateTime(player_data[i].client_fd,world_time);
    return reply(player,COMMAND_OK,"Time updated.");
  }
  if (!strcmp(argv[0],"tp")) {
    if (!commands_is_admin(player)) return reply(player,COMMAND_DENIED,"Administrator permission required.");
    int x, y, z;
    if (argc == 2) {
      PlayerData *target = named_player(argv[1]);
      if (!target) return reply(player,COMMAND_USAGE,"Player not found.");
      x = target->x; y = target->y; z = target->z;
    } else if (argc == 4 && number(argv[1],-32768,32767,&x) && number(argv[2],0,255,&y) && number(argv[3],-32768,32767,&z)) { /* validated */ }
    else return reply(player,COMMAND_USAGE,"Usage: /tp <player> or /tp <x y z> (integer coordinates).");
    if (!player->health) return reply(player,COMMAND_DENIED,"Respawn before teleporting.");
    if (!teleport_ready(slot)) return reply(player,COMMAND_DENIED,"Wait two seconds between teleports.");
    world_teleport(player,x,y,z); return reply(player,COMMAND_OK,"Teleported.");
  }
  if (!strcmp(argv[0],"spawnmob")) {
    if (!commands_is_admin(player)) return reply(player,COMMAND_DENIED,"Administrator permission required.");
    if (argc != 2 && argc != 5) return reply(player,COMMAND_USAGE,"Usage: /spawnmob <chicken|cow|pig|sheep|zombie|skeleton|spider|creeper|zombie_pigman|ghast> [x y z]");
    const MobType *type = mobs_by_name(argv[1]);
    if (!type) return reply(player,COMMAND_USAGE,"Unknown mob. Use chicken, cow, pig, sheep, zombie, skeleton, spider, creeper, zombie_pigman or ghast.");
    if (!player->health || (player->flags&0x22)) return reply(player,COMMAND_DENIED,"Spawn mobs after loading or respawning.");
    int x = player->x, y = player->y, z = (int)player->z+2;
    if (argc == 5 && (!number(argv[2],-32768,32767,&x) || !number(argv[3],1,253,&y) || !number(argv[4],-32768,32767,&z)))
      return reply(player,COMMAND_USAGE,"Coordinates must be integers: X/Z -32768..32767, Y 1..253.");
    if (!mobs_spawn(type->type,x,y,z)) return reply(player,COMMAND_DENIED,"Cannot spawn: need safe ground, clear space, no overlap and a free mob slot.");
    snprintf(output,sizeof(output),"Spawned %s at %d %d %d.",type->name,x,y,z);
    return reply(player,COMMAND_OK,output);
  }
  if (!strcmp(argv[0],"gamemode")) {
    if (!commands_is_admin(player)) return reply(player,COMMAND_DENIED,"Administrator permission required.");
    if (argc < 2 || argc > 3) return reply(player,COMMAND_USAGE,"Usage: /gamemode <survival|creative|adventure|spectator|0..3> [player]");
    static const char *const names[] = {"survival","creative","adventure","spectator"};
    int mode = -1;
    for (int i = 0; i < 4; i ++) if (!strcmp(argv[1],names[i])) mode = i;
    if (mode < 0 && !number(argv[1],0,3,&mode)) return reply(player,COMMAND_USAGE,"Unknown game mode.");
    PlayerData *target = argc == 3 ? named_player(argv[2]) : player;
    if (!target) return reply(player,COMMAND_USAGE,"Player not found.");
    int index = player_index(target); sessions[index].mode = (uint8_t)mode; sessions[index].initialized = 1;
    target->grounded_y = target->y;
    target->flags &= (uint8_t)~0x10u; /* Stop an in-progress eating action. */
    sc_changeGameMode(target,(uint8_t)mode);
    return reply(player,COMMAND_OK,"Game mode updated for this connection.");
  }
  return reply(player,COMMAND_UNKNOWN,"Unknown command. Use /help.");
}

void commands_chat (PlayerData *player, const char *input, size_t length) {
  if (!player || !input || !length || length > 224) return;
  if (length == 5 && !memcmp(input,"!help",5)) { commands_execute(player,"help",4); return; }
  char text[225], output[288]; memcpy(text,input,length); text[length] = 0;
  if (length >= 4 && !memcmp(text,"!msg",4) && (length == 4 || text[4] == ' ')) {
    char *name = text+4; while (*name == ' ') name ++;
    char *message = name; while (*message && *message != ' ') message ++;
    if (*message) *message++ = 0;
    while (*message == ' ') message ++;
    if (!*name || !*message) { reply(player,COMMAND_USAGE,"Usage: !msg <player> <message>"); return; }
    PlayerData *target = named_player(name);
    if (!target) { reply(player,COMMAND_USAGE,"Player not found."); return; }
    snprintf(output,sizeof(output),"%.*s whispers to you: %s",16,player->name,message);
    reply(target,COMMAND_OK,output);
    snprintf(output,sizeof(output),"You whisper to %.*s: %s",16,target->name,message);
    reply(player,COMMAND_OK,output); return;
  }
  snprintf(output,sizeof(output),"<%.*s> %s",16,player->name,text);
  for (int i = 0; i < MAX_PLAYERS; i ++) if (player_data[i].client_fd >= 0 && !(player_data[i].flags & 0x20)) reply(&player_data[i],COMMAND_OK,output);
}
