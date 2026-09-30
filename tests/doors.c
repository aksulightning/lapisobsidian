#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include "doors.h"
#include "items.h"
#include "signs.h"
#include "commands.h"
#include "crafting.h"
#include "packets.h"
#include "procedures.h"
#include "registries.h"
#include "serialize.h"
#include "tools.h"
#include "varnum.h"
#include "worldgen.h"

static int sockets[2];
static PlayerData *p = &player_data[0];
static void drain (void) { uint8_t b[4096]; while (recv(sockets[0],b,sizeof(b),MSG_DONTWAIT)>0) {} }
static void block (int x, int y, int z, uint8_t b) { assert(!makeBlockChange((short)x,(uint8_t)y,(short)z,b)); drain(); }
static void clean (void) {
  assert(doors_load(NULL));
  for (int i = 0; i < MAX_BLOCK_CHANGES; i++) block_changes[i].block = 255;
  block_changes_count = 0;
  p->x = -1; p->y = 201; p->z = -2; p->yaw = 0;
  p->inventory_items[0] = I_oak_door; p->inventory_count[0] = 10;
  block(-2,200,-2,B_stone);
}
static uint64_t position (int x, int y, int z) {
  return (((uint64_t)(int64_t)x&0x3ffffffu)<<38)|(((uint64_t)(int64_t)z&0x3ffffffu)<<12)|((uint64_t)y&4095u);
}
static unsigned var (const uint8_t *data, size_t n, size_t *at) {
  unsigned value = 0;
  for (unsigned shift = 0; shift < 35; shift += 7) {
    assert(*at < n); unsigned b = data[(*at)++]; value |= (b&127u)<<shift; if (!(b&128u)) return value;
  }
  assert(0); return 0;
}
static void wire (uint16_t lower, uint16_t upper) {
  doors_send_chunk(sockets[1],-1,-1);
  for (int half = 0; half < 2; half++) {
    uint8_t data[32]; int n = readVarInt(sockets[0]); assert(n > 0 && n <= 32);
    assert(recv_all(sockets[0],data,(size_t)n,0) == n);
    size_t at = 0; assert(var(data,(size_t)n,&at) == 8);
    uint64_t v = 0; for (int i = 0; i < 8; i++) v = (v<<8)|data[at++];
    assert(v == position(-2,201+half,-2));
    assert(var(data,(size_t)n,&at) == (half ? upper : lower) && at == (size_t)n);
  }
}
static void mode (const char *name) { char cmd[40]; snprintf(cmd,sizeof(cmd),"gamemode %s",name); assert(commands_execute(p,cmd,strlen(cmd)) == COMMAND_OK); drain(); }
int main (void) {
  assert(mkdir(".tests/door-world",0700) == 0 || access(".tests/door-world",F_OK) == 0);
  assert(chdir(".tests/door-world") == 0); remove("world.bin"); remove("doors.bin");
  assert(signs_load(NULL)); assert(doors_load(NULL));
  for (int i = 0; i < MAX_BLOCK_CHANGES; i++) block_changes[i].block = 255;
  for (int i = 0; i < MAX_PLAYERS; i++) player_data[i].client_fd = -1;
  assert(initSerializer() == 0);
  assert(socketpair(AF_UNIX,SOCK_STREAM,0,sockets) == 0); p->client_fd = sockets[1]; p->health = 20;
  assert(commands_configure("0123456789abcdef0123456789abcdef"));
  clean();
  assert(!doors_at(-32769,201,0) && !doors_at(0,256,0));
  assert(!doors_place(p,-2,200,-2,0) && !doors_place(p,-2,200,-2,5));
  assert(!doors_place(p,-32769,200,0,1));
  block(-2,202,-2,B_stone); assert(!doors_place(p,-2,200,-2,1)); block(-2,202,-2,B_air);
  p->x = -2; assert(!doors_place(p,-2,200,-2,1)); p->x = -1;
  p->x = 100; assert(!doors_place(p,-2,200,-2,1)); p->x = -1;
  p->y = 255; block(-2,254,-2,B_stone); assert(!doors_place(p,-2,254,-2,1)); p->y = 201;
  /* Four independent protocol-state vectors, including both halves and open. */
  static const int8_t yaw[4] = {-128,0,64,-64};
  static const uint16_t expected[4][4] = {{4697,4689,4695,4687},{4713,4705,4711,4703},{4729,4721,4727,4719},{4745,4737,4743,4735}};
  for (unsigned i = 0; i < 4; i++) {
    p->yaw = yaw[i]; handlePlayerUseItem(p,-2,200,-2,1); drain();
    const Door *d = doors_at(-2,201,-2); assert(d && d == doors_at(-2,202,-2) && d->facing == i && !d->open);
    assert(p->inventory_count[0] == 9-i); wire(expected[i][0],expected[i][1]);
    assert(doors_interact(p,-2,202,-2)); drain(); assert(d->open); wire(expected[i][2],expected[i][3]);
    assert(doors_interact(p,-2,201,-2)); drain(); assert(!d->open);
    block(-2,201,-2,B_air); assert(!doors_at(-2,202,-2) && getBlockAt(-2,202,-2) == B_air);
  }
  /* One drop for either half; repeated mining cannot duplicate it. */
  for (int half = 0; half < 2; half++) {
    handlePlayerUseItem(p,-2,200,-2,1); drain(); unsigned before = p->inventory_count[0];
    handlePlayerAction(p,2,-2,(short)(201+half),-2); drain();
    assert(!doors_at(-2,201,-2) && getBlockAt(-2,201,-2) == B_air && getBlockAt(-2,202,-2) == B_air);
    assert(p->inventory_count[0] == before);
    items_tick(1000000); drain();
    assert(p->inventory_count[0] == before+1);
    handlePlayerAction(p,2,-2,(short)(201+half),-2); drain(); assert(p->inventory_count[0] == before+1);
  }
  assert(commands_execute(p,"admin 0123456789abcdef0123456789abcdef",38) == COMMAND_OK); drain();
  mode("creative"); unsigned before = p->inventory_count[0];
  handlePlayerUseItem(p,-2,200,-2,1); drain(); assert(p->inventory_count[0] == before);
  handlePlayerAction(p,0,-2,202,-2); drain(); assert(!doors_at(-2,201,-2) && p->inventory_count[0] == before);
  assert(doors_place(p,-2,200,-2,1)); drain();
  mode("adventure"); assert(!doors_place(p,-2,200,-2,1)); assert(doors_interact(p,-2,201,-2)); drain();
  mode("spectator"); assert(!doors_interact(p,-2,201,-2));
  mode("survival");
  block(-2,201,-2,B_stone); assert(!doors_at(-2,201,-2) && getBlockAt(-2,201,-2) == B_stone && getBlockAt(-2,202,-2) == B_air);
  block(-2,201,-2,B_air);
  assert(doors_load("doors.bin")); p->yaw = 0;
  assert(doors_place(p,-2,200,-2,1)); drain(); assert(doors_interact(p,-2,202,-2)); drain();
  assert(doors_at(-2,201,-2)->open);
  assert(doors_load("doors.bin") && doors_at(-2,201,-2)->open); wire(4711,4703);
  /* A failed save restores the old state, or both old placement cells. */
  assert(mkdir("doors.bin.tmp",0700) == 0);
  assert(!doors_interact(p,-2,201,-2)); drain(); assert(doors_at(-2,201,-2)->open);
  block(-4,200,-2,B_stone); block(-4,201,-2,B_short_grass);
  assert(!doors_place(p,-4,200,-2,1)); drain(); assert(getBlockAt(-4,201,-2) == B_short_grass && getBlockAt(-4,202,-2) == B_air);
  assert(rmdir("doors.bin.tmp") == 0);
  /* Durable corrupt fixtures must fail before modifying world blocks. */
  uint8_t saved[15]; FILE *f = fopen("doors.bin","rb"); assert(f); assert(fread(saved,1,15,f) == 15); assert(fgetc(f) == EOF); fclose(f);
  for (unsigned test = 0; test < 7; test++) {
    uint8_t data[15]; memcpy(data,saved,15);
    if (test == 0) data[6] = 2;
    if (test == 1) data[12] = 255;
    if (test == 2) data[13] = 4;
    if (test == 3) data[14] = 2;
    f = fopen("doors.bin","wb"); assert(f); assert(fwrite(data,1,test == 4 ? 14 : 15,f) == (test == 4 ? 14u : 15u));
    if (test == 5) { data[12]++; assert(fwrite(data+8,1,7,f) == 7); } /* overlapping pair */
    if (test == 6) for (unsigned i = 0; i < DOOR_LIMIT; i++) { data[8] = 0; data[9] = (uint8_t)i; assert(fwrite(data+8,1,7,f) == 7); }
    fclose(f); assert(!doors_load("doors.bin")); assert(getBlockAt(-2,201,-2) == B_oak_door);
  }
  f = fopen("doors.bin","wb"); assert(f); assert(fwrite(saved,1,15,f) == 15); fclose(f);
  assert(doors_load("doors.bin"));
  block(-2,200,-2,B_dirt); assert(doors_at(-2,201,-2));
  block(-2,200,-2,B_air); assert(!doors_at(-2,201,-2));
  assert(doors_load("doors.bin") && !doors_at(-2,201,-2));
  block(-2,200,-2,B_stone); assert(doors_place(p,-2,200,-2,1)); drain();
  /* Interrupted/legacy single-half state is cleaned up on load. */
  for (int i = 0; i < block_changes_count; i++) if (block_changes[i].x == -2 && block_changes[i].z == -2 && block_changes[i].y == 202) block_changes[i].block = B_air;
  assert(doors_load("doors.bin") && !doors_at(-2,201,-2)); assert(getBlockAt(-2,201,-2) == B_air); drain();
  block(-2,201,-2,B_oak_door); assert(doors_load("doors.bin")); drain(); assert(getBlockAt(-2,201,-2) == B_air);
  /* Craft either horizontal alignment of the six-plank Beta one-door recipe. */
  for (unsigned offset = 0; offset < 2; offset++) {
    memset(p->craft_items,0,sizeof(p->craft_items));
    for (unsigned row = 0; row < 3; row++) for (unsigned col = 0; col < 2; col++) p->craft_items[row*3+col+offset] = I_oak_planks;
    uint8_t count; uint16_t item; getCraftingOutput(p,&count,&item); assert(count == 1 && item == I_oak_door);
  }
  clean();
  /* The second half failing at edit capacity must not leave the first. */
  block_changes_count = MAX_BLOCK_CHANGES-1;
  for (int i = 1; i < MAX_BLOCK_CHANGES-1; i++) block_changes[i] = (BlockChange){.x=(short)i,.z=100,.y=0,.block=B_stone};
  assert(!doors_place(p,-2,200,-2,1)); drain(); assert(!doors_at(-2,201,-2) && getBlockAt(-2,201,-2) == B_air && getBlockAt(-2,202,-2) == B_air);
  clean();
  for (int i = 0; i < DOOR_LIMIT; i++) {
    p->x = (short)(1000+i+1); p->z = 0;
    block(1000+i,200,0,B_stone); assert(doors_place(p,1000+i,200,0,1)); drain();
  }
  p->x = 1000+DOOR_LIMIT+1; block(1000+DOOR_LIMIT,200,0,B_stone);
  assert(!doors_place(p,1000+DOOR_LIMIT,200,0,1)); drain();
  close(sockets[0]); close(sockets[1]); remove("doors.bin"); remove("world.bin"); assert(chdir("../..") == 0);
  puts("doors: two halves, facing/state packets, toggles, crafting, single drops, permissions, save rollback, restart, limits and stale cleanup passed");
  return 0;
}
