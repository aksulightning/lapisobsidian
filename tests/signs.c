#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
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
static void drain (void) { uint8_t data[4096]; while (recv(sockets[0],data,sizeof(data),MSG_DONTWAIT)>0) {} }
static void connect_player (void) {
  assert(socketpair(AF_UNIX,SOCK_STREAM,0,sockets) == 0); p->client_fd = sockets[1];
}
static void disconnect_player (void) { close(sockets[0]); close(sockets[1]); p->client_fd = -1; }
static void setup (void) {
  assert(signs_load(NULL));
  for (int i = 0; i < MAX_BLOCK_CHANGES; i++) block_changes[i].block = 255;
  block_changes_count = 0; memset(player_data,0,sizeof(player_data));
  for (int i = 0; i < MAX_PLAYERS; i++) player_data[i].client_fd = -1;
  p->health = 20; p->x = -2; p->y = 201; p->z = -2;
  assert(commands_configure("0123456789abcdef0123456789abcdef"));
  assert(initSerializer() == 0); connect_player();
  assert(makeBlockChange(-2,200,-2,B_stone) == 0); drain();
}
static void pos (uint8_t *out, int x, int y, int z) {
  uint64_t v = (((uint64_t)(int64_t)x&0x3ffffffu)<<38)|(((uint64_t)(int64_t)z&0x3ffffffu)<<12)|((uint64_t)y&4095u);
  for (unsigned i = 0; i < 8; i++) out[i] = (uint8_t)(v>>(56-i*8));
}
static int incoming (int (*handler)(int,int), uint8_t *data, size_t sent, int declared) {
  assert(send(sockets[0],data,sent,0) == (ssize_t)sent); shutdown(sockets[0],SHUT_WR);
  int result = handler(sockets[1],declared); drain(); disconnect_player(); connect_player(); return result;
}
static unsigned var (const uint8_t *d, size_t n, size_t *at) {
  unsigned v = 0;
  for (unsigned shift = 0; shift < 35; shift += 7) {
    assert(*at < n); unsigned b = d[(*at)++]; v |= (b&127u)<<shift; if (!(b&128u)) return v;
  }
  assert(0); return 0;
}
static size_t frame (uint8_t *data, size_t n) {
  int length = readVarInt(sockets[0]); assert(length > 0 && (size_t)length <= n);
  assert(recv_all(sockets[0],data,(size_t)length,0) == length); return (size_t)length;
}
static void named (const uint8_t *data, size_t n, size_t *at, unsigned type, const char *name) {
  size_t len = strlen(name); assert(n-*at >= 3+len && data[(*at)++] == type);
  assert(data[(*at)++] == 0 && data[(*at)++] == len); assert(memcmp(data+*at,name,len) == 0); *at += len;
}
static void nbt_string (const uint8_t *data, size_t n, size_t *at, const uint8_t *expected, size_t len) {
  assert(n-*at >= 2+len); unsigned a = data[(*at)++], b = data[(*at)++]; assert(a*256+b == len);
  assert(memcmp(data+*at,expected,len) == 0); *at += len;
}
static void wire_sign (void) {
  uint8_t data[2048]; signs_send_chunk(sockets[1],-1,-1);
  size_t n = frame(data,sizeof(data)), at = 0; assert(var(data,n,&at) == 8);
  uint8_t position[8]; pos(position,-2,201,-2); assert(memcmp(data+at,position,8) == 0); at += 8;
  assert(var(data,n,&at) == 4383 && at == n);
  n = frame(data,sizeof(data)); at = 0; assert(var(data,n,&at) == 6); assert(memcmp(data+at,position,8) == 0); at += 8;
  assert(var(data,n,&at) == 7 && data[at++] == 10);
  named(data,n,&at,8,"id"); nbt_string(data,n,&at,(const uint8_t *)"minecraft:sign",14);
  named(data,n,&at,1,"is_waxed"); assert(data[at++] == 0);
  for (unsigned side = 0; side < 2; side++) {
    named(data,n,&at,10,side ? "back_text" : "front_text");
    named(data,n,&at,8,"color"); nbt_string(data,n,&at,(const uint8_t *)"black",5);
    named(data,n,&at,1,"has_glowing_text"); assert(data[at++] == 0);
    named(data,n,&at,9,"messages"); assert(data[at++] == 8);
    const uint8_t four[4] = {0,0,0,4}; assert(memcmp(data+at,four,4) == 0); at += 4;
    const uint8_t emoji[] = {0xed,0xa0,0xbd,0xed,0xb8,0x80};
    for (unsigned line = 0; line < 4; line++) {
      const uint8_t *t = (const uint8_t *)""; size_t len = 0;
      if (!side && !line) { t = (const uint8_t *)"Hello"; len = 5; }
      if (!side && line == 1) { t = emoji; len = sizeof(emoji); }
      if (side && !line) { t = (const uint8_t *)"Back"; len = 4; }
      nbt_string(data,n,&at,t,len);
    }
    assert(data[at++] == 0);
  }
  assert(data[at++] == 0 && at == n);
}
int main (void) {
  assert(mkdir(".tests/sign-world",0700) == 0 || access(".tests/sign-world",F_OK) == 0);
  assert(chdir(".tests/sign-world") == 0); remove("world.bin"); remove("signs.bin");
  setup();
  char lines[4][SIGN_LINE_MAX+1] = {"Hello","\xf0\x9f\x98\x80","",""};
  assert(signs_coords_valid(-32768,0,32767)); assert(!signs_coords_valid(-32769,0,0)); assert(!signs_coords_valid(0,256,0));
  assert(!signs_place(p,-32769,200,0,1)); assert(!signs_place(p,-2,200,-2,0));
  assert(signs_load("signs.bin"));
  p->inventory_items[0] = I_oak_sign; p->inventory_count[0] = 2;
  handlePlayerUseItem(p,-2,200,-2,1); drain();
  assert(getBlockAt(-2,201,-2) == B_oak_sign && p->inventory_count[0] == 1);
  assert(signs_at(-2,201,-2) && signs_state(signs_at(-2,201,-2)) == 4383);
  assert(signs_edit(p,-2,201,-2,true,lines)); drain();
  assert(!signs_edit(p,-2,201,-2,true,lines)); /* one shot */
  assert(signs_open(p,-2,201,-2,false)); drain();
  assert(!signs_edit(p,-2,201,-2,true,lines));
  memset(lines,0,sizeof(lines)); strcpy(lines[0],"Back");
  assert(signs_edit(p,-2,201,-2,false,lines)); drain(); wire_sign();
  uint8_t tiny[3]; assert(signs_nbt(signs_at(-2,201,-2),tiny,sizeof(tiny)) == 0);
  /* World and text survive independent serializer restart. */
  disconnect_player(); assert(signs_load(NULL)); block_changes_count = 0;
  assert(initSerializer() == 0); p->client_fd = -1;
  assert(signs_load("signs.bin")); connect_player();
  assert(strcmp(signs_at(-2,201,-2)->lines[0][0],"Hello") == 0); wire_sign();
  assert(signs_open(p,-2,201,-2,true)); drain(); server_ticks += 61;
  assert(!signs_edit(p,-2,201,-2,true,lines));
  assert(signs_open(p,-2,201,-2,true)); drain(); signs_reset_player(p);
  assert(!signs_edit(p,-2,201,-2,true,lines));
  assert(signs_open(p,-2,201,-2,true)); drain(); p->x = 100;
  assert(!signs_edit(p,-2,201,-2,true,lines)); p->x = -2;
  PlayerData *q = &player_data[1]; q->client_fd = p->client_fd; q->health = 20; q->x = -2; q->y = 201; q->z = -2;
  assert(!signs_open(q,-2,201,-2,false)); q->client_fd = -1;
  assert(commands_execute(p,"admin 0123456789abcdef0123456789abcdef",38) == COMMAND_OK); drain();
  assert(commands_execute(p,"gamemode adventure",18) == COMMAND_OK); drain();
  assert(!signs_edit(p,-2,201,-2,true,lines) && !signs_open(p,-2,201,-2,true));
  assert(!signs_place(p,-2,200,-2,2));
  assert(commands_execute(p,"gamemode spectator",18) == COMMAND_OK); drain();
  assert(!signs_open(p,-2,201,-2,true) && !signs_place(p,-2,200,-2,2));
  assert(commands_execute(p,"gamemode survival",17) == COMMAND_OK); drain();
  /* Parse complete edit only, including every truncated prefix. */
  uint8_t update[32] = {0}; pos(update,-2,201,-2); update[8] = 1; update[9] = 2; update[10] = 'O'; update[11] = 'K';
  for (size_t n = 0; n < 15; n++) assert(incoming(cs_updateSign,update,n,15) == 1);
  assert(strcmp(signs_at(-2,201,-2)->lines[0][0],"Hello") == 0);
  assert(signs_open(p,-2,201,-2,true)); drain(); assert(incoming(cs_updateSign,update,15,15) == 0);
  assert(strcmp(signs_at(-2,201,-2)->lines[0][0],"OK") == 0);
  update[15] = 0; assert(incoming(cs_updateSign,update,16,16) == 1);
  update[8] = 2; assert(incoming(cs_updateSign,update,15,15) == 1); update[8] = 1;
  update[10] = 0xc0; update[11] = 0x80; assert(incoming(cs_updateSign,update,15,15) == 1);
  pos(update,32768,201,-2); assert(incoming(cs_updateSign,update,15,15) == 1);
  assert(cs_updateSign(sockets[1],SIGN_PACKET_MAX+1) == 1);
  pos(update,-2,201,-2); update[8] = 1;
  memset(update+9,0x80,5); update[14] = 0; assert(incoming(cs_updateSign,update,15,15) == 1);
  memset(update+9,0xff,4); update[13] = 0x0f; assert(incoming(cs_updateSign,update,15,15) == 1);
  update[9] = SIGN_LINE_MAX+1; assert(incoming(cs_updateSign,update,15,15) == 1);
  uint8_t invalid[][4] = {{0},{0xc0,0x80},{0xed,0xa0,0x80},{0xf4,0x90,0x80,0x80},{0xc2,0xa7}};
  const size_t lens[] = {1,2,3,4,2};
  for (unsigned i = 0; i < 5; i++) assert(!signs_text_valid(invalid[i],lens[i]));
  memset(lines[0],'a',SIGN_LINE_MAX); lines[0][SIGN_LINE_MAX] = 0;
  assert(!signs_text_valid((const uint8_t *)lines[0],SIGN_LINE_MAX));
  /* A failed disk commit must preserve previous text. */
  assert(signs_open(p,-2,201,-2,true)); drain();
  assert(mkdir("signs.bin.tmp",0700) == 0); memset(lines,0,sizeof(lines)); strcpy(lines[0],"Unsaved");
  assert(!signs_edit(p,-2,201,-2,true,lines)); drain(); assert(rmdir("signs.bin.tmp") == 0);
  assert(strcmp(signs_at(-2,201,-2)->lines[0][0],"OK") == 0);
  /* Removal clears text/editor; reusing coordinates starts blank. */
  assert(makeBlockChange(-2,201,-2,B_air) == 0); drain(); assert(!signs_at(-2,201,-2));
  assert(signs_load("signs.bin") && !signs_at(-2,201,-2));
  assert(signs_place(p,-2,200,-2,1)); drain(); assert(!signs_at(-2,201,-2)->lines[0][0][0]);
  assert(makeBlockChange(-2,200,-2,B_air) == 0); drain(); assert(!signs_at(-2,201,-2) && getBlockAt(-2,201,-2) == B_air);
  /* Wall orientation, support removal, invalid boundary placement. */
  for (uint8_t face = 2; face <= 5; face++) {
    assert(makeBlockChange(-2,200,-2,B_stone) == 0); drain();
    assert(signs_place(p,-2,200,-2,face)); drain();
    int x = -2+(face == 4 ? -1 : face == 5 ? 1 : 0), z = -2+(face == 2 ? -1 : face == 3 ? 1 : 0);
    assert(signs_state(signs_at(x,200,z)) == 4859+(face-2)*2);
    assert(makeBlockChange(-2,200,-2,B_air) == 0); drain(); assert(!signs_at(x,200,z));
  }
  /* Place packet malformed/truncated input must not consume items or place. */
  assert(makeBlockChange(-2,200,-2,B_stone) == 0); drain();
  p->inventory_items[0] = I_oak_sign; p->inventory_count[0] = 1;
  uint8_t place[25] = {0}; pos(place+1,-2,200,-2); place[9] = 1;
  for (size_t n = 0; n < sizeof(place); n++) assert(incoming(cs_useItemOn,place,n,sizeof(place)) == 1);
  assert(!signs_at(-2,201,-2) && p->inventory_count[0] == 1);
  assert(incoming(cs_useItemOn,place,sizeof(place),sizeof(place)) == 0);
  assert(signs_at(-2,201,-2) && p->inventory_count[0] == 0);
  uint8_t action[11] = {2}; pos(action+1,-2,201,-2); action[9] = 1;
  for (size_t n = 0; n < sizeof(action); n++) assert(incoming(cs_playerAction,action,n,sizeof(action)) == 1);
  assert(signs_at(-2,201,-2));
  assert(incoming(cs_playerAction,action,sizeof(action),sizeof(action)) == 0);
  assert(!signs_at(-2,201,-2) && p->inventory_items[0] == I_oak_sign && p->inventory_count[0] == 1);
  assert(signs_place(p,-2,200,-2,1)); drain();
  /* Reconcile text records whose world blocks disappeared (crash/old backup). */
  for (int i = 0; i < block_changes_count; i++) if (block_changes[i].block == B_oak_sign) block_changes[i].block = B_air;
  assert(signs_load("signs.bin") && !signs_at(-2,201,-2));
  assert(signs_load("signs.bin") && !signs_at(-2,201,-2));
  /* Craft the Beta-era one-sign recipe. */
  memset(p->craft_items,0,sizeof(p->craft_items)); uint8_t count; uint16_t item;
  getCraftingOutput(p,&count,&item); assert(count == 0 && item == 0);
  for (unsigned i = 0; i < 6; i++) p->craft_items[i] = I_oak_planks;
  p->craft_items[7] = I_stick; getCraftingOutput(p,&count,&item); assert(count == 1 && item == I_oak_sign);
  /* Capacity is fixed; coordinates cannot wrap at the world boundary. */
  assert(signs_load(NULL));
  for (int i = 0; i < SIGN_LIMIT; i++) {
    p->x = (int16_t)(1000+i); p->z = 0; p->y = 201;
    assert(makeBlockChange(p->x,200,0,B_stone) == 0); drain();
    assert(signs_place(p,p->x,200,0,1)); drain();
  }
  p->x = 1000+SIGN_LIMIT;
  assert(makeBlockChange(p->x,200,0,B_stone) == 0); drain();
  assert(!signs_place(p,p->x,200,0,1)); drain(); assert(!signs_at(p->x,201,0));
  assert(signs_load(NULL)); p->x = 32767;
  assert(makeBlockChange(32767,200,0,B_stone) == 0); drain();
  assert(!signs_place(p,32767,200,0,5));
  assert(!signs_place(p,32767,255,0,1));
  /* Old sign-only saves recover an editable blank record on interaction. */
  p->x = 1000; assert(signs_interact(p,1000,201,0)); drain();
  assert(signs_at(1000,201,0) && signs_at(1000,201,0)->lines[0][0][0] == 0);
  assert(signs_load("signs.bin"));
  disconnect_player();
  /* Malformed disk data is rejected, never silently overwritten. */
  assert(signs_load("signs.bin")); connect_player(); p->x = 1000;
  assert(signs_interact(p,1000,201,0)); drain(); disconnect_player();
  uint8_t saved[1024]; FILE *fixture = fopen("signs.bin","rb"); assert(fixture);
  size_t saved_size = fread(saved,1,sizeof(saved),fixture); fclose(fixture);
  assert(saved_size == 8+6+8*(SIGN_LINE_MAX+1));
  for (unsigned test = 0; test < 5; test++) {
    uint8_t corrupt[1024]; memcpy(corrupt,saved,saved_size);
    if (test == 0) corrupt[6] = 2; /* unknown version */
    if (test == 1) corrupt[13] = 20; /* invalid orientation */
    if (test == 2) corrupt[14] = 0xff; /* malformed UTF-8 */
    fixture = fopen("signs.bin","wb"); assert(fixture);
    assert(fwrite(corrupt,1,saved_size,fixture) == saved_size);
    if (test >= 3) {
      unsigned extra = test == 3 ? 1 : SIGN_LIMIT;
      for (unsigned i = 0; i < extra; i++) {
        /* Unique valid coordinates for the capacity case; duplicate for case 3. */
        if (test == 4) { corrupt[8] = 0; corrupt[9] = (uint8_t)i; }
        assert(fwrite(corrupt+8,1,saved_size-8,fixture) == saved_size-8);
      }
    }
    fclose(fixture); assert(!signs_load("signs.bin"));
  }
  fixture = fopen("signs.bin","wb"); assert(fixture); assert(fwrite(saved,1,saved_size,fixture) == saved_size); fclose(fixture);
  FILE *f = fopen("signs.bin","ab"); assert(f); fputc(1,f); fclose(f); assert(!signs_load("signs.bin"));
  remove("signs.bin"); remove("world.bin"); assert(chdir("../..") == 0);
  puts("signs: placement, sides, NBT, UTF-8, edits, permissions, persistence, removal, stale cleanup and malformed frames passed");
  return 0;
}
