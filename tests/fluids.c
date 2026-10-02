#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include "fluids.h"
#include "circuits.h"
#include "commands.h"
#include "crafting.h"
#include "farming.h"
#include "items.h"
#include "packets.h"
#include "procedures.h"
#include "registries.h"
#include "serialize.h"
#include "worldgen.h"

static int sockets[2];
static PlayerData *p = &player_data[0];
static unsigned sounds;
static char last_sound[100];
static uint32_t var (const uint8_t *data, size_t size, size_t *at) {
  uint32_t value = 0;
  for (unsigned shift = 0; shift < 35; shift += 7) {
    assert(*at < size); uint8_t b = data[(*at)++]; assert(shift < 28 || !(b&240u));
    value |= (uint32_t)(b&127u)<<shift; if (!(b&128u)) return value;
  }
  assert(0); return 0;
}
static void drain (void) {
  uint8_t bytes[65536]; size_t n = 0;
  for (;;) {
    ssize_t got = recv(sockets[0],bytes+n,sizeof(bytes)-n,MSG_DONTWAIT);
    if (got <= 0) break;
    n += (size_t)got; assert(n < sizeof(bytes));
  }
  size_t at = 0;
  while (at < n) {
    uint32_t length = var(bytes,n,&at); assert(length && length <= n-at); size_t end = at+length;
    uint32_t id = var(bytes,end,&at);
    if (id == 0x6e) {
      assert(!var(bytes,end,&at)); uint32_t size = var(bytes,end,&at); assert(size < sizeof(last_sound) && size <= end-at);
      memcpy(last_sound,bytes+at,size); last_sound[size] = 0; at += size;
      assert(bytes[at++] == 0 && var(bytes,end,&at) == 4 && end-at == 28); sounds++;
    }
    at = end;
  }
}
static void block (int x, int y, int z, uint8_t b) {
  assert(!makeBlockChange((short)x,(uint8_t)y,(short)z,b)); drain();
}
static void tick (unsigned n) { for (unsigned i = 0; i < n; i++) { fluids_tick(100000); drain(); } }
static void clean (void) {
  for (int i = 0; i < MAX_BLOCK_CHANGES; i++) block_changes[i].block = 255;
  block_changes_count = 0; items_clear(); assert(circuits_load(NULL) && farming_load(NULL));
  p->flags = 0; p->health = 20; p->hotbar = 0; p->x = 0; p->y = 202; p->z = 0;
  memset(p->inventory_items,0,sizeof(p->inventory_items));
  memset(p->inventory_count,0,sizeof(p->inventory_count));
  fluids_init(); sounds = 0;
}
static void floor_at (int y, int radius) {
  for (int x = -radius; x <= radius; x++) for (int z = -radius; z <= radius; z++) block(x,y,z,B_stone);
  tick(100); /* flush setup work before timing propagation */
}
static void hold (uint16_t item, uint8_t count) { p->inventory_items[0] = item; p->inventory_count[0] = count; }
static void bucket (uint16_t item, uint8_t count) {
  hold(item,count); assert(fluids_use_bucket(p,0,90)); drain();
}
static void air_square (int y, int radius) {
  for (int x = -radius; x <= radius; x++) for (int z = -radius; z <= radius; z++) assert(getBlockAt(x,y,z) == B_air);
}
static void packet_float (uint8_t *out, float f) {
  uint32_t b; memcpy(&b,&f,4);
  for (unsigned i = 0; i < 4; i++) out[i] = (uint8_t)(b>>(24u-8u*i));
}
static int use_packet (const uint8_t *bytes, size_t n) {
  assert(send(sockets[0],bytes,n,0) == (ssize_t)n);
  int r = cs_useItem(sockets[1],(int)n);
  uint8_t unread[32]; while (recv(sockets[1],unread,sizeof(unread),MSG_DONTWAIT) > 0) {}
  drain(); return r;
}
int main (void) {
  assert(!mkdir(".tests/fluid-world",0700) || !access(".tests/fluid-world",F_OK));
  assert(!chdir(".tests/fluid-world")); remove("world.bin");
  for (int i = 0; i < MAX_PLAYERS; i++) player_data[i].client_fd = -1;
  for (int i = 0; i < MAX_BLOCK_CHANGES; i++) block_changes[i].block = 255;
  assert(!initSerializer()); assert(!socketpair(AF_UNIX,SOCK_STREAM,0,sockets)); p->client_fd = sockets[1];
  assert(commands_configure("0123456789abcdef0123456789abcdef")); commands_reset_player(p);
  clean(); floor_at(200,9); block(0,201,0,B_water);
  tick(1); assert(getBlockAt(1,201,0) == B_air);
  tick(80);
  for (int x = 1; x <= 7; x++) assert(getBlockAt(x,201,0) == B_water+x);
  assert(getBlockAt(8,201,0) == B_air);
  block(0,201,0,B_air); tick(400); air_square(201,8);
  puts("fluids: water range and source removal passed");

  clean(); floor_at(200,9); block(0,205,0,B_water); tick(150);
  for (int y = 201; y < 205; y++) assert(getBlockAt(0,y,0) == B_water_1);
  assert(getBlockAt(1,201,0) == B_water_1);
  block(0,205,0,B_air); tick(400);
  for (int y = 201; y <= 205; y++) air_square(y,8);
  puts("fluids: waterfalls drain without new sources");

  clean(); floor_at(200,5); block(0,201,0,B_lava); tick(5); assert(getBlockAt(1,201,0) == B_air);
  tick(80); assert(getBlockAt(1,201,0) == B_lava_2 && getBlockAt(3,201,0) == B_lava_6);
  assert(getBlockAt(4,201,0) == B_air);
  block(0,201,0,B_air); tick(300); air_square(201,4);
  puts("fluids: lava cadence, range and drainage passed");

  clean(); floor_at(200,5); block(0,201,0,B_lava); block(1,201,0,B_water); tick(20);
  assert(getBlockAt(0,201,0) == B_obsidian);
  block(-3,201,0,B_lava_2); block(-4,201,0,B_water); tick(20);
  assert(getBlockAt(-3,201,0) == B_cobblestone);
  block(0,204,0,B_water); block(0,205,0,B_lava); tick(20); assert(getBlockAt(0,204,0) == B_stone);
  puts("fluids: mixing passed");

  clean(); floor_at(200,9); block(0,201,0,B_water); tick(80);
  buildChunkSection(0,192,0);
  assert(chunk_section[(9u*256u+1u)^7u] == B_water_1);
  /* Restore the existing packed save and resume flow, including orphan cleanup. */
  writeBlockChangesToDisk(0,MAX_BLOCK_CHANGES-1);
  memset(block_changes,255,sizeof(block_changes)); block_changes_count = 0;
  assert(!initSerializer()); p->client_fd = sockets[1]; fluids_init(); tick(150);
  assert(getBlockAt(1,201,0) == B_water_1);
  block(0,201,0,B_air); fluids_init(); tick(500); air_square(201,8);
  puts("fluids: chunk serialization and restart recovery passed");

  clean(); floor_at(200,1); bucket(I_water_bucket,1);
  assert(sounds == 1 && !strcmp(last_sound,"minecraft:item.bucket.empty"));
  assert(getBlockAt(0,201,0) == B_water && p->inventory_items[0] == I_bucket);
  bucket(I_bucket,1); assert(getBlockAt(0,201,0) == B_air && p->inventory_items[0] == I_water_bucket);
  bucket(I_lava_bucket,1); assert(getBlockAt(0,201,0) == B_lava && p->inventory_items[0] == I_bucket);
  bucket(I_bucket,2); assert(getBlockAt(0,201,0) == B_air && p->inventory_count[0] == 1 && p->inventory_items[1] == I_lava_bucket);
  block(0,201,0,B_water);
  for (unsigned i = 1; i < 36; i++) { p->inventory_items[i] = I_stone; p->inventory_count[i] = 64; }
  bucket(I_bucket,2); assert(getBlockAt(0,201,0) == B_water && p->inventory_count[0] == 2);
  block(0,201,0,B_water_1); bucket(I_bucket,1); assert(getBlockAt(0,201,0) == B_water_1 && p->inventory_items[0] == I_bucket);
  block(0,201,0,B_air); block(0,202,0,B_stone); bucket(I_water_bucket,1); assert(getBlockAt(0,201,0) == B_air);
  assert(getItemStackSize(I_bucket) == 16 && getItemStackSize(I_lava_bucket) == 1 && getItemStackSize(I_water_bucket) == 1);
  memset(p->craft_items,0,sizeof(p->craft_items)); p->craft_items[0] = p->craft_items[2] = p->craft_items[4] = I_iron_ingot;
  uint16_t item; uint8_t count; getCraftingOutput(p,&count,&item); assert(item == I_bucket && count == 1);
  puts("fluids: buckets, inventory capacity, obstruction and recipe passed");

  clean(); floor_at(200,9);
  block(1,201,0,B_poppy); block(2,201,0,B_wheat);
  block(3,200,0,B_dirt); hold(I_wooden_hoe,1); assert(farming_use(p,3,200,0,1)); drain();
  hold(I_wheat_seeds,1); assert(farming_use(p,3,200,0,1)); drain();
  assert(circuits_place(p,-1,200,0,1,I_redstone)); drain();
  assert(circuits_place(p,-2,200,0,1,I_oak_pressure_plate)); drain();
  block(0,201,0,B_water); tick(100);
  assert(getBlockAt(1,201,0) == B_water_1 && getBlockAt(2,201,0) == B_water_2);
  assert(getBlockAt(-1,201,0) == B_water_1 && getBlockAt(-2,201,0) == B_water_2);
  unsigned flowers = 0, seeds = 0, dust = 0, plates = 0;
  for (unsigned i = 0; i < ITEM_ENTITY_LIMIT; i++) {
    const DroppedItem *d = items_at(i);
    if (d->item == I_poppy) flowers += d->count;
    if (d->item == I_wheat_seeds) seeds += d->count;
    if (d->item == I_redstone) dust += d->count;
    if (d->item == I_oak_pressure_plate) plates += d->count;
  }
  assert(flowers == 1 && seeds == 2 && dust == 1 && plates == 1);
  assert(getBlockAt(3,201,0) == B_water_3);
  uint16_t state;
  assert(!farming_state_at(3,201,0,B_wheat,&state));
  assert(!circuits_state_at(-1,201,0,B_torch,&state));
  puts("fluids: washed plants and circuit drops passed");

  clean(); floor_at(200,1);
  assert(commands_execute(p,"admin 0123456789abcdef0123456789abcdef",38) == COMMAND_OK); drain();
  assert(commands_execute(p,"gamemode adventure",18) == COMMAND_OK); drain();
  bucket(I_water_bucket,1); assert(getBlockAt(0,201,0) == B_air);
  assert(commands_execute(p,"gamemode spectator",18) == COMMAND_OK); drain();
  bucket(I_water_bucket,1); assert(getBlockAt(0,201,0) == B_air);
  assert(commands_execute(p,"gamemode creative",17) == COMMAND_OK); drain();
  bucket(I_water_bucket,1); assert(getBlockAt(0,201,0) == B_water && p->inventory_items[0] == I_water_bucket);
  assert(commands_execute(p,"gamemode survival",17) == COMMAND_OK); drain();
  block(0,201,0,B_water_1); bucket(I_water_bucket,1); assert(getBlockAt(0,201,0) == B_water);
  bucket(I_lava_bucket,1); assert(getBlockAt(0,201,0) == B_stone && p->inventory_items[0] == I_bucket);
  puts("fluids: game modes and pouring into flowing or opposing fluid passed");

  clean(); floor_at(200,1); hold(I_water_bucket,1);
  uint8_t packet[10] = {0,3}; packet_float(packet+2,0); packet_float(packet+6,90);
  for (size_t n = 1; n < sizeof(packet); n++) assert(use_packet(packet,n));
  assert(getBlockAt(0,201,0) == B_air);
  uint8_t malformed[18]; memcpy(malformed,packet,10); malformed[10] = 0; assert(use_packet(malformed,11));
  memset(malformed,128,6); memset(malformed+6,0,12); assert(use_packet(malformed,18));
  packet[0] = 2; assert(use_packet(packet,10)); packet[0] = 1;
  assert(!use_packet(packet,10) && getBlockAt(0,201,0) == B_air); packet[0] = 0;
  packet_float(packet+2,NAN); assert(use_packet(packet,10)); packet_float(packet+2,0);
  packet_float(packet+6,91); assert(use_packet(packet,10)); packet_float(packet+6,90);
  p->health = 0; assert(!use_packet(packet,10) && getBlockAt(0,201,0) == B_air); p->health = 20;
  assert(!use_packet(packet,10) && getBlockAt(0,201,0) == B_water && p->inventory_items[0] == I_bucket);
  puts("fluids: Use Item validation and main-hand dispatch passed");

  clean();
  /* Full queue, invalid coordinates, Y=0/255 and compact coordinate edges. */
  fluids_block_changed(INT_MIN,0,0); fluids_block_changed(INT_MAX,0,0);
  fluids_block_changed(0,INT_MIN,0); fluids_block_changed(0,INT_MAX,0);
  for (int x = 100; x < 1500; x++) block(x,220,0,B_stone);
  block(32767,254,32767,B_stone); block(32766,255,32767,B_stone); block(32767,255,32766,B_stone);
  block(32767,255,32767,B_water);
  block(-32768,0,-32768,B_water); block(0,255,0,B_water_1);
  tick(1000); assert(getBlockAt(0,255,0) == B_air);
  assert(getBlockAt(32767,255,32767) == B_water && getBlockAt(-32768,0,-32768) == B_water);
  puts("fluids: overflow recovery and coordinate boundaries passed");
  clean(); floor_at(200,9); block(0,201,0,B_water);
  /* Fill edit storage directly: simulation must pause, then resume after space
   * is made available, without losing the rejected update or a bucket. */
  int first_unused = block_changes_count;
  for (int i = block_changes_count; i < MAX_BLOCK_CHANGES; i++)
    block_changes[i] = (BlockChange){.x=(short)(100+i),.y=220,.z=100,.block=B_stone};
  block_changes_count = MAX_BLOCK_CHANGES;
  tick(20); assert(getBlockAt(1,201,0) == B_air);
  for (int i = first_unused; i < MAX_BLOCK_CHANGES; i++) block_changes[i].block = 255;
  fluids_block_changed(0,201,0); tick(200); assert(getBlockAt(1,201,0) == B_water_1);
  puts("fluids: edit-capacity pause and recovery passed");
  close(sockets[0]); close(sockets[1]); puts("fluids: passed");
}
