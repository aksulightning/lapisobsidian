#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include "optional_features.h"
#include "commands.h"
#include "items.h"
#include "plates.h"
#include "procedures.h"
#include "registries.h"
#include "serialize.h"
#include "worldgen.h"

static int pair[2];
static PlayerData *p = &player_data[0];
static void drain (void) { char b[8192]; while (recv(pair[0],b,sizeof(b),MSG_DONTWAIT) > 0) {} }
static CommandResult command (const char *text) {
  CommandResult result = commands_execute(p,text,strlen(text)); drain(); return result;
}
static void block (int x, int y, int z, uint8_t value) {
  assert(!makeBlockChange((short)x,(uint8_t)y,(short)z,value)); drain();
}
static unsigned drops (void) {
  unsigned n = 0;
  for (size_t i = 0; i < ITEM_ENTITY_LIMIT; i++) if (items_at(i)->item == I_oak_log) n += items_at(i)->count;
  return n;
}
static void tree (void) {
  items_clear(); drain();
  p->x = 0; p->y = 201; p->z = 0; p->flags = 0; p->health = 20;
  p->inventory_items[0] = I_netherite_axe; p->inventory_count[0] = 1;
  rng_seed = 123456789;
  block(0,200,0,B_dirt);
  for (int y = 201; y <= 203; y++) block(0,y,0,B_oak_log);
  block(0,204,0,B_oak_leaves);
}
static void mine (void) { handlePlayerAction(p,2,0,201,0); drain(); }
int main (void) {
  char dir[64]; snprintf(dir,sizeof(dir),".tests/optional-%d-%d",LAPIS_WORLD_EDIT,LAPIS_TREE_CHOPPER);
  if (mkdir(dir,0700)) assert(!access(dir,F_OK));
  assert(!chdir(dir)); remove("world.bin");
  for (int i = 0; i < MAX_PLAYERS; i++) player_data[i].client_fd = -1;
  memset(block_changes,255,sizeof(block_changes)); block_changes_count = 0;
  assert(!initSerializer()); assert(!socketpair(AF_UNIX,SOCK_STREAM,0,pair));
  p->client_fd = pair[1]; p->health = 20;
  assert(commands_configure("0123456789abcdef0123456789abcdef")); commands_reset_player(p);
#if LAPIS_WORLD_EDIT == 1
  assert(command("we pos1") == COMMAND_DENIED);
#else
  assert(command("we help") == COMMAND_UNKNOWN);
#endif
  assert(command("admin 0123456789abcdef0123456789abcdef") == COMMAND_OK);
#if LAPIS_WORLD_EDIT == 1
  assert(command("we set stone") == COMMAND_USAGE);
  assert(command("we pos1 2 202 2") == COMMAND_OK);
  assert(command("we pos2 1 201 1") == COMMAND_OK);
  assert(command("we set minecraft:stone") == COMMAND_OK);
  assert(getBlockAt(1,201,1) == B_stone && getBlockAt(2,202,2) == B_stone);
  assert(getBlockAt(3,202,2) == B_air);
  block(1,201,1,B_glass);
  assert(command("we replace stone dirt") == COMMAND_OK);
  assert(getBlockAt(1,201,1) == B_glass && getBlockAt(2,202,2) == B_dirt);
  assert(command("we set chest") == COMMAND_USAGE);
  assert(command("we set 255") == COMMAND_USAGE);
  assert(command("we set") == COMMAND_USAGE);
  assert(command("we replace stone") == COMMAND_USAGE);
  assert(command("we pos1 0 256 0") == COMMAND_USAGE);
  assert(command("we pos1 4069 0 0") == COMMAND_USAGE);
  assert(command("we pos1 0 -1 0") == COMMAND_USAGE);
  assert(command("we pos1 999999999999999999999999999999999 0 0") == COMMAND_USAGE);
  assert(command("we pos1 0 201 0") == COMMAND_OK);
  assert(command("we pos2 4067 255 4067") == COMMAND_OK);
  int edits = block_changes_count;
  assert(command("we set stone") == COMMAND_DENIED && block_changes_count == edits);
  assert(command("we pos2 0 201 0") == COMMAND_OK);
  /* A plate change cannot reuse the previous world's selection. */
  plate_current = 1;
  assert(command("we set stone") == COMMAND_USAGE); plate_current = 0;
  assert(command("we pos1") == COMMAND_OK);
  assert(command("we pos2") == COMMAND_OK);
  commands_reset_player(p);
  assert(command("admin 0123456789abcdef0123456789abcdef") == COMMAND_OK);
  assert(command("we set stone") == COMMAND_USAGE);
  assert(command("we pos1 0 201 0") == COMMAND_OK);
  assert(command("we pos2 0 201 0") == COMMAND_OK);
  assert(command("we clear") == COMMAND_OK);
  assert(command("we set stone") == COMMAND_USAGE);
#endif
  tree(); handlePlayerAction(p,0,0,201,0); drain();
  assert(getBlockAt(0,201,0) == B_oak_log); /* Must finish mining first. */
  mine();
#if LAPIS_TREE_CHOPPER == 1
  assert(getBlockAt(0,201,0) == B_air && getBlockAt(0,203,0) == B_air && drops() == 3);
#else
  assert(getBlockAt(0,201,0) == B_air && getBlockAt(0,203,0) == B_oak_log && drops() == 1);
#endif
  assert(getBlockAt(0,204,0) == B_oak_leaves);
  tree(); p->flags = 4; mine();
  assert(getBlockAt(0,202,0) == B_oak_log && drops() == 1);
  tree(); p->inventory_items[0] = I_stone_pickaxe; mine();
  assert(getBlockAt(0,202,0) == B_oak_log && drops() == 1);
  tree(); block(0,204,0,B_air); mine();
  assert(getBlockAt(0,202,0) == B_oak_log && drops() == 1);
  tree(); assert(command("gamemode adventure") == COMMAND_OK); mine();
  assert(getBlockAt(0,201,0) == B_oak_log && drops() == 0);
  assert(command("gamemode creative") == COMMAND_OK);
  handlePlayerAction(p,0,0,201,0); drain();
  assert(getBlockAt(0,201,0) == B_air && getBlockAt(0,202,0) == B_oak_log && drops() == 0);
  assert(command("gamemode survival") == COMMAND_OK);
#if LAPIS_TREE_CHOPPER == 1
  tree(); rng_seed = 0; mine(); /* Deterministically break the axe on the first log. */
  assert(!p->inventory_count[0] && drops() == 1 && getBlockAt(0,202,0) == B_oak_log);
  tree();
  for (unsigned i = 0; i < ITEM_ENTITY_LIMIT; i++) {
    assert(items_spawn(I_stone,64,100+(int)i*3,200,100,500)); drain();
  }
  mine(); assert(getBlockAt(0,201,0) == B_oak_log && p->inventory_count[0] == 1 && drops() == 0);
  tree();
  /* More than 64 connected logs falls back to normal single-block mining. */
  for (int x = 1; x <= 64; x++) block(x,201,0,B_oak_log);
  mine(); assert(getBlockAt(0,202,0) == B_oak_log && getBlockAt(64,201,0) == B_oak_log && drops() == 1);
#endif
#if LAPIS_WORLD_EDIT == 1
  /* Exhaust edit storage without allocating 20,000 disk writes. */
  memset(block_changes,0,sizeof(block_changes));
  for (int i = 0; i < MAX_BLOCK_CHANGES; i++) {
    block_changes[i].x = (short)(10000+i); block_changes[i].y = 200; block_changes[i].block = B_stone;
  }
  block_changes_count = MAX_BLOCK_CHANGES;
  assert(command("we pos1 0 230 0") == COMMAND_OK);
  assert(command("we pos2 0 230 0") == COMMAND_OK);
  assert(command("we set stone") == COMMAND_DENIED);
  assert(getBlockAt(0,230,0) == B_air);
#endif
  close(pair[0]); close(pair[1]);
  printf("optional features: world edit=%d, tree chopper=%d passed\n",LAPIS_WORLD_EDIT,LAPIS_TREE_CHOPPER);
  return 0;
}
