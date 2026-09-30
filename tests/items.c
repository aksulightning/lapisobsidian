#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include "items.h"
#include "commands.h"
#include "doors.h"
#include "packets.h"
#include "procedures.h"
#include "registries.h"
#include "serialize.h"
#include "signs.h"
#include "tools.h"
#include "varnum.h"
#include "worldgen.h"

static int sockets[2], observer[2];
static PlayerData *p = &player_data[0], *other = &player_data[1];
static void drain_fd (int fd) { uint8_t b[8192]; while (recv(fd,b,sizeof(b),MSG_DONTWAIT)>0) {} }
static void drain (void) { drain_fd(sockets[0]); drain_fd(observer[0]); }
static unsigned total (void) { unsigned n = 0; for (size_t i = 0; i < ITEM_ENTITY_LIMIT; i++) n += items_at(i)->count; return n; }
static const DroppedItem *first (void) { for (size_t i = 0; i < ITEM_ENTITY_LIMIT; i++) if (items_at(i)->count) return items_at(i); return NULL; }
static void clean (void) {
  items_clear(); drain();
  memset(p->inventory_items,0,sizeof(p->inventory_items)); memset(p->inventory_count,0,sizeof(p->inventory_count));
  memset(p->craft_items,0,sizeof(p->craft_items)); memset(p->craft_count,0,sizeof(p->craft_count));
  p->flags = 0; p->x = -2; p->y = 201; p->z = -2; p->hotbar = 0; p->health = 20; p->flagval_16 = 0; p->flagval_8 = 0;
  other->x = 10; other->y = 201; other->z = 10; other->health = 20;
}
static void block (int x, int y, int z, uint8_t b) { assert(!makeBlockChange((short)x,(uint8_t)y,(short)z,b)); drain(); }
static unsigned var (const uint8_t *b, size_t n, size_t *at) {
  unsigned v = 0;
  for (unsigned shift = 0; shift < 35; shift += 7) {
    assert(*at < n); unsigned c = b[(*at)++]; assert(shift < 28 || !(c&240u));
    v |= (c&127u)<<shift; if (!(c&128u)) return v;
  }
  assert(0); return 0;
}
static unsigned packet (int fd, uint8_t *b, size_t *n, size_t *at) {
  int length = readVarInt(fd); assert(length > 0 && length <= 128);
  assert(recv_all(fd,b,(size_t)length,0) == length); *n = (size_t)length; *at = 0;
  return var(b,*n,at);
}
static void wire_spawn (int fd, unsigned count, unsigned item) {
  uint8_t b[128]; size_t n,at;
  assert(packet(fd,b,&n,&at) == 1);
  assert(var(b,n,&at) == (uint32_t)ITEM_ENTITY_BASE);
  at += 16; assert(var(b,n,&at) == 69); assert(n-at == 34);
  assert(packet(fd,b,&n,&at) == 0x5c);
  assert(var(b,n,&at) == (uint32_t)ITEM_ENTITY_BASE);
  assert(b[at++] == 5 && b[at++] == 8 && b[at++] == 1);
  assert(b[at++] == 8 && b[at++] == 7);
  assert(var(b,n,&at) == count && var(b,n,&at) == item);
  assert(b[at++] == 0 && b[at++] == 0 && b[at++] == 255 && at == n);
}
static void put_var (uint8_t *b, size_t *at, uint32_t n) {
  do { b[(*at)++] = (uint8_t)((n&127u)|(n > 127 ? 128u : 0u)); n >>= 7; } while (n);
}
static void put_stack (uint8_t *b, size_t *at, uint16_t item, uint8_t count, bool hash) {
  b[(*at)++] = 1; put_var(b,at,item); put_var(b,at,count);
  b[(*at)++] = hash ? 1 : 0;
  if (hash) { b[(*at)++] = 0; for (unsigned i = 0; i < 4; i++) b[(*at)++] = 0x80; }
  b[(*at)++] = 0;
}
static int click (const uint8_t *b, size_t n) {
  assert(send(sockets[0],b,n,0) == (ssize_t)n);
  recv_count = 1; int result = cs_clickContainer(sockets[1],(int)n); drain(); return result;
}
static void mode (const char *name) {
  char text[32]; snprintf(text,sizeof(text),"gamemode %s",name);
  assert(commands_execute(p,text,strlen(text)) == COMMAND_OK); drain();
}
int main (void) {
  assert(mkdir(".tests/item-world",0700) == 0 || access(".tests/item-world",F_OK) == 0);
  assert(chdir(".tests/item-world") == 0); remove("world.bin");
  for (int i = 0; i < MAX_PLAYERS; i++) player_data[i].client_fd = -1;
  for (int i = 0; i < MAX_BLOCK_CHANGES; i++) block_changes[i].block = 255;
  block_changes_count = 0; assert(initSerializer() == 0); assert(signs_load(NULL) && doors_load(NULL));
  assert(socketpair(AF_UNIX,SOCK_STREAM,0,sockets) == 0);
  assert(socketpair(AF_UNIX,SOCK_STREAM,0,observer) == 0);
  p->client_fd = sockets[1]; other->client_fd = observer[1]; clean();
  assert(commands_configure("0123456789abcdef0123456789abcdef"));
  assert(commands_execute(p,"admin 0123456789abcdef0123456789abcdef",38) == COMMAND_OK); drain();
  for (int x = -4; x <= 0; x++) for (int z = -4; z <= 0; z++) block(x,200,z,B_stone);
  assert(items_at(ITEM_ENTITY_LIMIT) == NULL);
  assert(!items_spawn(0,1,0,201,0,0)); assert(!items_spawn(UINT16_MAX,1,0,201,0,0));
  assert(!items_spawn(I_dirt,0,0,201,0,0)); assert(!items_spawn(I_dirt,65,0,201,0,0));
  assert(!items_spawn(I_iron_pickaxe,2,0,201,0,0));
  assert(!items_spawn(I_dirt,1,-32769,0,0,0) && !items_spawn(I_dirt,1,32768,0,0,0));
  assert(!items_spawn(I_dirt,1,0,-1,0,0) && !items_spawn(I_dirt,1,0,256,0,0));
  assert(!items_spawn(I_dirt,1,0,1,32768,0) && !items_spawn(I_dirt,1,0,1,0,ITEM_LIFETIME_MS+1));
  assert(items_spawn(I_dirt,5,-2,201,-2,1500));
  wire_spawn(sockets[0],5,I_dirt); wire_spawn(observer[0],5,I_dirt);
  items_tick(1000000); drain(); assert(total() == 5 && p->inventory_count[0] == 0 && first()->y == 201);
  /* Two players compete: exactly one receives each item, with pickup/removal. */
  other->x = -2; other->z = -2;
  items_tick(1000000);
  assert(p->inventory_count[0] == 5 && other->inventory_count[0] == 0 && total() == 0);
  uint8_t data[128]; size_t n,at;
  assert(packet(observer[0],data,&n,&at) == 0x75);
  assert(var(data,n,&at) == (uint32_t)ITEM_ENTITY_BASE && var(data,n,&at) == (unsigned)sockets[1] && var(data,n,&at) == 5 && at == n);
  assert(packet(observer[0],data,&n,&at) == 0x46); assert(var(data,n,&at) == 1 && var(data,n,&at) == (uint32_t)ITEM_ENTITY_BASE && at == n); drain();
  clean();
  /* Partial capacity leaves the remainder in the world; armor is not storage. */
  for (unsigned i = 0; i < 36; i++) { p->inventory_items[i] = I_dirt; p->inventory_count[i] = 64; }
  p->inventory_count[7] = 62;
  assert(items_spawn(I_dirt,5,-2,201,-2,0)); items_tick(1000000); drain();
  assert(p->inventory_count[7] == 64 && total() == 3 && p->inventory_count[36] == 0);
  items_tick(1000000); drain(); assert(total() == 3);
  p->inventory_count[3] = 0; items_tick(1000000); drain(); assert(total() == 0 && p->inventory_count[3] == 3);
  clean();
  /* Gravity reaches a solid floor without tunneling, even after a long tick. */
  p->x = 20; assert(items_spawn(I_dirt,1,-2,210,-2,1500)); items_tick(4000000); drain(); assert(first()->y == 201);
  /* Merges preserve the older age and the longer pickup delay. */
  assert(items_spawn(I_dirt,3,-2,201,-2,2000)); drain(); assert(total() == 4 && first()->age_ms == 4000 && first()->delay_ms == 2000);
  items_tick(INT64_MAX); drain(); assert(total() == 0);
  block(-32768,0,32767,B_air);
  assert(items_spawn(I_dirt,1,-32768,0,32767,0)); items_tick(1000000); drain(); assert(total() == 0);
  clean(); block(-2,201,-2,B_lava); assert(items_spawn(I_dirt,1,-2,201,-2,0)); items_tick(100000); drain(); assert(!total()); block(-2,201,-2,B_air);
  /* Joining/leaving range updates tracking, and disconnect removes viewer bits. */
  p->x = 1000; other->x = 1000; assert(items_spawn(I_dirt,1,-2,201,-2,1500)); assert(first()->viewers == 0);
  p->x = -2; items_sync_player(p); wire_spawn(sockets[0],1,I_dirt); assert(first()->viewers == 1);
  p->x = 1000; items_sync_player(p); assert(packet(sockets[0],data,&n,&at) == 0x46);
  p->x = -2; items_sync_player(p); drain(); items_forget_player(p); assert(first()->viewers == 0);
  clean();
  /* Mining produces one entity; repeat/creative/distant requests cannot duplicate. */
  block(-2,201,-2,B_dirt); handlePlayerAction(p,2,-2,201,-2); drain(); assert(total() == 1 && !p->inventory_count[0]);
  handlePlayerAction(p,2,-2,201,-2); assert(total() == 1); items_tick(1000000); drain(); assert(p->inventory_count[0] == 1 && !total());
  block(-2,201,-2,B_dirt); p->x = 20; handlePlayerAction(p,2,-2,201,-2); assert(getBlockAt(-2,201,-2) == B_dirt);
  p->x = -2; mode("creative"); handlePlayerAction(p,0,-2,201,-2); drain(); assert(!total()); mode("survival");
  /* Q and Ctrl-Q drop authoritative held counts, including full-inventory players. */
  clean(); p->inventory_items[0] = I_dirt; p->inventory_count[0] = 5;
  handlePlayerAction(p,4,0,0,0); drain(); assert(total() == 1 && p->inventory_count[0] == 4);
  handlePlayerAction(p,3,0,0,0); drain(); assert(total() == 5 && !p->inventory_count[0] && !p->inventory_items[0]);
  items_tick(1000000); drain(); assert(total() == 5); items_tick(1000000); drain(); assert(!total() && p->inventory_count[0] == 5);
  /* Mob death uses the old Y, drops without a player killer, and cannot repeat. */
  clean(); mob_data[0] = (MobData){.type=25,.x=-2,.y=201,.z=-2,.data=4};
  hurtEntity(-2,-1,D_generic,4); drain(); assert(total() == 1 && first()->item == I_chicken && first()->y > 200);
  hurtEntity(-2,-1,D_generic,4); assert(total() == 1); hurtEntity(INT_MIN,-1,D_generic,1); interactEntity(INT_MIN,p->client_fd); drain();
  clean();
  /* Truncations at every byte cannot commit even the first changed slot. */
  const uint8_t drop[] = {0,0,0,36,0,4,1,0,36,0,0};
  p->inventory_items[0] = I_dirt; p->inventory_count[0] = 5;
  for (size_t len = 1; len < sizeof(drop); len++) { assert(click(drop,len) == 1); assert(!total() && p->inventory_count[0] == 5); }
  assert(click(drop,sizeof(drop)) == 0); assert(total() == 1 && p->inventory_count[0] == 4);
  uint8_t all[sizeof(drop)]; memcpy(all,drop,sizeof(all)); all[4] = 1;
  assert(click(all,sizeof(all)) == 0); assert(total() == 5 && p->inventory_count[0] == 0);
  clean(); p->flagval_16 = I_dirt; p->flagval_8 = 5;
  const uint8_t outside[] = {0,0,0xfc,0x19,1,0,0,0};
  assert(click(outside,sizeof(outside)) == 0); assert(total() == 1 && p->flagval_8 == 4 && p->flagval_16 == I_dirt);
  uint8_t bad[sizeof(drop)]; memcpy(bad,drop,sizeof(bad)); bad[8] = 255; assert(click(bad,sizeof(bad)) == 1);
  memcpy(bad,drop,sizeof(bad)); bad[6] = 65; assert(click(bad,sizeof(bad)) == 1);
  memcpy(bad,drop,sizeof(bad)); bad[0] = 255; assert(click(bad,sizeof(bad)) == 1);
  memcpy(bad,drop,sizeof(bad)); bad[9] = 2; assert(click(bad,sizeof(bad)) == 1);
  assert(cs_clickContainer(sockets[1],4097) == 1 && cs_clickContainer(sockets[1],0) == 1);
  /* Ordinary inventory transfers still parse hashes, cursor and craft slots. */
  clean(); p->inventory_items[0] = I_dirt; p->inventory_count[0] = 5;
  uint8_t transfer[96] = {0,0,0,36,0,0,1,0,36}; size_t used = 9;
  put_stack(transfer,&used,I_dirt,4,true); put_stack(transfer,&used,I_dirt,1,false);
  assert(click(transfer,used) == 0 && p->inventory_count[0] == 4 && p->flagval_8 == 1);
  assert(p->flagval_16 == I_dirt && !total());
  transfer[3] = 1; transfer[8] = 1; used = 9;
  put_stack(transfer,&used,I_dirt,1,false); transfer[used++] = 0;
  assert(click(transfer,used) == 0 && p->craft_items[0] == I_dirt && p->craft_count[0] == 1 && !p->flagval_8);
  /* Valid chest storage uses packed memcpy, never unaligned uint16_t access. */
  block(0,201,0,B_chest);
  uint8_t *storage = NULL;
  for (int i = 0; i < block_changes_count; i++) if (block_changes[i].block == B_chest) { storage = (uint8_t *)&block_changes[i+1]; break; }
  assert(storage); memcpy(p->craft_items,&storage,sizeof(storage)); p->flags = 0x80;
  transfer[0] = 2; transfer[3] = 0; transfer[8] = 0;
  assert(click(transfer,used) == 0);
  uint16_t stored_item; memcpy(&stored_item,storage,2); assert(stored_item == I_dirt && storage[2] == 1);
  const uint8_t chest_drop[] = {2,0,0,0,0,4,0,0};
  assert(click(chest_drop,sizeof(chest_drop)) == 0 && !storage[2] && total() == 1);
  clean();
  /* Hostile chest pointer and craft-grid access are rejected before dereference. */
  p->flags = 0x80; memset(p->craft_items,255,sizeof(p->craft_items));
  const uint8_t chest[] = {2,0,0,0,0,4,0,0}; assert(click(chest,sizeof(chest)) == 1);
  p->flags = 0;
  clean(); mode("spectator"); p->inventory_items[0] = I_dirt; p->inventory_count[0] = 5;
  assert(!items_drop_slot(p,0,true)); assert(click(drop,sizeof(drop)) == 0); assert(!total());
  assert(items_spawn(I_dirt,1,-2,201,-2,0)); items_tick(1000000); drain(); assert(total() == 1); mode("survival");
  clean();
  /* Fixed capacity rejects a drop without consuming inventory or a mined block. */
  for (int i = 0; i < ITEM_ENTITY_LIMIT; i++) assert(items_spawn(I_dirt,64,1000+i,201,1000,0));
  assert(total() == ITEM_ENTITY_LIMIT*64); p->inventory_items[0] = I_dirt; p->inventory_count[0] = 5;
  assert(!items_drop_slot(p,0,true)); drain(); assert(p->inventory_count[0] == 5);
  block(-2,201,-2,B_dirt); handlePlayerAction(p,2,-2,201,-2); drain(); assert(getBlockAt(-2,201,-2) == B_dirt);
  p->flagval_16 = I_dirt; p->flagval_8 = 5;
  assert(click(outside,sizeof(outside)) == 0 && p->flagval_8 == 5);
  items_tick(1000); drain();
  items_clear(); drain(); assert(!total());
  clean(); p->x = 20;
  assert(items_spawn(I_dirt,32,-2,204,-2,0) && items_spawn(I_dirt,32,-2,206,-2,0));
  items_tick(1000000); drain(); assert(total() == 64 && first()->count == 64);
  clean(); mode("creative");
  uint8_t creative_drop[16] = {255,255,1}; used = 3;
  put_var(creative_drop,&used,I_dirt); creative_drop[used++] = 0; creative_drop[used++] = 0;
  assert(send(sockets[0],creative_drop,used,0) == (ssize_t)used);
  assert(cs_creativeSlot(sockets[1],(int)used) == 0 && total() == 1); drain();
  clean(); mode("survival");
  assert(send(sockets[0],creative_drop,used,0) == (ssize_t)used);
  assert(cs_creativeSlot(sockets[1],(int)used) == 0 && !total()); drain();
  close(sockets[0]); close(sockets[1]); close(observer[0]); close(observer[1]); remove("world.bin"); assert(chdir("../..") == 0);
  puts("items: lifecycle, gravity, capacity, partial pickup, merging, visibility, mining, mob loot, wire packets and malformed inventory input passed");
  return 0;
}
