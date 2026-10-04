#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include "plates.h"
#include "server_config.h"
#include "commands.h"
#include "doors.h"
#include "signs.h"
#include "circuits.h"
#include "farming.h"
#include "fluids.h"
#include "items.h"
#include "mobs.h"
#include "packets.h"
#include "procedures.h"
#include "registries.h"
#include "serialize.h"
#include "worldgen.h"

static int pair[2][2];
static PlayerData *a = &player_data[0], *b = &player_data[1];
static size_t drain (int peer) { uint8_t buf[65536]; size_t n = 0; ssize_t got; while ((got = recv(peer,buf,sizeof(buf),MSG_DONTWAIT)) > 0) n += (size_t)got; return n; }
static void drain_all (void) { drain(pair[0][0]); drain(pair[1][0]); }
static void block (int x, int y, int z, uint8_t value) { assert(!makeBlockChange((short)x,(uint8_t)y,(short)z,value)); drain_all(); }
static CommandResult command (PlayerData *p, const char *s) { CommandResult result = commands_execute(p,s,strlen(s)); drain_all(); return result; }
static void select_player (unsigned id) { assert(plates_select(id)); player_plates[0] = (uint8_t)id; }
static uint32_t var (const uint8_t *p, size_t n, size_t *at) {
  uint32_t v = 0; for (unsigned shift = 0; shift < 35; shift += 7) {
    assert(*at < n); uint8_t b0 = p[(*at)++]; assert(shift < 28 || !(b0&240u));
    v |= (uint32_t)(b0&127u)<<shift; if (!(b0&128u)) return v;
  }
  assert(0); return 0;
}
static void connect_players (void) {
  for (unsigned i = 0; i < MAX_PLAYERS; i++) player_data[i].client_fd = -1;
  for (unsigned i = 0; i < 2; i++) {
    assert(!socketpair(AF_UNIX,SOCK_STREAM,0,pair[i]));
    PlayerData *p = &player_data[i]; p->client_fd = pair[i][1]; p->x = 2; p->y = 201; p->z = 0;
    p->health = 20; p->flags = 0; p->hotbar = 0; commands_reset_player(p);
  }
}
static void disconnect_players (void) {
  for (unsigned i = 0; i < 2; i++) { close(pair[i][0]); close(pair[i][1]); player_data[i].client_fd = -1; }
}
static void travel (const char *name) {
  /* Chunk output is larger than a socket buffer. Drain concurrently and decode
   * the actual frames afterwards, rather than substituting a fake packet sink. */
  drain_all(); pid_t child = fork(); assert(child >= 0);
  if (!child) {
    close(pair[0][1]); FILE *f = fopen("travel-wire.bin","wb"); if (!f) _exit(2);
    uint8_t data[65536]; ssize_t n;
    while ((n = recv(pair[0][0],data,sizeof(data),0)) > 0) if (fwrite(data,1,(size_t)n,f) != (size_t)n) _exit(3);
    _exit(fclose(f) ? 4 : 0);
  }
  assert(plates_travel(a,name)); assert(!shutdown(pair[0][1],SHUT_WR));
  int status; assert(waitpid(child,&status,0) == child && WIFEXITED(status) && !WEXITSTATUS(status));
  FILE *f = fopen("travel-wire.bin","rb"); assert(f); assert(!fseek(f,0,SEEK_END)); long size = ftell(f); assert(size > 0 && size < 8*1024*1024);
  rewind(f); uint8_t *data = malloc((size_t)size); assert(data && fread(data,1,(size_t)size,f) == (size_t)size); fclose(f);
  size_t at = 0; unsigned respawns = 0, chunks = 0;
  while (at < (size_t)size) {
    uint32_t n = var(data,(size_t)size,&at); assert(n <= (size_t)size-at); size_t end = at+n;
    uint32_t id = var(data,end,&at);
    if (id == 0x4b) {
      assert(!var(data,end,&at)); uint32_t length = var(data,end,&at);
      const char *dimension = plates_dimension(plate_current);
      assert(length == strlen(dimension) && length <= end-at && !memcmp(data+at,dimension,length)); respawns++;
    }
    if (id == 0x27) chunks++;
    at = end;
  }
  assert(respawns == 1 && chunks == 25); free(data);
  uint8_t leaving[32]; ssize_t left = recv(pair[1][0],leaving,sizeof(leaving),MSG_DONTWAIT);
  assert(left > 0); size_t offset = 0;
  uint32_t length = var(leaving,(size_t)left,&offset); assert(length == (size_t)left-offset);
  assert(var(leaving,(size_t)left,&offset) == 0x46 && var(leaving,(size_t)left,&offset) == 1);
  assert(var(leaving,(size_t)left,&offset) == (uint32_t)a->client_fd && offset == (size_t)left);
  /* B only sees A disappear; no destination chunks or entities cross plates. */
  close(pair[0][0]); close(pair[0][1]); assert(!socketpair(AF_UNIX,SOCK_STREAM,0,pair[0])); a->client_fd = pair[0][1];
}
int main (void) {
  char folder[] = ".tests/plates-world-XXXXXX"; assert(mkdtemp(folder) && !chdir(folder));
  server_config = (ServerConfig)SERVER_CONFIG_DEFAULTS;
  assert(plates_start() && !plates_enabled);
  server_config.experimental_enable_plates = true; server_config.seed = 42;
  assert(plates_start() && plates_enabled && plates_type() == PLATE_HUB && !strcmp(plates_name(0),"hub"));
  assert(getTerrainAt(8,64,8,(ChunkAnchor){0}) != B_air && getHeightAt(8,8) == 64);
  assert(commands_configure("0123456789abcdef0123456789abcdef")); connect_players();
  assert(command(a,"plate create flat 123 flatworld") == COMMAND_DENIED);
  assert(command(a,"plate remove hub") == COMMAND_DENIED);
  assert(command(a,"admin 0123456789abcdef0123456789abcdef") == COMMAND_OK);
  assert(command(a,"plate create flat 123 flatworld") == COMMAND_OK);
  assert(command(a,"plate create sky -456 skybox") == COMMAND_OK);
  assert(command(a,"plate create fire 123 volcanic") == COMMAND_OK);
  assert(command(a,"plate create beta 789 betanium") == COMMAND_OK);
  assert(command(a,"plate flat 123 flatworld") == COMMAND_USAGE);
  assert(command(a,"plate sky") == COMMAND_USAGE);
  assert(command(a,"plate confirm") == COMMAND_DENIED && plates_find("sky") >= 0);
  assert(command(a,"plate create") == COMMAND_USAGE);
  assert(command(a,"plate create incomplete 1") == COMMAND_USAGE);
  assert(command(a,"plate create bad 1 hub extra") == COMMAND_USAGE);
  assert(command(a,"plate remove") == COMMAND_USAGE);
  assert(command(a,"plate remove sky extra") == COMMAND_USAGE);
  assert(command(a,"plate nonsense") == COMMAND_USAGE);
  assert(!plates_is_loaded(1)); assert(plates_dimension_count() == 5);
  assert(command(a,"plate create ../bad 1 flatworld") == COMMAND_USAGE);
  assert(command(a,"plate create bad 9223372036854775808 hub") == COMMAND_USAGE);
  assert(command(a,"plate create bad 1 unknown") == COMMAND_USAGE);
  assert(!plates_create("flat",2,PLATE_SKYBOX) && !plates_create("confirm",1,PLATE_HUB));
  assert(!plates_remove("hub") && command(a,"plate remove hub") == COMMAND_DENIED);
  for (unsigned t = 0; t <= PLATE_VOLCANIC; t++) {
    unsigned id = t == PLATE_HUB ? 0 : t == PLATE_FLATWORLD ? 1 : t == PLATE_SKYBOX ? 2 : t == PLATE_VOLCANIC ? 3 : 4;
    assert(plates_select(id));
    assert(plates_mob_allowed(MOB_PIG) == (t == PLATE_BETANIUM || t == PLATE_SKYBOX));
    assert(plates_mob_allowed(MOB_GHAST) == (t == PLATE_VOLCANIC));
  }
  select_player(1); world_time = 500;
  block(0,200,0,B_stone); assert(signs_place(a,0,200,0,1)); drain_all();
  char lines[4][SIGN_LINE_MAX+1] = {{"flat sign"}}; assert(signs_edit(a,0,201,0,true,lines)); drain_all();
  block(1,200,0,B_stone); assert(circuits_place(a,1,200,0,1,I_lever)); drain_all();
  block(3,200,0,B_dirt); a->inventory_items[0] = I_wooden_hoe; a->inventory_count[0] = 1; assert(farming_use(a,3,200,0,1)); drain_all();
  a->inventory_items[0] = I_wheat_seeds; a->inventory_count[0] = 1; assert(farming_use(a,3,200,0,1)); drain_all();
  block(6,200,0,B_stone); block(6,201,0,B_water);
  assert(items_spawn(I_dirt,2,8,201,0,500)); drain_all();
  assert(!mobs_spawn(MOB_ZOMBIE,8,65,8));
  /* Direct block broadcasts and player damage cannot reach the other plate. */
  assert(!makeBlockChange(9,201,0,B_obsidian)); assert(drain(pair[0][0]) > 0 && !drain(pair[1][0]));
  uint8_t health = b->health; hurtEntity(b->client_fd,a->client_fd,D_generic,1); assert(b->health == health); drain_all();
  select_player(2); assert(world_time == 0 && block_changes_count == 0 && getBlockAt(9,201,0) == B_air);
  assert(!signs_at(0,201,0)); uint16_t state;
  assert(!circuits_state_at(1,201,0,B_lever,&state) && !farming_state_at(3,201,0,B_wheat,&state));
  for (unsigned i = 0; i < ITEM_ENTITY_LIMIT; i++) assert(!items_at(i)->count);
  fluids_tick(100000); assert(getBlockAt(6,201,0) == B_air);
  block(9,201,0,B_cobblestone);
  select_player(1); assert(world_time == 500 && getBlockAt(9,201,0) == B_obsidian && signs_at(0,201,0));
  assert(farming_state_at(3,201,0,B_wheat,&state));
  fluids_tick(100000); fluids_tick(100000); drain_all(); assert(getBlockAt(5,201,0) == B_water_1);
  puts("plates: terrain, blocks, signs, circuits, farms, fluids, items and damage isolation passed");

  select_player(3); a->x = 8; a->y = 65; a->z = 8; a->flags = 0;
  assert(!mobs_spawn(MOB_ZOMBIE,8,65,10));
  assert(mobs_spawn(MOB_ZOMBIE_PIGMAN,8,65,10)); drain_all();
  health = a->health; mobs_tick(1000000); drain_all(); assert(a->health == health);
  assert(mobs_spawn(MOB_GHAST,20,84,20)); drain_all();
  a->x = 20; a->y = 84; a->z = 10; int edits = block_changes_count;
  mobs_tick(1000000); drain_all(); assert(mobs_arrow_count() > 0);
  mobs_tick_arrows(1000000); drain_all(); assert(block_changes_count == edits);
  select_player(2); for (unsigned i = 0; i < MAX_MOBS; i++) assert(!mob_data[i].type); assert(!mobs_arrow_count());
  puts("plates: volcanic-only mob policy, neutral pigman and non-griefing ghast projectile passed");

  select_player(0); a->flags = 0; a->health = 20;
  travel("flat"); assert(player_plates[0] == 1 && player_plates[1] == 0);
  assert(!plates_movement_guard(a,8.5,65,8.5));
  assert(command(a,"plate remove flat") == COMMAND_OK && command(a,"plate confirm") == COMMAND_DENIED);
  assert(command(a,"plate remove sky") == COMMAND_OK && command(b,"plate confirm") == COMMAND_DENIED);
  assert(command(a,"plate confirm") == COMMAND_OK && plates_find("sky") < 0);
  assert(command(a,"plate create race 1 flatworld") == COMMAND_OK);
  assert(command(a,"plate remove race") == COMMAND_OK);
  assert(plates_remove("race") && plates_create("race",2,PLATE_SKYBOX));
  assert(command(a,"plate confirm") == COMMAND_DENIED && plates_find("race") >= 0);
  assert(command(a,"plate list") == COMMAND_OK);
  puts("plates: travel framing, occupied/protected removal, permission and confirmation isolation passed");

  /* Shared inventory persists; fresh connections always enter hub. */
  a->inventory_items[0] = I_diamond; a->inventory_count[0] = 3; a->uuid[0] = 7;
  writePlayerDataToDisk(); disconnect_players(); plates_shutdown();
  assert(plates_start() && !strcmp(plates_name(0),"hub") && player_plates[0] == 0 && a->inventory_items[0] == I_diamond && a->inventory_count[0] == 3);
  assert(plates_select((unsigned)plates_find("flat")) && getBlockAt(9,201,0) == B_obsidian);
  const Sign *sign = signs_at(0,201,0); assert(sign && !strcmp(sign->lines[0][0],"flat sign"));
  assert(circuits_state_at(1,201,0,B_lever,&state) && farming_state_at(3,201,0,B_wheat,&state));
  assert(plates_find("sky") < 0 && plates_find("race") > 0);
  assert(plates_select(0) && getBlockAt(9,201,0) == B_air && !signs_at(0,201,0));
  /* Names cannot escape the root; pre-existing directories and symlinks fail. */
  assert(!plates_create("../escape",1,PLATE_HUB));
  assert(!symlink("../outside","plates/link")); assert(!plates_create("link",1,PLATE_HUB));
  assert(plates_create("broken",1,PLATE_FLATWORLD)); int broken = plates_find("broken");
  assert(!symlink("../../../outside.bin","plates/broken/world.bin")); assert(!plates_load((unsigned)broken));
  assert(plates_create("lastone",3,PLATE_HUB) && plates_create("lasttwo",4,PLATE_FLATWORLD));
  assert(!plates_create("overflow",5,PLATE_FLATWORLD));
  plates_shutdown();
  FILE *f = fopen("plates/index.txt","wb"); assert(f); fputs("LAPIS_PLATES_1\n../escape 0000000000000001 hub\n",f); fclose(f);
  assert(!plates_start()); plates_shutdown();
  server_config.experimental_enable_plates = false; assert(plates_start() && !plates_enabled && active_world == &legacy_world);
  puts("plates: restart, profiles, sidecar saves, malformed catalogs, path checks and disabled legacy mode passed");
}
