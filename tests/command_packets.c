#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include "commands.h"
#include "packets.h"
#include "procedures.h"
#include "registries.h"
#include "tools.h"
#include "varnum.h"

static int sockets[2];
static const char token[] = "0123456789abcdef0123456789abcdef";
static void setup (void) {
  assert(socketpair(AF_UNIX,SOCK_STREAM,0,sockets) == 0);
  for (int i = 0; i < MAX_PLAYERS; i ++) player_data[i].client_fd = -1;
  memset(&player_data[0],0,sizeof(player_data[0])); player_data[0].client_fd = sockets[1];
  player_data[0].health = 20; strcpy(player_data[0].name,"Test");
  assert(commands_configure(token)); commands_reset_player(&player_data[0]);
}
static void cleanup (void) { close(sockets[0]); close(sockets[1]); }
static size_t frame (uint8_t *data, size_t capacity) {
  int n = readVarInt(sockets[0]); assert(n > 0 && (size_t)n <= capacity);
  assert(recv_all(sockets[0],data,(size_t)n,false) == n); return (size_t)n;
}
static void auth (void) {
  uint8_t data[512];
  assert(commands_execute(&player_data[0],"admin 0123456789abcdef0123456789abcdef",38) == COMMAND_OK);
  frame(data,sizeof(data));
}
static int command_packet (const uint8_t *data, size_t sent, int declared, bool signed_packet) {
  if (sent) assert(send(sockets[0],data,sent,0) == (ssize_t)sent);
  shutdown(sockets[0],SHUT_WR);
  return cs_chatCommand(sockets[1],declared,signed_packet);
}
static unsigned value (const uint8_t *data, size_t length, size_t *at) {
  unsigned result = 0;
  for (unsigned shift = 0; shift < 35; shift += 7) {
    assert(*at < length); unsigned b = data[(*at)++]; result |= (b & 127u)<<shift;
    if (!(b & 128u)) return result;
  }
  assert(0); return 0;
}
int main (void) {
  uint8_t signed_command[64] = {13,'t','i','m','e',' ','s','e','t',' ','1','0','0','0'};
  size_t size = 14+16+1+1+4;
  for (size_t n = 0; n < size; n ++) {
    setup(); auth(); world_time = 55;
    assert(command_packet(signed_command,n,(int)size,true) != 0 && world_time == 55); cleanup();
  }
  setup(); auth(); world_time = 55;
  assert(command_packet(signed_command,size,(int)size,true) == 0 && world_time == 1000); cleanup();
  setup(); auth(); world_time = 55; signed_command[30] = 1;
  assert(command_packet(signed_command,size,(int)size,true) != 0 && world_time == 55); cleanup(); signed_command[30] = 0;
  setup(); auth(); world_time = 55;
  assert(command_packet(signed_command,15,15,false) != 0 && world_time == 55); cleanup();
  setup(); world_time = 55;
  assert(command_packet(signed_command,14,14,false) == 0 && world_time == 55); cleanup();
  const uint8_t bad[][6] = {{0xff,0xff,0xff,0xff,0x7f,0},{0x80,0x80,0x80,0x80,0x80,0},{0x81,2,0,0,0,0},{0,0,0,0,0,0}};
  for (unsigned i = 0; i < 4; i ++) { setup(); assert(command_packet(bad[i],6,6,false) != 0);cleanup(); }
  setup(); assert(cs_chatCommand(sockets[1],1000000,false) != 0); cleanup();
  /* Decode the complete advertised command graph independently. */
  setup(); assert(sc_commands(sockets[1]) == 0); uint8_t wire[512]; size_t length = frame(wire,sizeof(wire)), at = 0;
  assert(value(wire,length,&at) == 0x10); assert(value(wire,length,&at) == 19);
  const char *names[] = {"help","seed","worldinfo","spawn","tp","time","gamemode","admin","spawnmob"};
  for (unsigned i = 0; i < 19; i ++) {
    unsigned flags = value(wire,length,&at), children = value(wire,length,&at);
    assert(flags == (i == 0 ? 0u : i < 10 ? 5u : 6u)); assert(children == (i == 0 ? 9u : i < 10 ? 1u : 0u));
    for (unsigned j = 0; j < children; j ++) assert(value(wire,length,&at) == (i == 0 ? j+1 : i+9));
    if (i) {
      const char *name = i < 10 ? names[i-1] : "arguments";
      unsigned n = value(wire,length,&at); assert(n == strlen(name) && n <= length-at);
      assert(!memcmp(wire+at,name,n)); at += n;
      if (i >= 10) { assert(value(wire,length,&at) == 5); assert(value(wire,length,&at) == 2); }
    }
  }
  assert(value(wire,length,&at) == 0 && at == length); cleanup();
  setup(); auth();
  assert(commands_execute(&player_data[0],"gamemode creative",17) == COMMAND_OK);
  length = frame(wire,sizeof(wire)); assert(length == 6 && wire[0] == 0x22 && wire[1] == 3 && wire[2] == 0x3f && wire[3] == 0x80);
  length = frame(wire,sizeof(wire)); assert(length == 10 && wire[0] == 0x39 && wire[1] == 13);
  length = frame(wire,sizeof(wire)); assert(length == 20 && wire[0] == 0x3f && wire[1] == 4 && wire[19] == 1);
  frame(wire,sizeof(wire));
  hurtEntity(sockets[1],-1,0,20);
  assert(player_data[0].health == 20);
  /* A plain stone stack in creative hotbar slot 36. */
  uint8_t creative[] = {0,36,64,I_stone,0,0};
  assert(send(sockets[0],creative,sizeof(creative),0) == sizeof(creative));
  assert(cs_creativeSlot(sockets[1],sizeof(creative)) == 0);
  assert(player_data[0].inventory_items[0] == I_stone && player_data[0].inventory_count[0] == 64); cleanup();
  setup(); assert(send(sockets[0],creative,sizeof(creative),0) == sizeof(creative));
  assert(cs_creativeSlot(sockets[1],sizeof(creative)) == 0 && player_data[0].inventory_count[0] == 0); cleanup();
  for (size_t n = 0; n < sizeof(creative); n ++) {
    setup(); if (n) assert(send(sockets[0],creative,n,0) == (ssize_t)n); shutdown(sockets[0],SHUT_WR);
    assert(cs_creativeSlot(sockets[1],sizeof(creative)) != 0); cleanup();
  }
  uint8_t invalid_item[] = {0,36,1,0x88,0x0b,0,0}; /* 1416, beyond registry */
  setup(); assert(send(sockets[0],invalid_item,sizeof(invalid_item),0) == sizeof(invalid_item));
  assert(cs_creativeSlot(sockets[1],sizeof(invalid_item)) != 0); cleanup();
  setup(); auth();
  assert(commands_execute(&player_data[0],"gamemode adventure",18) == COMMAND_OK);
  for (unsigned i = 0; i < 4; i ++) frame(wire,sizeof(wire));
  int edits = block_changes_count;
  handlePlayerAction(&player_data[0],0,8,64,8); handlePlayerAction(&player_data[0],2,8,64,8);
  assert(block_changes_count == edits);
  assert(commands_execute(&player_data[0],"gamemode spectator",18) == COMMAND_OK);
  for (unsigned i = 0; i < 4; i ++) frame(wire,sizeof(wire));
  handlePlayerUseItem(&player_data[0],8,64,8,1);
  hurtEntity(sockets[1],sockets[1],0,20);
  assert(block_changes_count == edits && player_data[0].health == 20 && !(player_data[0].flags & 1)); cleanup();
  /* Chat must be fully received and valid UTF-8 before it is forwarded. */
  uint8_t chat[26] = {3,'h','i','!'};
  for (size_t n = 0; n < sizeof(chat); n ++) {
    setup(); if (n) assert(send(sockets[0],chat,n,0) == (ssize_t)n); shutdown(sockets[0],SHUT_WR);
    assert(cs_chat(sockets[1],sizeof(chat)) != 0); cleanup();
  }
  setup(); assert(send(sockets[0],chat,sizeof(chat),0) == sizeof(chat));
  assert(cs_chat(sockets[1],sizeof(chat)) == 0); length = frame(wire,sizeof(wire)); assert(wire[0] == 0x72 && length > 5); cleanup();
  chat[1] = 0xc0; chat[2] = 0x80;
  setup(); assert(send(sockets[0],chat,sizeof(chat),0) == sizeof(chat)); assert(cs_chat(sockets[1],sizeof(chat)) != 0);cleanup();
  setup(); char emoji[] = "\xf0\x9f\x98\x80";
  assert(sc_systemChat(sockets[1],emoji,4) == 0);
  length = frame(wire,sizeof(wire));
  const uint8_t emoji_packet[] = {0x72,8,0,6,0xed,0xa0,0xbd,0xed,0xb8,0x80,0};
  assert(length == sizeof(emoji_packet) && !memcmp(wire,emoji_packet,length)); cleanup();
  puts("command packets: truncated/signed/oversized input, command graph, mode updates, creative permission and UTF-8/NBT chat passed");
  return 0;
}
