#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include "circuits.h"
#include "commands.h"
#include "crafting.h"
#include "doors.h"
#include "farming.h"
#include "items.h"
#include "mobs.h"
#include "packets.h"
#include "procedures.h"
#include "registries.h"
#include "serialize.h"
#include "worldgen.h"

static int sockets[2];
static PlayerData *p = &player_data[0];
static uint8_t wire[65536];
static size_t used;
static unsigned sounds, blocks;
static char last_sound[100];
static unsigned category;
static uint32_t var (size_t *at) {
  uint32_t value = 0;
  for (unsigned shift = 0; shift < 35; shift += 7) {
    assert(*at < used); uint8_t b = wire[(*at)++]; assert(shift < 28 || !(b&240u));
    value |= (uint32_t)(b&127u)<<shift; if (!(b&128u)) return value;
  }
  assert(0); return 0;
}
static void drain (void) {
  used = 0;
  for (;;) { ssize_t n = recv(sockets[0],wire+used,sizeof(wire)-used,MSG_DONTWAIT); if (n <= 0) break; used += (size_t)n; assert(used < sizeof(wire)); }
  size_t at = 0;
  while (at < used) {
    uint32_t len = var(&at); assert(len && len <= used-at); size_t end = at+len;
    uint32_t id = var(&at);
    if (id == 0x6e) {
      assert(!var(&at)); uint32_t n = var(&at); assert(n < sizeof(last_sound) && n <= end-at);
      memcpy(last_sound,wire+at,n); last_sound[n] = 0; at += n;
      assert(wire[at++] == 0); category = var(&at); assert(end-at == 28); sounds++;
    } else if (id == 0x08) {
      assert(end-at >= 9); at += 8; assert(var(&at) <= 30000 && at == end); blocks++;
    }
    at = end;
  }
}
static void block (int x, int y, int z, uint8_t b) { assert(!makeBlockChange((short)x,(uint8_t)y,(short)z,b)); drain(); }
static void clean (void) {
  assert(farming_load(NULL) && circuits_load(NULL) && doors_load(NULL)); mobs_clear(); items_clear(); drain();
  for (int i = 0; i < MAX_BLOCK_CHANGES; i++) block_changes[i].block = 255;
  block_changes_count = 0; p->flags = 0; p->health = 20; p->x = 2; p->y = 201; p->z = 0; p->hotbar = 0;
  memset(p->inventory_items,0,sizeof(p->inventory_items)); memset(p->inventory_count,0,sizeof(p->inventory_count));
  memset(p->craft_items,0,sizeof(p->craft_items)); memset(p->craft_count,0,sizeof(p->craft_count)); sounds = blocks = 0;
}
static void hold (uint16_t item, uint8_t count) { p->inventory_items[0] = item; p->inventory_count[0] = count; }
static void use (int x, int y, int z) { handlePlayerUseItem(p,(short)x,(short)y,(short)z,1); drain(); }
static void tick (void) { circuits_tick(); drain(); }
static void grow (unsigned ticks) { for (unsigned i = 0; i < ticks; i++) { farming_tick(100000); drain(); } }
static uint16_t crop (void) { uint16_t v; assert(farming_state_at(0,201,0,B_wheat,&v)); return v; }
static unsigned drops (uint16_t item) { unsigned n = 0; for (unsigned i = 0; i < ITEM_ENTITY_LIMIT; i++) if (items_at(i)->item == item) n += items_at(i)->count; return n; }
static void place (int x, uint16_t item) { block(x,200,0,B_stone); assert(circuits_place(p,x,200,0,1,item)); drain(); }
static void recipe (uint16_t item, uint8_t count) { uint16_t got; uint8_t n; getCraftingOutput(p,&n,&got); assert(got == item && n == count); }
int main (void) {
  assert(mkdir(".tests/farming-world",0700) == 0 || access(".tests/farming-world",F_OK) == 0);
  assert(!chdir(".tests/farming-world")); remove("world.bin"); remove("farming.bin"); remove("circuits.bin");
  for (int i = 0; i < MAX_BLOCK_CHANGES; i++) block_changes[i].block = 255;
  for (int i = 0; i < MAX_PLAYERS; i++) player_data[i].client_fd = -1;
  assert(!initSerializer()); assert(!socketpair(AF_UNIX,SOCK_STREAM,0,sockets)); p->client_fd = sockets[1];
  assert(commands_configure("0123456789abcdef0123456789abcdef")); commands_reset_player(p); clean();
  /* Every wall orientation, correct supporting block and inversion. */
  static const int step[4][2] = {{0,-1},{0,1},{-1,0},{1,0}};
  for (unsigned d = 0; d < 4; d++) {
    clean(); block(0,201,0,B_stone);
    assert(circuits_place(p,0,201,0,(uint8_t)(d+2),I_redstone_torch)); drain(); tick();
    uint16_t state; int x = step[d][0], z = step[d][1];
    assert(circuits_state_at(x,201,z,B_redstone_torch,&state) && state == 5918+d*2);
    block(x,200,z,B_stone); block(x,200,z,B_air); assert(circuits_wall_torch_at(x,201,z));
    block(0,201,0,B_air); assert(getBlockAt(x,201,z) == B_air && drops(I_redstone_torch) == 1);
  }
  clean(); block(0,201,0,B_stone); assert(circuits_place(p,0,201,0,2,I_redstone_torch)); drain();
  place(1,I_lever); tick(); assert(circuits_power(0,201,-1) == 15);
  assert(circuits_interact(p,1,201,0)); drain(); tick(); tick(); assert(!circuits_power(0,201,-1));
  /* Plates poll occupancy even with no pending block changes. */
  for (unsigned wooden = 0; wooden < 2; wooden++) {
    clean(); place(0,wooden ? I_oak_pressure_plate : I_stone_pressure_plate); place(1,I_redstone); tick();
    assert(!circuits_power(0,201,0)); p->x = 0; tick(); assert(circuits_power(1,201,0) == 15);
    p->x = 2; tick(); assert(!circuits_power(1,201,0));
    mob_data[0] = (MobData){MOB_COW,0,201,0,10}; tick(); assert(circuits_power(0,201,0) == 15);
    mob_data[0].data = 0; tick(); assert(!circuits_power(0,201,0)); memset(mob_data,0,sizeof(mob_data));
    assert(items_spawn(I_stone,1,0,201,0,500)); drain(); tick(); assert((circuits_power(0,201,0) != 0) == wooden);
    items_clear(); drain(); tick(); assert(!circuits_power(0,201,0));
    block(0,200,0,B_air); assert(getBlockAt(0,201,0) == B_air);
  }
  /* Wood opens by hand; iron only by power. Consumers cannot power each other. */
  for (unsigned iron = 0; iron < 2; iron++) {
    clean(); assert(circuits_load("circuits.bin")); place(0,iron ? I_iron_trapdoor : I_oak_trapdoor); place(1,I_lever);
    uint16_t state; assert(circuits_state_at(0,201,0,iron ? B_iron_trapdoor : B_oak_trapdoor,&state) && state == (iron ? 11303 : 6155));
    assert(circuits_interact(p,0,201,0) == !iron); drain(); assert(circuits_trapdoor_open(0,201,0) == !iron);
    assert(circuits_load("circuits.bin")); tick(); assert(circuits_trapdoor_open(0,201,0) == !iron);
    assert(circuits_interact(p,1,201,0)); drain(); tick(); assert(circuits_trapdoor_open(0,201,0));
    assert(!circuits_powered(-1,201,0));
    assert(circuits_interact(p,1,201,0)); drain(); tick(); assert(!circuits_trapdoor_open(0,201,0));
    assert(circuits_load("circuits.bin")); tick(); assert(!circuits_trapdoor_open(0,201,0));
    remove("circuits.bin");
  }
  clean(); block(0,202,0,B_stone); assert(circuits_place(p,0,202,0,0,I_oak_trapdoor)); drain();
  uint16_t value; assert(circuits_state_at(0,201,0,B_oak_trapdoor,&value) && value == 6147);
  /* Version-one circuit saves remain readable, unknown versions/values do not. */
  clean(); block(0,200,0,B_stone); block(0,201,0,B_lever);
  uint8_t legacy[] = {'L','O','C','I','R','C',1,0,0,0,0,0,201,CIRCUIT_LEVER,1};
  FILE *f = fopen("circuits.bin","wb"); assert(f && fwrite(legacy,1,sizeof(legacy),f) == sizeof(legacy)); fclose(f);
  assert(circuits_load("circuits.bin")); tick(); assert(circuits_power(0,201,0) == 15);
  legacy[6] = 3; f = fopen("circuits.bin","wb"); assert(f); fwrite(legacy,1,sizeof(legacy),f); fclose(f); assert(!circuits_load("circuits.bin")); remove("circuits.bin");
  /* Till, seed, dry pause, water range, growth, restart, harvest and replant. */
  clean(); assert(farming_load("farming.bin")); block(0,200,0,B_dirt); hold(I_iron_hoe,1); use(0,200,0);
  assert(getBlockAt(0,200,0) == B_farmland); hold(I_wheat_seeds,3); use(0,200,0);
  assert(crop() == 4342 && p->inventory_count[0] == 2); grow(400); assert(crop() == 4342);
  block(5,200,0,B_water); grow(400); assert(crop() == 4342); block(4,200,0,B_water);
  grow(400); assert(crop() > 4342 && crop() < 4349);
  assert(farming_state_at(0,200,0,B_farmland,&value) && value == 4357);
  value = crop(); assert(farming_load("farming.bin")); assert(crop() == value);
  block(4,200,0,B_air); grow(640); assert(crop() == value);
  block(4,200,0,B_water); grow(2240); assert(crop() == 4349);
  blocks = 0; farming_send_chunk(sockets[1],0,0); drain(); assert(blocks == 2);
  /* Two distinct loot stacks cannot share the last empty item slot. */
  for (unsigned i = 0; i < ITEM_ENTITY_LIMIT-1; i++) assert(items_spawn(I_stone,64,100+(int)i,200,0,500));
  drain(); handlePlayerAction(p,0,0,201,0); drain(); assert(crop() == 4349);
  items_clear(); drain(); handlePlayerAction(p,0,0,201,0); drain();
  assert(getBlockAt(0,201,0) == B_air && drops(I_wheat) == 1 && drops(I_wheat_seeds) == 2);
  items_clear(); drain(); hold(I_wheat_seeds,2); use(0,200,0); assert(crop() == 4342);
  block(0,200,0,B_air); assert(getBlockAt(0,201,0) == B_air && drops(I_wheat_seeds) == 1);
  assert(!farming_state_at(0,200,0,B_farmland,&value));
  block(0,200,0,B_dirt); hold(I_iron_hoe,1); assert(!mkdir("farming.bin.tmp",0700)); use(0,200,0);
  assert(getBlockAt(0,200,0) == B_dirt); assert(!rmdir("farming.bin.tmp"));
  f = fopen("farming.bin","ab"); assert(f); fputc(1,f); fclose(f); assert(!farming_load("farming.bin")); remove("farming.bin");
  assert(!farming_state_at(32768,201,0,B_wheat,&value));
  /* Bounded plots, invalid coordinates and game modes do not consume seeds. */
  clean(); hold(I_wheat_seeds,64);
  assert(farming_use(p,32768,200,0,1)); assert(farming_use(p,0,255,0,1)); assert(p->inventory_count[0] == 64);
  assert(commands_execute(p,"admin 0123456789abcdef0123456789abcdef",38) == COMMAND_OK); drain();
  assert(commands_execute(p,"gamemode adventure",18) == COMMAND_OK); drain();
  block(0,200,0,B_farmland); use(0,200,0); assert(getBlockAt(0,201,0) == B_air && p->inventory_count[0] == 64);
  assert(commands_execute(p,"gamemode survival",17) == COMMAND_OK); drain();
  for (unsigned i = 0; i < FARM_LIMIT; i++) {
    p->x = (short)(i+2); block((int)i,200,0,B_farmland); hold(I_wheat_seeds,1); use((int)i,200,0);
    assert(getBlockAt((int)i,201,0) == B_wheat && !p->inventory_count[0]);
  }
  p->x = FARM_LIMIT; block(FARM_LIMIT,200,0,B_farmland); hold(I_wheat_seeds,1); use(FARM_LIMIT,200,0);
  assert(getBlockAt(FARM_LIMIT,201,0) == B_air && p->inventory_count[0] == 1);
  /* Recipes and authoritative cursor crafting, without trusting predictions. */
  clean(); p->craft_items[3] = p->craft_items[4] = p->craft_items[5] = I_wheat; recipe(I_bread,1);
  p->craft_count[3] = p->craft_count[4] = p->craft_count[5] = 1;
  const uint8_t click[] = {12,0,0,0,0,0,0,0};
  assert(send(sockets[0],click,sizeof(click),0) == sizeof(click)); recv_count = 1;
  assert(!cs_clickContainer(sockets[1],sizeof(click))); drain(); assert(p->flagval_16 == I_bread && p->flagval_8 == 1);
  assert(!p->craft_items[3] && !p->craft_count[4]);
  hold(I_bread,1); p->hunger = 10; assert(handlePlayerEating(p,false)); drain(); assert(p->hunger == 15);
  memset(p->craft_items,0,sizeof(p->craft_items)); p->craft_items[0] = p->craft_items[1] = I_stone; recipe(I_stone_pressure_plate,1);
  p->craft_items[0] = p->craft_items[1] = I_oak_planks; recipe(I_oak_pressure_plate,1);
  p->craft_items[4] = p->craft_items[7] = I_stick; recipe(I_wooden_hoe,1);
  p->craft_items[0] = p->craft_items[1] = I_iron_ingot; recipe(I_iron_hoe,1);
  p->craft_items[4] = p->craft_items[7] = 0; p->craft_items[3] = p->craft_items[4] = I_iron_ingot; recipe(I_iron_trapdoor,1);
  for (unsigned i = 0; i < 6; i++) p->craft_items[i] = I_oak_planks;
  recipe(I_oak_trapdoor,2);
  /* Existing named client sounds, correctly framed and distance bounded. */
  clean(); mob_data[0] = (MobData){MOB_COW,0,201,0,10}; mobs_hurt_sound(-2,false); drain();
  assert(sounds == 1 && category == 6 && !strcmp(last_sound,"minecraft:entity.cow.hurt"));
  mobs_hurt_sound(-2,true); drain(); assert(!strcmp(last_sound,"minecraft:entity.cow.death"));
  mob_data[0].type = MOB_ZOMBIE; mobs_hurt_sound(-2,false); drain(); assert(category == 5 && !strcmp(last_sound,"minecraft:entity.zombie.hurt"));
  p->y = 240; sounds = 0; mobs_hurt_sound(-2,false); drain(); assert(!sounds); p->y = 201;
  mob_data[0].type = MOB_COW; for (int i = 0; i < 9; i++) { mobs_tick(1000000); drain(); }
  assert(sounds && !strcmp(last_sound,"minecraft:entity.cow.ambient"));
  clean(); close(sockets[0]); close(sockets[1]); remove("world.bin"); remove("farming.bin"); remove("circuits.bin"); assert(!chdir("../.."));
  puts("farming/circuits: wall support, plates, trapdoors, save compatibility, irrigated growth, harvest capacity, recipes, sound frames and range passed");
  return 0;
}
