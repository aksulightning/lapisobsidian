#include "optional_features.h"
#if LAPIS_WORLD_EDIT == 1
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "packets.h"
#include "plates.h"
#include "procedures.h"
#include "registries.h"
#include "world_border.h"
#include "worldgen.h"

#define WORLD_EDIT_LIMIT 4096u
/* Session state, deliberately outside the persisted PlayerData layout. */
static struct {
  int pos[2][3];
  unsigned plate;
  uint8_t selected;
} selections[MAX_PLAYERS];

static int slot (const PlayerData *player) {
  for (int i = 0; i < MAX_PLAYERS; i++) if (player == &player_data[i]) return i;
  return -1;
}
void world_edit_reset_player (PlayerData *player) {
  int i = slot(player);
  if (i >= 0) memset(&selections[i],0,sizeof(selections[i]));
}
static CommandResult reply (PlayerData *player, CommandResult result, const char *text) {
  sc_systemChat(player->client_fd,(char *)text,(uint16_t)strlen(text));
  return result;
}
static bool coordinate (const char *text, int low, int high, int *out) {
  char *end; errno = 0;
  long n = strtol(text,&end,10);
  if (errno || end == text || *end || n < low || n > high) return false;
  *out = (int)n; return true;
}
/* Only plain building blocks: no invalid palette entries or uninitialized
 * doors, containers, signs, crops or circuits can be placed by this command. */
static bool material (const char *name, uint8_t *out) {
  static const struct { const char *name; uint8_t block; } blocks[] = {
    {"air",B_air}, {"stone",B_stone}, {"dirt",B_dirt},
    {"grass_block",B_grass_block}, {"cobblestone",B_cobblestone},
    {"oak_planks",B_oak_planks}, {"oak_log",B_oak_log},
    {"oak_leaves",B_oak_leaves}, {"glass",B_glass}, {"sand",B_sand},
    {"gravel",B_gravel}, {"bricks",B_bricks}, {"obsidian",B_obsidian}
  };
  if (!strncmp(name,"minecraft:",10)) name += 10;
  for (size_t i = 0; i < sizeof(blocks)/sizeof(blocks[0]); i++) {
    if (!strcmp(name,blocks[i].name)) { *out = blocks[i].block; return true; }
  }
  return false;
}
CommandResult world_edit_command (PlayerData *player, int argc, char *const argv[]) {
  int i = slot(player);
  if (i < 0 || !plates_player_active(player)) return COMMAND_INVALID;
  if (!commands_is_admin(player)) return reply(player,COMMAND_DENIED,"Administrator permission required.");
  if (!player->health || (player->flags & 0x22)) return reply(player,COMMAND_DENIED,"Wait until loaded and alive.");
  if (argc == 1 || (argc == 2 && !strcmp(argv[1],"help"))) {
    reply(player,COMMAND_OK,"World edit: /we pos1|pos2 [x y z], /we set <block>, /we replace <from> <to>, /we clear. Limit: 4096 blocks; no undo.");
    return reply(player,COMMAND_OK,"Blocks: air, stone, dirt, grass_block, cobblestone, oak_planks, oak_log, oak_leaves, glass, sand, gravel, bricks, obsidian.");
  }
  if (selections[i].plate != plate_current) world_edit_reset_player(player);
  selections[i].plate = plate_current;
  if (argc == 2 && !strcmp(argv[1],"clear")) {
    world_edit_reset_player(player);
    return reply(player,COMMAND_OK,"Selection cleared.");
  }
  if (!strcmp(argv[1],"pos1") || !strcmp(argv[1],"pos2")) {
    int pos[3] = {player->x,player->y,player->z};
    if (argc != 2 && (argc != 5 ||
        !coordinate(argv[2],-WORLD_BORDER_LIMIT,WORLD_BORDER_LIMIT,&pos[0]) ||
        !coordinate(argv[3],0,255,&pos[1]) ||
        !coordinate(argv[4],-WORLD_BORDER_LIMIT,WORLD_BORDER_LIMIT,&pos[2])))
      return reply(player,COMMAND_USAGE,"Usage: /we pos1|pos2 [x y z]; X/Z within +/-4068, Y 0..255.");
    if (world_border_outside(pos[0],pos[2]) || pos[1] < 0 || pos[1] > 255)
      return reply(player,COMMAND_USAGE,"Selection must be inside the world border.");
    unsigned corner = !strcmp(argv[1],"pos2") ? 1u : 0u;
    memcpy(selections[i].pos[corner],pos,sizeof(pos));
    selections[i].selected |= (uint8_t)(1u << corner);
    char text[96]; snprintf(text,sizeof(text),"Position %u: %d %d %d.",corner+1,pos[0],pos[1],pos[2]);
    return reply(player,COMMAND_OK,text);
  }
  bool replace = !strcmp(argv[1],"replace");
  if ((!replace && strcmp(argv[1],"set")) || argc != (replace ? 4 : 3))
    return reply(player,COMMAND_USAGE,"Usage: /we set <block> or /we replace <from> <to>. See /we help.");
  uint8_t from = 0, to;
  if (!material(argv[replace ? 3 : 2],&to) || (replace && !material(argv[2],&from)))
    return reply(player,COMMAND_USAGE,"Unsupported block name. See /we help.");
  if (selections[i].selected != 3) return reply(player,COMMAND_USAGE,"Select both corners with /we pos1 and /we pos2.");
  int low[3], high[3]; unsigned volume = 1;
  for (unsigned axis = 0; axis < 3; axis++) {
    int a = selections[i].pos[0][axis], b = selections[i].pos[1][axis];
    low[axis] = a < b ? a : b; high[axis] = a > b ? a : b;
    unsigned width = (unsigned)(high[axis]-low[axis]+1);
    if (width > WORLD_EDIT_LIMIT/volume) return reply(player,COMMAND_DENIED,"Selection exceeds 4096 blocks.");
    volume *= width;
  }
  unsigned changed = 0; bool full = false;
  for (int x = low[0]; x <= high[0] && !full; x++)
    for (int y = low[1]; y <= high[1] && !full; y++)
      for (int z = low[2]; z <= high[2]; z++) {
        uint8_t before = getBlockAt(x,y,z);
        if (before == to || (replace && before != from)) continue;
        if (makeBlockChange((short)x,(uint8_t)y,(short)z,to)) { full = true; break; }
        changed++;
      }
  char text[128];
  snprintf(text,sizeof(text),"Changed %u blocks.%s",changed,full ? " Edit storage full; remaining blocks unchanged." : "");
  return reply(player,full ? COMMAND_DENIED : COMMAND_OK,text);
}
#endif
