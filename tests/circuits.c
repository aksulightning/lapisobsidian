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
#include "items.h"
#include "musicbox.h"
#include "notes.h"
#include "packets.h"
#include "procedures.h"
#include "registries.h"
#include "serialize.h"
#include "worldgen.h"

static int sockets[2];
static PlayerData *p = &player_data[0];
static unsigned sounds;
static uint8_t wire[65536];
static size_t used;
static void drain (void) {
  used = 0;
  for (;;) { ssize_t n = recv(sockets[0],wire+used,sizeof(wire)-used,MSG_DONTWAIT); if (n <= 0) break; used += (size_t)n; assert(used < sizeof(wire)); }
  size_t at = 0;
  while (at < used) {
    unsigned len = 0, shift = 0; uint8_t b;
    do { assert(at < used && shift < 35); b = wire[at++]; len |= (unsigned)(b&127u)<<shift; shift += 7; } while (b&128u);
    assert(len && len <= used-at);
    if (wire[at] == 0x6e) sounds++;
    at += len;
  }
}
static void block (int x, int y, int z, uint8_t b) { assert(!makeBlockChange((short)x,(uint8_t)y,(short)z,b)); drain(); }
static void clean (void) {
  assert(circuits_load(NULL)); assert(doors_load(NULL)); items_clear(); musicbox_shutdown(); musicbox_reset_player(p);
  for (int i = 0; i < MAX_BLOCK_CHANGES; i++) block_changes[i].block = 255;
  block_changes_count = 0; p->flags = 0; p->health = 20; p->y = 201; p->z = 0; sounds = 0;
}
static void place (int x, int z, uint16_t held) {
  p->x = (short)(x+2); p->z = (short)z; p->y = 201;
  block(x,200,z,B_stone); assert(circuits_place(p,x,200,z,1,held)); drain();
}
static void tick (void) { circuits_tick(); drain(); }
static CommandResult command (const char *s) { CommandResult r = commands_execute(p,s,strlen(s)); drain(); return r; }
static const uint8_t midi[] = {
  'M','T','h','d',0,0,0,6,0,0,0,1,0,96,'M','T','r','k',0,0,0,19,
  0,0x90,60,100,96,0x80,60,0,0,0x90,64,80,96,64,0,0,255,47,0
};
static void song_file (const char *path) { FILE *f = fopen(path,"wb"); assert(f); assert(fwrite(midi,1,sizeof(midi),f) == sizeof(midi)); assert(!fclose(f)); }
int main (void) {
  assert(mkdir(".tests/circuit-world",0700) == 0 || access(".tests/circuit-world",F_OK) == 0);
  assert(!chdir(".tests/circuit-world")); remove("world.bin"); remove("circuits.bin"); remove("doors.bin");
  for (int i = 0; i < MAX_BLOCK_CHANGES; i++) block_changes[i].block = 255;
  for (int i = 0; i < MAX_PLAYERS; i++) player_data[i].client_fd = -1;
  assert(!initSerializer()); assert(!socketpair(AF_UNIX,SOCK_STREAM,0,sockets)); p->client_fd = sockets[1];
  assert(commands_configure("0123456789abcdef0123456789abcdef")); commands_reset_player(p); clean();
  assert(isPassableBlock(B_redstone_torch) && isColumnBlock(B_redstone_torch));
  assert(getMiningResult(I_iron_pickaxe,B_redstone_ore) == I_redstone && !getMiningResult(I_wooden_pickaxe,B_redstone_ore));
  place(0,0,I_redstone_torch);
  for (int x = 1; x <= 17; x++) place(x,0,I_redstone);
  tick();
  for (int x = 1; x <= 17; x++) assert(circuits_power(x,201,0) == (x <= 15 ? 16-x : 0));
  uint16_t state; assert(circuits_state_at(2,201,0,B_redstone_torch,&state));
  assert(state == 3895); /* side E/W, none N/S, power 14 */
  assert(circuits_drop(1,201,0,0) == I_redstone);
  block(0,201,0,B_air); tick();
  for (int x = 1; x <= 17; x++) assert(!circuits_power(x,201,0));
  /* A removed support drops dust once, including before the generic mining loop. */
  block(1,200,0,B_air); assert(getBlockAt(1,201,0) == B_air);
  unsigned drops = 0; for (unsigned i = 0; i < ITEM_ENTITY_LIMIT; i++) if (items_at(i)->count && items_at(i)->item == I_redstone) drops++;
  assert(drops == 1);
  clean();
  place(0,0,I_redstone_torch); p->x = 3; p->y = 200;
  block(1,199,0,B_stone); assert(circuits_place(p,1,199,0,1,I_lever)); drain(); tick();
  assert(circuits_power(0,201,0) == 15);
  assert(circuits_interact(p,1,200,0)); drain(); tick(); tick();
  assert(circuits_power(0,201,0) == 0); assert(circuits_interact(p,1,200,0)); drain(); tick(); tick(); assert(circuits_power(0,201,0) == 15);
  clean(); place(0,0,I_lever); place(1,0,I_note_block); p->x = 2;
  assert(circuits_interact(p,1,201,0)); drain(); assert(sounds == 1);
  assert(circuits_state_at(1,201,0,B_note_block,&state) && state == 634); /* basedrum, note 1, unpowered */
  assert(circuits_interact(p,0,201,0)); drain(); tick(); assert(sounds == 2); tick(); assert(sounds == 2);
  block(1,202,0,B_stone); circuits_strike(1,201,0); drain(); assert(sounds == 2); block(1,202,0,B_air);
  block(1,200,0,B_oak_planks); assert(notes_instrument(1,201,0) == NOTE_BASS);
  block(1,200,0,B_sand); assert(notes_instrument(1,201,0) == NOTE_SNARE);
  block(1,200,0,B_glass); assert(notes_instrument(1,201,0) == NOTE_HAT);
  assert(circuits_load("circuits.bin")); /* Adopt legacy carriers without metadata. */
  assert(circuits_interact(p,0,201,0)); drain(); assert(circuits_interact(p,1,201,0)); drain();
  assert(circuits_load("circuits.bin")); tick(); assert(circuits_power(0,201,0) == 15);
  assert(circuits_state_at(1,201,0,B_note_block,&state) && state == 733); /* hat, note 1, powered */
  assert(mkdir("circuits.bin.tmp",0700) == 0); assert(!circuits_interact(p,0,201,0)); assert(!rmdir("circuits.bin.tmp")); tick(); assert(circuits_power(0,201,0) == 15);
  block(1,201,0,B_air); assert(circuits_load("circuits.bin")); assert(!circuits_state_at(1,201,0,B_note_block,&state));
  FILE *f = fopen("circuits.bin","ab"); assert(f); assert(fputc(1,f) != EOF); fclose(f); assert(!circuits_load("circuits.bin")); remove("circuits.bin");
  clean(); assert(doors_load("doors.bin")); place(0,0,I_lever); p->x = 2; block(1,200,0,B_stone); assert(doors_place(p,1,200,0,1)); drain();
  assert(circuits_interact(p,0,201,0)); drain(); tick(); assert(doors_at(1,201,0)->open);
  assert(doors_save()); f = fopen("doors.bin","rb"); assert(f); assert(!fseek(f,14,SEEK_SET)); assert(fgetc(f) == 0); fclose(f);
  assert(circuits_interact(p,0,201,0)); drain(); tick(); assert(!doors_at(1,201,0)->open);
  assert(!circuits_place(p,0,255,0,1,I_redstone)); assert(!circuits_place(p,0,200,0,4,I_redstone_torch));
  /* Standard recipes, including both grids' shifted two-item recipes. */
  memset(p->craft_items,0,sizeof(p->craft_items)); p->craft_items[1] = I_redstone; p->craft_items[4] = I_stick;
  uint8_t count; uint16_t result; getCraftingOutput(p,&count,&result); assert(count == 1 && result == I_redstone_torch);
  p->craft_items[1] = I_stick; p->craft_items[4] = I_cobblestone; getCraftingOutput(p,&count,&result); assert(result == I_lever);
  for (unsigned i = 0; i < 9; i++) p->craft_items[i] = I_oak_planks;
  p->craft_items[4] = I_redstone; getCraftingOutput(p,&count,&result); assert(result == I_note_block);
  p->craft_items[4] = I_diamond; getCraftingOutput(p,&count,&result); assert(result == I_jukebox);
  clean();
  for (unsigned i = 0; i < CIRCUIT_LIMIT; i++) place(100+(int)i,0,I_redstone);
  p->x = 100+CIRCUIT_LIMIT; block(p->x,200,0,B_stone); assert(!circuits_place(p,p->x,200,0,1,I_redstone));
  clean(); assert(mkdir("songs",0700) == 0 || access("songs",F_OK) == 0); song_file("songs/test.mid");
  remove("songs/zlink.mid"); assert(!symlink("test.mid","songs/zlink.mid"));
  assert(musicbox_init("songs")); p->x = 2; block(0,201,0,B_jukebox);
  assert(command("music 1") == COMMAND_DENIED); assert(command("music reload") == COMMAND_DENIED);
  musicbox_menu(p,0,201,0); drain(); assert(command("music 2") == COMMAND_USAGE); assert(command("music ../test.mid") == COMMAND_USAGE);
  assert(command("music 1") == COMMAND_OK); musicbox_tick(20000); drain(); assert(sounds == 1);
  musicbox_tick(480000); drain(); assert(sounds == 2);
  assert(command("music stop") == COMMAND_OK); musicbox_tick(1000000); drain(); assert(sounds == 2);
  p->x = 100; assert(command("music 1") == COMMAND_DENIED); p->x = 2;
  assert(command("music 1") == COMMAND_OK); block(0,201,0,B_air); musicbox_tick(20000); drain(); assert(sounds == 2);
  assert(command("music stop") == COMMAND_DENIED);
  assert(command("admin 0123456789abcdef0123456789abcdef") == COMMAND_OK);
  assert(command("music reload") == COMMAND_OK);
  /* Longer rests let two boxes run concurrently while a third is rejected. */
  uint8_t longer[sizeof(midi)+1]; memcpy(longer,midi,sizeof(midi)-4);
  longer[21] = 20; const uint8_t ending[5] = {0x83,0x60,255,47,0}; memcpy(longer+sizeof(midi)-4,ending,5);
  f = fopen("songs/test.mid","wb"); assert(f); assert(fwrite(longer,1,sizeof(longer),f) == sizeof(longer)); fclose(f);
  assert(musicbox_init("songs"));
  for (int x = 0; x <= 4; x += 2) {
    block(x,201,0,B_jukebox); musicbox_menu(p,x,201,0); drain();
    assert(command("music 1") == (x == 4 ? COMMAND_DENIED : COMMAND_OK));
    if (x != 4) musicbox_tick(1000000);
    drain();
  }
  assert(command("music stop") == COMMAND_OK); /* Selected idle third box does not stop others. */
  assert(command("music reload") == COMMAND_OK);
  block(0,201,0,B_jukebox); musicbox_menu(p,0,201,0); drain();
  f = fopen("songs/test.mid","wb"); assert(f); fputs("not MIDI",f); fclose(f);
  assert(command("music 1") == COMMAND_DENIED);
  musicbox_tick(61000000); assert(command("music 1") == COMMAND_DENIED);
  musicbox_shutdown(); clean(); close(sockets[0]); close(sockets[1]); remove("songs/test.mid"); remove("songs/zlink.mid"); rmdir("songs"); remove("circuits.bin"); remove("doors.bin"); remove("world.bin"); assert(!chdir("../.."));
  puts("circuits/musicbox: power decay/inversion, dust drops, notes/instruments/edges, doors, persistence/failure, recipes, limits, menu permissions, playback and removal passed");
  return 0;
}
