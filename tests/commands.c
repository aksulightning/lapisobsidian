#include "plates.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "commands.h"
#include "mobs.h"
#include "packets.h"
#include "procedures.h"

static char response[512];
static unsigned teleports, changes;
int sc_systemChat (int fd, char *text, uint16_t n) { (void)fd; assert(n < sizeof(response)); memcpy(response,text,n); response[n] = 0; return 0; }
int getPlayerData (int fd, PlayerData **out) { for (int i = 0; i < MAX_PLAYERS; i ++) if (player_data[i].client_fd == fd) { *out = &player_data[i]; return 0; } return 1; }
uint8_t getHeightAt (int x, int z) { assert(x == 8 && z == 8); return 70; }
int sc_setCenterChunk (int fd, int x, int z) { (void)fd;(void)x;(void)z;return 0; }
int sc_chunkDataAndUpdateLight (int fd, int x, int z) { (void)fd;(void)x;(void)z;return 0; }
int sc_synchronizePlayerPosition (int fd, double x, double y, double z, float yaw, float pitch) { (void)fd;(void)x;(void)y;(void)z;(void)yaw;(void)pitch;teleports++;return 0; }
int sc_teleportEntity (int fd, int id, double x, double y, double z, float yaw, float pitch) { (void)fd;(void)id;(void)x;(void)y;(void)z;(void)yaw;(void)pitch;return 0; }
int sc_updateTime (int fd, uint64_t t) { (void)fd;assert(t == world_time);return 0; }
int sc_changeGameMode (PlayerData *player, uint8_t mode) { assert(mode == commands_gamemode(player));changes++;return 0; }
void musicbox_reset_player (PlayerData *p) { (void)p; }
CommandResult musicbox_command (PlayerData *p, int argc, char *const argv[]) { (void)p; (void)argc; (void)argv; return COMMAND_USAGE; }
static unsigned mob_spawns;
const MobType *mobs_by_name (const char *name) {
  static const MobType skeleton = {"skeleton",MOB_SKELETON,20};
  return !strcmp(name,"skeleton") ? &skeleton : NULL;
}
bool mobs_spawn (uint8_t type, int x, int y, int z) {
  assert(type == MOB_SKELETON);
  if (x < -32768 || x > 32767 || y < 1 || y > 253 || z < -32768 || z > 32767) return false;
  mob_spawns++; return true;
}
static CommandResult run (const char *input) { server_ticks += 10; return commands_execute(&player_data[0],input,strlen(input)); }
int main (void) {
  for (int i = 0; i < MAX_PLAYERS; i ++) player_data[i].client_fd = -1;
  PlayerData *p = &player_data[0], *q = &player_data[1]; p->client_fd = 10; q->client_fd = 11;
  strcpy(p->name,"Test"); strcpy(q->name,"Target"); p->health = q->health = 20;
  q->x = -10; q->y = 90; q->z = 20;
  assert(commands_configure(NULL)); commands_reset_player(p); commands_reset_player(q);
  assert(run("help") == COMMAND_OK && strstr(response,"/gamemode"));
  assert(run("  /seed  ") == COMMAND_UNKNOWN); /* slash only at command start */
  world_seed = UINT64_C(0x8000000000000000);
  assert(run("/seed") == COMMAND_OK && strstr(response,"-9223372036854775808"));
  assert(run("worldinfo") == COMMAND_OK && strstr(response,"772"));
  assert(run("nonesuch") == COMMAND_UNKNOWN);
  assert(run("help extra") == COMMAND_USAGE);
  assert(run("help a b c d e f") == COMMAND_USAGE);
  assert(run("time query") == COMMAND_OK);
  assert(run("time set day") == COMMAND_DENIED);
  assert(run("tp Target") == COMMAND_DENIED);
  assert(run("gamemode creative") == COMMAND_DENIED);
  assert(run("spawnmob skeleton") == COMMAND_DENIED && !mob_spawns);
  assert(run("admin abc") == COMMAND_DENIED);
  assert(run("spawn") == COMMAND_OK && teleports == 1 && p->x == 8 && p->y == 71 && p->grounded_y == 71);
  assert(commands_execute(p,"spawn",5) == COMMAND_DENIED && teleports == 1);
  assert(!commands_configure("short"));
  const char *token = "0123456789abcdef0123456789abcdef";
  assert(commands_configure(token)); commands_reset_player(p); commands_reset_player(q);
  assert(run("admin wrong") == COMMAND_DENIED);
  assert(run("admin 0123456789abcdef0123456789abcdef") == COMMAND_OK && commands_is_admin(p));
  assert(!commands_is_admin(q));
  assert(run("spawnmob skeleton") == COMMAND_OK && mob_spawns == 1);
  assert(run("spawnmob skeleton -32768 253 32767") == COMMAND_OK && mob_spawns == 2);
  assert(run("spawnmob") == COMMAND_USAGE);
  assert(run("spawnmob dragon") == COMMAND_USAGE);
  assert(run("spawnmob skeleton 1 2") == COMMAND_USAGE);
  assert(run("spawnmob skeleton nan 2 0") == COMMAND_USAGE);
  assert(run("spawnmob skeleton 0 254 0") == COMMAND_USAGE);
  assert(run("spawnmob skeleton 32768 2 0") == COMMAND_USAGE);
  assert(run("spawnmob skeleton 0 -1 0") == COMMAND_USAGE);
  assert(run("spawnmob skeleton 0 1 0 extra") == COMMAND_USAGE && mob_spawns == 2);
  p->health = 0; assert(run("spawnmob skeleton") == COMMAND_DENIED); p->health = 20;

  assert(run("time set night") == COMMAND_OK && world_time == 13000);
  assert(run("time set 23999") == COMMAND_OK && world_time == 23999);
  assert(run("time set 24000") == COMMAND_USAGE && world_time == 23999);
  assert(run("time set -1") == COMMAND_USAGE);
  assert(run("time set 99999999999999999999999") == COMMAND_USAGE);
  assert(run("tp -32768 255 32767") == COMMAND_OK && p->x == 8 && p->y == 71 && p->z == 8);
  assert(run("tp -32769 64 0") == COMMAND_USAGE);
  assert(run("tp 0 256 0") == COMMAND_USAGE);
  assert(run("tp 0 nan 0") == COMMAND_USAGE);
  assert(run("tp 0 1.5 0") == COMMAND_USAGE);
  assert(run("tp 1 2") == COMMAND_USAGE);
  assert(run("tp Tar") == COMMAND_USAGE);
  assert(run("tp Target") == COMMAND_OK && p->x == q->x && p->y == q->y);
  assert(run("gamemode creative") == COMMAND_OK && commands_abilities(p) == 13);
  assert(run("gamemode spectator Target") == COMMAND_OK && commands_gamemode(q) == 3 && commands_abilities(q) == 7);
  assert(run("gamemode adventure") == COMMAND_OK && commands_gamemode(p) == 2);
  assert(run("gamemode 0") == COMMAND_OK && commands_gamemode(p) == 0);
  assert(run("gamemode 4") == COMMAND_USAGE);
  assert(run("gamemode") == COMMAND_USAGE);
  assert(changes == 4);
  unsigned before = teleports; uint16_t time = world_time;
  char garbage[257]; memset(garbage,'a',sizeof(garbage));
  assert(commands_execute(p,garbage,sizeof(garbage)) == COMMAND_INVALID);
  assert(commands_execute(p,NULL,5) == COMMAND_INVALID);
  assert(run("") == COMMAND_INVALID && run("  ") == COMMAND_INVALID);
  for (unsigned ch = 0; ch < 256; ch ++) {
    if (ch >= 32 && ch <= 126) continue;
    char input[] = {'t','p',' ',(char)ch};
    assert(commands_execute(p,input,sizeof(input)) == COMMAND_INVALID);
  }
  assert(teleports == before && world_time == time);
  commands_reset_player(p); assert(!commands_is_admin(p) && commands_gamemode(p) == GAMEMODE);
  assert(run("admin wrong") == COMMAND_DENIED); assert(run("admin wrong") == COMMAND_DENIED); assert(run("admin wrong") == COMMAND_DENIED);
  assert(run("admin 0123456789abcdef0123456789abcdef") == COMMAND_DENIED);
  commands_chat(p,"!msg Target hello",17); assert(strstr(response,"You whisper to Target: hello"));
  commands_chat(p,"!msg",4); assert(strstr(response,"Usage"));
  puts("commands: parsing, bounds, permissions, session reset, modes, teleports, time and legacy whispers passed");
  return 0;
}

CommandResult plates_command(PlayerData *p,int argc,char *const argv[]) { (void)p;(void)argc;(void)argv; return COMMAND_DENIED; }
const char *plates_name(unsigned id) { (void)id;return "hub"; }
const char *plates_type_name(PlateType type) { (void)type;return "hub"; }
