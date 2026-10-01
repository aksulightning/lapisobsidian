#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include "commands.h"
#include "crafting.h"
#include "doors.h"
#include "items.h"
#include "mobs.h"
#include "packets.h"
#include "procedures.h"
#include "registries.h"
#include "serialize.h"
#include "signs.h"
#include "tools.h"
#include "varnum.h"
#include "worldgen.h"

static int fd[2], watch[2];
static PlayerData *p = &player_data[0], *q = &player_data[1];
static void drain (void) { uint8_t b[4096]; while (recv(fd[0],b,sizeof(b),MSG_DONTWAIT)>0) {} while (recv(watch[0],b,sizeof(b),MSG_DONTWAIT)>0) {} }
static void tick (void) { mobs_tick(1000000); drain(); }
static void arrow_tick (void) { mobs_tick_arrows(100000); drain(); }
static unsigned count (void) { unsigned n = 0; for (int i = 0; i < MAX_MOBS; i++) n += mob_data[i].type != 0; return n; }
static unsigned drops (uint16_t item) { unsigned n = 0; for (size_t i = 0; i < ITEM_ENTITY_LIMIT; i++) if (items_at(i)->item == item) n += items_at(i)->count; return n; }
static void block (int x, int y, int z, uint8_t b) { assert(!makeBlockChange((short)x,(uint8_t)y,(short)z,b)); drain(); }
static void mode (const char *s) { char cmd[64]; snprintf(cmd,sizeof(cmd),"gamemode %s",s); assert(commands_execute(p,cmd,strlen(cmd)) == COMMAND_OK); drain(); }
static void clean (void) {
  mobs_clear(); items_clear(); drain();
  p->flags = q->flags = 0; p->health = q->health = 20; p->hunger = q->hunger = 0;
  p->x = p->z = 0; p->y = 201; q->x = -4; q->z = 0; q->y = 201;
  p->hotbar = q->hotbar = 0; p->flagval_16 = 0; p->flagval_8 = 0;
  memset(p->inventory_items,0,sizeof(p->inventory_items)); memset(p->inventory_count,0,sizeof(p->inventory_count));
  memset(p->craft_items,0,sizeof(p->craft_items));
  world_time = 13000; mode("survival");
}
static uint32_t var (const uint8_t *b, size_t n, size_t *at) {
  uint32_t v = 0;
  for (unsigned shift = 0; shift < 35; shift += 7) { assert(*at < n); uint8_t c = b[(*at)++]; assert(shift < 28 || !(c&240u)); v |= (uint32_t)(c&127u)<<shift; if (!(c&128u)) return v; }
  assert(0); return 0;
}
static unsigned frame (uint8_t *b, size_t *n, size_t *at) {
  int len = readVarInt(fd[0]); assert(len > 0 && len <= 256); assert(recv_all(fd[0],b,(size_t)len,false) == len);
  *n = (size_t)len; *at = 0; return var(b,*n,at);
}
static int delta (const uint8_t *b, size_t *at) {
  unsigned raw = (unsigned)b[*at]*256u+b[*at+1]; *at += 2;
  return raw < 32768 ? (int)raw : (int)raw-65536;
}
static void movement (int dx, int dy, int dz, bool grounded) {
  uint8_t b[256]; size_t n,at;
  assert(frame(b,&n,&at) == 0x2f && var(b,n,&at) == UINT32_MAX-1);
  assert(n-at == 9 && delta(b,&at) == dx && delta(b,&at) == dy && delta(b,&at) == dz);
  at++; assert(b[at++] == 0 && b[at++] == grounded && at == n);
}
static bool quiet (void) { uint8_t b; return recv(fd[0],&b,1,MSG_DONTWAIT) < 0; }
static double number (const uint8_t *b, size_t *at) {
  uint64_t bits = 0; for (unsigned i = 0; i < 8; i++) bits = (bits<<8)|b[(*at)++];
  double value; memcpy(&value,&bits,sizeof(value)); return value;
}
static void walking_tests (void) {
  /* Exact protocol endpoints in both directions, including negative coordinates. */
  for (int direction = -1; direction <= 1; direction += 2) {
    clean(); p->x = (short)(direction*10); q->x = -100;
    assert(mobs_spawn(MOB_ZOMBIE,0,201,0)); tick(); assert(mob_data[0].x == direction);
    int sum = 0;
    mobs_tick_movement(0); mobs_tick_movement(-1); assert(quiet());
    for (int t = 1; t <= 20; t++) {
      int next = direction*(4096-(20-t)*4096/20);
      mobs_tick_movement(50000); movement(next-sum,0,0,true); assert(quiet()); sum = next; drain();
    }
    assert(sum == direction*4096);
    mobs_tick_movement(INT64_MAX); assert(quiet());
  }
  /* View re-entry begins at the transmitted fractional position, not destination. */
  clean(); assert(mobs_spawn(MOB_ZOMBIE,8,201,0)); tick();
  mobs_tick_movement(500000); movement(-2048,0,0,true); drain();
  mobs_forget_player(p); mobs_sync_player(p);
  uint8_t b[256]; size_t n,at;
  assert(frame(b,&n,&at) == 1 && var(b,n,&at) == UINT32_MAX-1); at += 16;
  assert(var(b,n,&at) == MOB_ZOMBIE);
  assert(number(b,&at) == 8.0 && number(b,&at) == 201.0 && number(b,&at) == 0.5); drain();
  mobs_tick_movement(500000); movement(-2048,0,0,true); drain();
  /* Upward steps lift first, downward steps leave the ledge before lowering. */
  clean(); block(7,201,0,B_stone); assert(mobs_spawn(MOB_ZOMBIE,8,201,0)); tick();
  assert(mob_data[0].x == 7 && mob_data[0].y == 202);
  mobs_tick_movement(500000); movement(0,4096,0,false); drain();
  mobs_tick_movement(500000); movement(-4096,0,0,true); drain();
  tick(); assert(mob_data[0].x == 6 && mob_data[0].y == 201);
  mobs_tick_movement(500000); movement(-4096,0,0,false); drain();
  mobs_tick_movement(500000); movement(0,-4096,0,true); drain();
  block(7,201,0,B_air);
  /* Blocked movement produces no positional update. Death stops a pending step. */
  clean(); block(7,201,0,B_stone); block(7,202,0,B_stone);
  assert(mobs_spawn(MOB_ZOMBIE,8,201,0)); tick(); assert(mob_data[0].x == 8);
  mobs_tick_movement(500000); assert(quiet()); block(7,201,0,B_air); block(7,202,0,B_air);
  tick(); mobs_tick_movement(50000); drain(); hurtEntity(-2,-1,D_generic,20); drain();
  mobs_tick_movement(50000); assert(quiet()); tick(); assert(!count());
  /* Reused slots reset interpolation; large elapsed times emit one bounded step. */
  assert(mobs_spawn(MOB_ZOMBIE,8,201,0)); drain(); mobs_tick_movement(50000); assert(quiet());
  tick(); mobs_tick_movement(INT64_MAX); movement(-4096,0,0,true); drain();
  mobs_tick_movement(INT64_MAX); assert(quiet()); clean();
}
static void string (const uint8_t *b, size_t n, size_t *at, const char *s) {
  uint32_t len = var(b,n,at); assert(len == strlen(s) && n-*at >= len); assert(!memcmp(b+*at,s,len)); *at += len;
}
static int interact (const uint8_t *b, size_t n) {
  assert(send(fd[0],b,n,0) == (ssize_t)n); recv_count = 1; int result = cs_interact(fd[1],(int)n); drain(); return result;
}
int main (void) {
  assert(mkdir(".tests/mob-world",0700) == 0 || access(".tests/mob-world",F_OK) == 0);
  assert(chdir(".tests/mob-world") == 0); remove("world.bin");
  for (int i = 0; i < MAX_PLAYERS; i++) player_data[i].client_fd = -1;
  for (int i = 0; i < MAX_BLOCK_CHANGES; i++) block_changes[i].block = 255;
  assert(initSerializer() == 0 && signs_load(NULL) && doors_load(NULL));
  assert(socketpair(AF_UNIX,SOCK_STREAM,0,fd) == 0 && socketpair(AF_UNIX,SOCK_STREAM,0,watch) == 0);
  p->client_fd = fd[1]; q->client_fd = watch[1]; p->health = q->health = 20;
  assert(commands_configure("0123456789abcdef0123456789abcdef"));
  assert(commands_execute(p,"admin 0123456789abcdef0123456789abcdef",38) == COMMAND_OK); drain(); clean();
  /* Fixed test platform avoids depending on any terrain seed. */
  for (int x = -20; x <= 35; x++) for (int z = -20; z <= 20; z++)
    block_changes[block_changes_count++] = (BlockChange){(short)x,(short)z,200,B_stone};
  walking_tests();
  /* Grass drop vectors: shears preserve grass; bare hands roll wheat seeds. */
  assert(getMiningResult(I_shears,B_short_grass) == I_short_grass);
  unsigned seeds = 0;
  rng_seed = 1;
  for (int i = 0; i < 1024; i++) { uint16_t item = getMiningResult(0,B_short_grass); assert(!item || item == I_wheat_seeds); seeds += item != 0; }
  assert(seeds > 80 && seeds < 180);
  block(1,201,0,B_short_grass); rng_seed = 8;
  handlePlayerAction(p,0,1,201,0); drain();
  assert(getBlockAt(1,201,0) == B_air && drops(I_wheat_seeds) == 1); items_clear(); drain();
  for (uint8_t plant = B_dandelion; plant <= B_red_mushroom; plant++) {
    assert(isInstantlyMined(p,plant) && isPassableBlock(plant) && isColumnBlock(plant));
    block(1,201,0,plant); handlePlayerAction(p,0,1,201,0); drain();
    assert(getBlockAt(1,201,0) == B_air && drops(registry_block_item(plant)) == 1); items_clear(); drain();
  }
  for (int slot = 0; slot < 9; slot++) {
    memset(p->craft_items,0,sizeof(p->craft_items)); p->craft_items[slot] = I_short_grass;
    uint16_t item; uint8_t n; getCraftingOutput(p,&n,&item); assert(item == I_wheat_seeds && n == 1);
  }
  p->craft_items[0] = I_dirt; uint16_t item; uint8_t amount; getCraftingOutput(p,&amount,&item); assert(!amount);
  clean();
  /* Manual custom-recipe pickup consumes the real input, even if the client's
   * prediction says no cursor item. Empty-output replay cannot make more seeds. */
  const uint8_t craft[] = {0,0,0,0,0,0,1,0,1,0,0};
  for (size_t len = 1; len <= sizeof(craft); len++) {
    p->craft_items[0] = I_short_grass; p->craft_count[0] = 1;
    assert(send(fd[0],craft,len,0) == (ssize_t)len);
    int result = cs_clickContainer(fd[1],(int)len); drain();
    if (len < sizeof(craft)) assert(result == 1 && p->craft_count[0] == 1 && !p->flagval_8);
    else assert(result == 0 && !p->craft_count[0] && p->flagval_16 == I_wheat_seeds && p->flagval_8 == 1);
  }
  assert(send(fd[0],craft,sizeof(craft),0) == sizeof(craft)); assert(cs_clickContainer(fd[1],sizeof(craft)) == 0); drain();
  assert(p->flagval_8 == 1 && !p->craft_count[0]);
  /* The 2x2 grid's lower-right slot and all 3x3 rows use explicit mapping. */
  p->craft_items[4] = I_short_grass; p->craft_count[4] = 1;
  assert(send(fd[0],craft,sizeof(craft),0) == sizeof(craft)); assert(cs_clickContainer(fd[1],sizeof(craft)) == 0); drain();
  assert(!p->craft_count[4] && p->flagval_8 == 2);
  clean();
  assert(!mobs_by_name("dragon") && !mobs_by_name(NULL));
  assert(!mobs_spawn(255,2,201,0) && !mobs_spawn(MOB_SKELETON,32768,201,0));
  assert(!mobs_spawn(MOB_SKELETON,2,254,0) && !mobs_spawn(MOB_SKELETON,2,0,0));
  assert(!mobs_spawn(MOB_SKELETON,0,201,0));
  block(2,202,0,B_stone); assert(!mobs_spawn(MOB_SKELETON,2,201,0)); block(2,202,0,B_air);
  assert(mobs_spawn(MOB_SKELETON,8,201,0));
  /* Spawn and bow equipment lengths and registry IDs are decoded independently. */
  uint8_t data[256]; size_t n,at;
  assert(frame(data,&n,&at) == 1 && var(data,n,&at) == UINT32_MAX-1); at += 16; assert(var(data,n,&at) == 110 && n-at == 34);
  assert(frame(data,&n,&at) == 0x5f && var(data,n,&at) == UINT32_MAX-1);
  assert(data[at++] == 0 && data[at++] == 1 && var(data,n,&at) == I_bow);
  assert(data[at++] == 0 && data[at++] == 0 && at == n); drain();
  tick(); assert(mobs_arrow_count() == 1);
  for (int t = 0; t < 10; t++) arrow_tick();
  assert(p->health == 16 && q->health == 20 && !mobs_arrow_count());
  /* A solid wall catches arrows; line of sight suppresses new shots. */
  clean(); assert(mobs_spawn(MOB_SKELETON,8,201,0)); tick(); assert(mobs_arrow_count() == 1);
  block(4,201,0,B_stone); block(4,202,0,B_stone);
  for (int t = 0; t < 12; t++) arrow_tick();
  assert(!mobs_arrow_count() && p->health == 20);
  tick(); tick(); assert(!mobs_arrow_count()); block(4,201,0,B_air); block(4,202,0,B_air);
  clean(); assert(mobs_spawn(MOB_SKELETON,8,201,0)); mode("creative"); q->x = -100; tick(); assert(!mobs_arrow_count());
  mode("spectator"); tick(); assert(!mobs_arrow_count()); mode("survival"); tick(); assert(mobs_arrow_count() == 1);
  mobs_tick_arrows(INT64_MAX); drain(); assert(!mobs_arrow_count());
  /* Independent fixed projectile cap and lifetime cleanup. */
  clean(); assert(mobs_spawn(MOB_SKELETON,8,201,0));
  for (int t = 0; t < 80; t++) tick();
  assert(mobs_arrow_count() == MOB_ARROW_LIMIT); mobs_tick_arrows(INT64_MAX); drain(); assert(!mobs_arrow_count());
  /* Sunlight affects skeletons/zombies, but a solid roof supplies shade. */
  clean(); world_time = 1000; assert(mobs_spawn(MOB_SKELETON,8,201,0)); tick(); assert((mob_data[0].data&31) == 18);
  block(8,203,0,B_stone); tick(); assert((mob_data[0].data&31) == 18); block(8,203,0,B_air);
  /* Neutral spiders ignore players both day and night, then target the attacker. */
  clean(); assert(mobs_spawn(MOB_SPIDER,2,201,0));
  for (int t = 0; t < 5; t++) tick();
  assert(p->health == 20 && q->health == 20);
  world_time = 1000; for (int t = 0; t < 5; t++) tick();
  assert((mob_data[0].data&31) == 16);
  p->x = (short)(mob_data[0].x-2); p->z = mob_data[0].z; q->x = (short)(mob_data[0].x+1); q->z = mob_data[0].z;
  hurtEntity(-2,p->client_fd,D_generic,1); drain(); tick(); assert(p->health == 18 && q->health == 20);
  mobs_forget_player(p); p->x = 30; for (int t = 0; t < 5; t++) tick();
  assert(q->health == 20);
  clean(); assert(mobs_spawn(MOB_SPIDER,2,201,0)); hurtEntity(-2,p->client_fd,D_generic,1); drain(); p->x = 30;
  for (int t = 0; t < 16; t++) tick();
  p->x = (short)(mob_data[0].x-1); p->z = mob_data[0].z; tick(); assert(p->health == 20);
  /* Creeper fuse cancels out of range; completed burst changes no block or HP. */
  clean(); assert(mobs_spawn(MOB_CREEPER,2,201,0)); drain();
  mobs_tick(1000000);
  assert(frame(data,&n,&at) == 0x5c && var(data,n,&at) == UINT32_MAX-1);
  assert(data[at++] == 16 && data[at++] == 1 && var(data,n,&at) == 1 && data[at++] == 255 && at == n);
  assert(frame(data,&n,&at) == 0x6e && var(data,n,&at) == 0); string(data,n,&at,"minecraft:entity.creeper.primed");
  assert(data[at++] == 0 && var(data,n,&at) == 5 && n-at == 28); drain();
  p->x = -15; q->x = -16; tick(); tick(); assert(count() == 1);
  p->x = (short)(mob_data[0].x-2); p->z = mob_data[0].z;
  tick(); tick(); int edits = block_changes_count;
  static BlockChange before_burst[MAX_BLOCK_CHANGES]; memcpy(before_burst,block_changes,sizeof(block_changes));
  mobs_tick(1000000);
  assert(!count() && block_changes_count == edits && p->health == 20 && q->health == 20);
  assert(!memcmp(before_burst,block_changes,sizeof(block_changes)));
  assert(frame(data,&n,&at) == 0x6e && var(data,n,&at) == 0); string(data,n,&at,"minecraft:entity.firework_rocket.blast"); assert(n-at == 30);
  assert(frame(data,&n,&at) == 0x29 && n == 48 && data[1] == 0 && data[2] == 0 && data[n-1] == 29);
  assert(frame(data,&n,&at) == 0x46 && var(data,n,&at) == 1 && var(data,n,&at) == UINT32_MAX-1 && at == n); drain();
  /* Explicit mob deaths retain item drops; fireworks do not create loot. */
  assert(!drops(I_gunpowder)); clean(); assert(mobs_spawn(MOB_SKELETON,2,201,0)); drain();
  hurtEntity(-2,-1,D_generic,20); drain(); assert(drops(I_bone) && drops(I_arrow)); tick(); assert(!count());
  /* Attack frames are atomic; oversized, truncated and distant attacks do nothing. */
  clean(); assert(mobs_spawn(MOB_SPIDER,2,201,0)); drain();
  const uint8_t attack[] = {0xfe,0xff,0xff,0xff,0x0f,1,0};
  for (size_t len = 1; len < sizeof(attack); len++) { assert(interact(attack,len) == 1); assert((mob_data[0].data&31) == 16); }
  p->x = 30; assert(interact(attack,sizeof(attack)) == 0 && (mob_data[0].data&31) == 16);
  p->x = 0; assert(interact(attack,sizeof(attack)) == 0 && (mob_data[0].data&31) == 15);
  const uint8_t invalid[] = {0x80,0x80,0x80,0x80,0x80,1,0}; assert(interact(invalid,sizeof(invalid)) == 1);
  const uint8_t minimum[] = {0x80,0x80,0x80,0x80,0x08,1,0}; assert(interact(minimum,sizeof(minimum)) == 0);
  assert(cs_interact(fd[1],25) == 1);
  /* Admin command integration, capacity and exploration boundary checks. */
  clean(); assert(commands_execute(q,"spawnmob skeleton",17) == COMMAND_DENIED && !count()); drain();
  assert(commands_execute(p,"spawnmob skeleton",17) == COMMAND_OK && count() == 1); drain();
  assert(commands_execute(p,"spawnmob spider 4 201 4",23) == COMMAND_OK && count() == 2); drain();
  assert(commands_execute(p,"spawnmob creeper 0 201 0",24) == COMMAND_DENIED && count() == 2); drain();
  clean();
  const uint8_t natural[] = {MOB_ZOMBIE,MOB_SKELETON,MOB_SPIDER,MOB_CREEPER};
  for (uint32_t choice = 0; choice < 4; choice++) {
    mobs_spawn_exploration(0,0,0,0,201,(choice<<12)|(8u<<4)); drain();
    assert(count() == 1 && mob_data[0].type == natural[choice]); mobs_clear(); drain();
  }
  for (int i = 0; i < MAX_MOBS; i++) { assert(mobs_spawn(MOB_ZOMBIE,2*i,201,4)); drain(); }
  assert(!mobs_spawn(MOB_CREEPER,1,201,8));
  mobs_spawn_exploration(2047,2047,32767,32767,255,0); assert(count() == MAX_MOBS);
  mobs_spawn_exploration(-2048,-2048,-32768,-32768,0,0); assert(count() == MAX_MOBS);
  p->x = q->x = 1000; tick(); assert(!count());
  mobs_clear(); items_clear(); drain(); close(fd[0]); close(fd[1]); close(watch[0]); close(watch[1]); remove("world.bin"); assert(chdir("../..") == 0);
  puts("plants/mobs: smooth movement, exact relative frames, view re-entry, steps, death/reset, grass seeds and recipe, instant flowers, spawning, arrows/walls, neutral retaliation, harmless firecrackers, protocol frames, limits and admin permissions passed");
  return 0;
}
