#include <assert.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include "commands.h"
#include "crafting.h"
#include "globals.h"
#include "inventory.h"
#include "packet_input.h"
#include "packets.h"
#include "procedures.h"
#include "registries.h"
#include "tools.h"
#include "varnum.h"

static int sockets[2];
static PlayerData *p = &player_data[0];
static void drain (void) { uint8_t bytes[8192]; while (recv(sockets[0],bytes,sizeof(bytes),MSG_DONTWAIT) > 0) {} }
static void clean (void) {
  memset(p,0,sizeof(*p)); p->client_fd = sockets[1]; p->health = 20;
  commands_reset_player(p); inventory_reset(p); drain();
}
static int click (uint8_t window, uint16_t slot, uint8_t button, uint8_t mode, bool forged) {
  uint8_t b[32] = {window,0,(uint8_t)(slot>>8),(uint8_t)slot,button,mode,0,0}; size_t n = 8;
  if (forged) {
    b[6] = 1; b[7] = 0; b[8] = 36; b[9] = 1;
    b[10] = (uint8_t)((I_diamond&127)|128); b[11] = (uint8_t)(I_diamond>>7);
    b[12] = 64; b[13] = b[14] = b[15] = 0; n = 16;
  }
  assert(send(sockets[0],b,n,0) == (ssize_t)n); recv_count = 1;
  int r = cs_clickContainer(sockets[1],(int)n); drain(); return r;
}
static unsigned total (uint16_t item) {
  unsigned n = p->flagval_16 == item ? p->flagval_8 : 0;
  for (unsigned i = 0; i < 41; i++) if (p->inventory_items[i] == item) n += p->inventory_count[i];
  for (unsigned i = 0; i < 9; i++) if (p->craft_items[i] == item) n += p->craft_count[i];
  return n;
}
static int held (unsigned value) {
  uint8_t bytes[2] = {(uint8_t)(value>>8),(uint8_t)value};
  assert(send(sockets[0],bytes,2,0) == 2); return cs_setHeldItem(sockets[1]);
}
static void pair (int fd[2]) { assert(!socketpair(AF_UNIX,SOCK_STREAM,0,fd)); assert(!fcntl(fd[1],F_SETFL,O_NONBLOCK)); }
int main (void) {
  for (unsigned i = 0; i < MAX_PLAYERS; i++) player_data[i].client_fd = -1;
  assert(!socketpair(AF_UNIX,SOCK_STREAM,0,sockets)); assert(commands_configure(NULL)); clean();
  assert(!held(8) && p->hotbar == 8);
  const unsigned invalid[] = {9,40,255,256,257,512,65535};
  for (unsigned i = 0; i < sizeof(invalid)/sizeof(invalid[0]); i++) assert(held(invalid[i]) && p->hotbar == 8);
  /* An empty inventory cannot gain either predicted slot or cursor items. */
  assert(!click(0,36,0,0,true) && !total(I_diamond));
  p->inventory_items[0] = I_dirt; p->inventory_count[0] = 5;
  assert(!click(0,36,0,0,true) && p->flagval_8 == 5 && !p->inventory_count[0] && !total(I_diamond));
  assert(!click(0,37,1,0,false) && p->inventory_count[1] == 1 && p->flagval_8 == 4);
  assert(!click(0,37,0,0,false) && p->inventory_count[1] == 5 && !p->flagval_8);
  assert(!click(0,37,0,1,false) && p->inventory_count[9] == 5 && total(I_dirt) == 5);
  assert(!click(0,9,2,2,false) && p->inventory_count[2] == 5 && !p->inventory_count[9]);
  assert(!click(0,38,1,0,false) && p->flagval_8 == 3 && p->inventory_count[2] == 2);
  assert(!click(0,38,0,6,false) && p->flagval_8 == 5 && !p->inventory_count[2]);
  assert(!click(0,64537,0,5,false)); /* left drag */
  assert(!click(0,36,1,5,false)); assert(!click(0,37,1,5,false)); assert(!click(0,64537,2,5,false));
  assert(p->inventory_count[0] == 2 && p->inventory_count[1] == 2 && p->flagval_8 == 1 && total(I_dirt) == 5);
  assert(!click(0,5,0,0,false) && !p->inventory_count[39] && total(I_dirt) == 5); /* armor rejects dirt */
  assert(click(12,0,0,0,false) == 1); /* unopened window */
  clean(); p->craft_items[0] = I_oak_log; p->craft_count[0] = 2;
  assert(!click(0,0,0,0,true)); assert(p->flagval_16 == I_oak_planks && p->flagval_8 == 4 && p->craft_count[0] == 1);
  assert(!click(0,0,0,1,true)); assert(!p->craft_count[0] && total(I_oak_planks) == 8 && !total(I_diamond));
  assert(!click(0,0,0,0,true) && total(I_oak_planks) == 8);
  clean(); assert(inventory_open(p,14)); drain();
  p->craft_items[0] = I_sand; p->craft_count[0] = 64; p->craft_items[1] = I_coal; p->craft_count[1] = 64;
  p->craft_items[2] = I_glass; p->craft_count[2] = 60; getSmeltingOutput(p);
  assert(p->craft_count[2] == 64 && p->craft_count[0] == 60 && p->craft_count[1] == 63);
  p->flagval_16 = I_diamond; p->flagval_8 = 1;
  assert(!click(14,2,0,0,false) && p->craft_items[2] == I_glass && p->flagval_16 == I_diamond);
  uint8_t fake_close = 12; assert(send(sockets[0],&fake_close,1,0) == 1);
  assert(cs_closeContainer(sockets[1]) == 1 && p->craft_count[2] == 64);
  clean(); assert(inventory_open(p,2)); drain();
  memset(p->craft_items,255,sizeof(p->craft_items)); memset(p->craft_count,64,sizeof(p->craft_count)); p->flags = 0x80;
  assert(click(2,0,0,0,false) == 1);
  uint8_t close_chest = 2; assert(send(sockets[0],&close_chest,1,0) == 1);
  assert(!cs_closeContainer(sockets[1])); drain();
  for (unsigned i = 0; i < 41; i++) assert(!p->inventory_count[i]);
  clean(); p->craft_items[0] = I_dirt; p->craft_count[0] = 5;
  assert(inventory_close(p) && total(I_dirt) == 5 && !p->craft_count[0]); drain();
  p->flagval_16 = I_dirt; p->flagval_8 = 3;
  handlePlayerDisconnect(sockets[1]);
  assert(p->client_fd == -1 && p->inventory_count[0] == 8 && !p->flagval_8);
  close(sockets[0]); close(sockets[1]);

  /* Incomplete peers never block a ready peer; timeout is absolute, not refreshed
   * by another byte. All checks use deterministic clocks, no timeout sleeps. */
  int slow[2], fast[2]; pair(slow); pair(fast); PacketInput a,b;
  packet_input_reset(&a,slow[1]); packet_input_reset(&b,fast[1]);
  uint8_t prefix = 0x80; assert(send(slow[0],&prefix,1,0) == 1);
  assert(!packet_input_poll(&a,100));
  const uint8_t good[] = {3,0x34,0,8}; assert(send(fast[0],good,sizeof(good),0) == sizeof(good));
  assert(packet_input_poll(&b,101) == 1); p->client_fd = fast[1]; p->hotbar = 0;
  assert(readVarInt(fast[1]) == 0x34 && !cs_setHeldItem(fast[1]) && p->hotbar == 8 && packet_input_end());
  prefix = 1; assert(send(slow[0],&prefix,1,0) == 1); assert(!packet_input_poll(&a,200));
  assert(packet_input_poll(&a,100+PACKET_INPUT_TIMEOUT_US) == -1);
  close(slow[0]); close(slow[1]);
  /* A truncated body cannot steal bytes from the next frame. */
  const uint8_t joined[] = {2,0x34,0,3,0x34,0,7}; assert(send(fast[0],joined,sizeof(joined),0) == sizeof(joined));
  assert(packet_input_poll(&b,300) == 1 && readVarInt(fast[1]) == 0x34);
  assert(cs_setHeldItem(fast[1]) == 1 && p->hotbar == 8 && !packet_input_end());
  assert(packet_input_poll(&b,301) == 1 && readVarInt(fast[1]) == 0x34);
  assert(!cs_setHeldItem(fast[1]) && p->hotbar == 7 && packet_input_end());
  const uint8_t large[] = {0x81,0x40}; assert(send(fast[0],large,sizeof(large),0) == sizeof(large));
  assert(packet_input_poll(&b,400) == -1); close(fast[0]); close(fast[1]);
  pair(fast); packet_input_reset(&b,fast[1]);
  const uint8_t broken[] = {0x80,0x80,0x80}; assert(send(fast[0],broken,sizeof(broken),0) == sizeof(broken));
  assert(packet_input_poll(&b,500) == -1); close(fast[0]); close(fast[1]);
  puts("security: no forged inventory/cursor items, authoritative transfers/crafting, window locks, smelt cap, hotbar bounds, frame isolation, fair polling and absolute deadlines passed");
  return 0;
}
