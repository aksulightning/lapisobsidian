#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#else
#include <unistd.h>
#endif
#include "plates.h"
#include "optional_features.h"
#include "plate_contexts.h"
#include "server_config.h"
#include "beta173_rng.h"
#include "world_metadata.h"
#include "serialize.h"
#include "inventory.h"
#include "doors.h"
#include "signs.h"
#include "circuits.h"
#include "farming.h"
#include "fluids.h"
#include "items.h"
#include "mobs.h"
#include "musicbox.h"
#include "packets.h"
#include "procedures.h"
#include "registries.h"
#include "tools.h"
#include "worldgen.h"
#include "world_border.h"

typedef struct {
  char name[PLATE_NAME_MAX+1], dimension[48], path[80];
  uint64_t seed;
  uint32_t generation;
  WorldState *world;
  void *contexts[9];
} Plate;
static Plate plates[PLATE_LIMIT];
static uint32_t generation;
static struct { char remove[PLATE_NAME_MAX+1]; int64_t expires, travelled; uint32_t generation; bool arriving; } sessions[MAX_PLAYERS];
static const struct { size_t (*size)(void); void (*select)(void *); } modules[9] = {
  {signs_context_size,signs_context_select},{doors_context_size,doors_context_select},
  {circuits_context_size,circuits_context_select},{farming_context_size,farming_context_select},
  {fluids_context_size,fluids_context_select},{items_context_size,items_context_select},
  {mobs_context_size,mobs_context_select},{musicbox_context_size,musicbox_context_select},
  {worldgen_context_size,worldgen_context_select}
};
static int slot (const PlayerData *p) { for (int i = 0; i < MAX_PLAYERS; i++) if (p == &player_data[i]) return i; return -1; }
static bool valid_name (const char *s) {
  if (!s || !*s || strlen(s) > PLATE_NAME_MAX) return false;
  for (const char *p = s; *p; p++) if (!((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '_' || *p == '-')) return false;
  return strcmp(s,"confirm") && strcmp(s,"go") && strcmp(s,"list");
}
const char *plates_type_name (PlateType t) {
  static const char *const names[] = {"betanium","hub","flatworld","skybox","volcanic"};
  return (unsigned)t <= PLATE_VOLCANIC ? names[t] : "invalid";
}
static bool type_parse (const char *name, PlateType *out) {
  for (unsigned i = 0; i <= PLATE_VOLCANIC; i++) if (!strcmp(name,plates_type_name((PlateType)i))) { *out = (PlateType)i; return true; }
  return false;
}
int plates_find (const char *name) {
  if (name) for (unsigned i = 0; i < PLATE_LIMIT; i++) if (plates[i].name[0] && !strcmp(name,plates[i].name)) return (int)i;
  return -1;
}
const char *plates_name (unsigned id) { return id < PLATE_LIMIT && plates[id].name[0] ? plates[id].name : NULL; }
const char *plates_dimension (unsigned id) { return plates_enabled && plates_name(id) ? plates[id].dimension : "overworld"; }
unsigned plates_dimension_count (void) {
  if (!plates_enabled) return 1;
  unsigned n = 0; for (unsigned i = 0; i < PLATE_LIMIT; i++) n += plates[i].name[0] != 0;
  return n;
}
const char *plates_dimension_at (unsigned n) {
  if (plates_enabled) for (unsigned i = 0; i < PLATE_LIMIT; i++) if (plates[i].name[0]) { if (!n--) return plates[i].dimension; }
  return "overworld";
}
const char *plates_world_path (void) { return plates[plate_current].path; }
static void describe (unsigned id, const char *name, uint64_t seed, PlateType type) {
  snprintf(plates[id].name,sizeof(plates[id].name),"%s",name);
  snprintf(plates[id].dimension,sizeof(plates[id].dimension),"lapis_obsidian:%s",name);
  snprintf(plates[id].path,sizeof(plates[id].path),"plates/%s/world.bin",name);
  plates[id].generation = ++generation;
  plates[id].seed = seed; plate_types[id] = type;
}
/* Do not follow symlinks/reparse points into other worlds or arbitrary files. */
static bool plain_path (const char *path, bool directory, bool missing) {
#ifdef _WIN32
  DWORD a = GetFileAttributesA(path);
  if (a == INVALID_FILE_ATTRIBUTES) return missing && GetLastError() == ERROR_FILE_NOT_FOUND;
  return !(a&FILE_ATTRIBUTE_REPARSE_POINT) && !!(a&FILE_ATTRIBUTE_DIRECTORY) == directory;
#else
  struct stat st;
  if (lstat(path,&st)) return missing && errno == ENOENT;
  return directory ? S_ISDIR(st.st_mode) : S_ISREG(st.st_mode);
#endif
}
static bool directory_create (const char *path) {
#ifdef _WIN32
  return _mkdir(path) == 0;
#else
  return mkdir(path,0700) == 0;
#endif
}
static bool catalog_save (void) {
  if (!plain_path("plates/index.txt.tmp",false,true) || !plain_path("plates/index.txt",false,true)) return false;
  FILE *f = fopen("plates/index.txt.tmp","wb"); if (!f) return false;
  bool ok = fputs("LAPIS_PLATES_1\n",f) >= 0;
  for (unsigned i = 0; i < PLATE_LIMIT; i++) if (plates[i].name[0])
    if (fprintf(f,"%s %016" PRIx64 " %s\n",plates[i].name,plates[i].seed,plates_type_name(plate_types[i])) < 0) ok = false;
  if (fclose(f)) ok = false;
  if (!ok || rename("plates/index.txt.tmp","plates/index.txt")) return false;
  return true;
}
static void activate (unsigned id) {
  plate_current = id;
  active_world = plates[id].world ? plates[id].world : &legacy_world;
  for (unsigned i = 0; i < 9; i++) modules[i].select(plates[id].contexts[i]);
}
static void release (unsigned id) {
  for (unsigned i = 0; i < 9; i++) { free(plates[id].contexts[i]); plates[id].contexts[i] = NULL; }
  free(plates[id].world); plates[id].world = NULL;
}
bool plates_is_loaded (unsigned id) { return id < PLATE_LIMIT && plates[id].world != NULL; }
bool plates_load (unsigned id) {
  if (id >= PLATE_LIMIT || !plates[id].name[0]) return false;
  if (plates[id].world) return true;
  char path[96]; snprintf(path,sizeof(path),"plates/%s",plates[id].name);
  if (!plain_path(path,true,false)) return false;
  static const char *const files[] = {"world.bin","world.meta","signs.bin","doors.bin","circuits.bin","farming.bin"};
  for (unsigned i = 0; i < 6; i++) {
    snprintf(path,sizeof(path),"plates/%s/%s",plates[id].name,files[i]);
    if (!plain_path(path,false,true)) return false;
    size_t n = strlen(path); memcpy(path+n,".tmp",5);
    if (!plain_path(path,false,true)) return false;
  }
  plates[id].world = calloc(1,sizeof(WorldState));
  if (!plates[id].world) return false;
  for (unsigned i = 0; i < 9; i++) if (!(plates[id].contexts[i] = calloc(1,modules[i].size()))) { release(id); return false; }
  unsigned old = plate_current; activate(id);
  world_seed = plates[id].seed; rng_seed = (uint32_t)splitmix64(world_seed)|1u;
  memset(block_changes,255,sizeof(block_changes));
  snprintf(path,sizeof(path),"plates/%s/world.meta",plates[id].name);
  bool ok = world_metadata_open(path,plates[id].path,&world_seed,true,world_mirror_horizontal != 0) && !initSerializer();
  if (block_changes_count > MAX_BLOCK_CHANGES) ok = false;
  for (int i = 0; ok && i < block_changes_count; i++) if (block_changes[i].block == B_chest) {
    if (i > MAX_BLOCK_CHANGES-15) ok = false;
    i += 14;
  }
  if (ok) {
    snprintf(path,sizeof(path),"plates/%s/doors.bin",plates[id].name); ok = doors_load(path);
    snprintf(path,sizeof(path),"plates/%s/signs.bin",plates[id].name); ok = signs_load(path) && ok;
    snprintf(path,sizeof(path),"plates/%s/farming.bin",plates[id].name); ok = farming_load(path) && ok;
    snprintf(path,sizeof(path),"plates/%s/circuits.bin",plates[id].name); ok = circuits_load(path) && ok;
    ok = musicbox_init("songs") && ok; fluids_init();
  }
  if (!ok && old == id) {
    active_world = &legacy_world; for (unsigned i = 0; i < 9; i++) modules[i].select(NULL);
  } else activate(old);
  if (!ok) release(id);
  return ok;
}
bool plates_select (unsigned id) {
  if (!plates_enabled) return id == 0;
  if (!plates_load(id)) return false;
  activate(id); return true;
}
void plates_select_for_fd (int fd) {
  unsigned id = 0;
  for (unsigned i = 0; i < MAX_PLAYERS; i++) if (player_data[i].client_fd == fd) { id = player_plates[i]; break; }
  plates_select(id);
}
bool plates_create (const char *name, uint64_t seed, PlateType type) {
  if (!plates_enabled || generation == UINT32_MAX || !valid_name(name) || (unsigned)type > PLATE_VOLCANIC || plates_find(name) >= 0) return false;
  unsigned id = 0; while (id < PLATE_LIMIT && plates[id].name[0]) id++;
  if (id == PLATE_LIMIT) return false;
  char path[80]; snprintf(path,sizeof(path),"plates/%s",name);
  if (!directory_create(path)) return false;
  describe(id,name,seed,type);
  if (catalog_save()) return true;
  memset(&plates[id],0,sizeof(plates[id])); return false; /* Preserve orphan files for recovery. */
}
bool plates_remove (const char *name) {
  int id = plates_find(name); if (id <= 0 || !strcmp(name,"hub")) return false;
  for (unsigned i = 0; i < MAX_PLAYERS; i++) if (player_data[i].client_fd >= 0 && player_plates[i] == id) return false;
  char from[80], to[128]; snprintf(from,sizeof(from),"plates/%s",name);
  snprintf(to,sizeof(to),"plates/removed-%s-%" PRId64,name,get_program_time());
  if (!plain_path(from,true,false) || plain_path(to,true,false) || !plain_path(to,true,true) || rename(from,to)) return false;
  Plate removed = plates[id]; memset(&plates[id],0,sizeof(plates[id]));
  if (!catalog_save()) { plates[id] = removed; rename(to,from); return false; }
  if (plate_current == (unsigned)id) activate(0);
  /* Playback owns heap-allocated MIDI tracks; shut down before freeing context. */
  unsigned old = plate_current; plates[id] = removed;
  if (removed.world) { activate((unsigned)id); musicbox_shutdown(); activate(old); }
  release((unsigned)id); memset(&plates[id],0,sizeof(plates[id]));
  return true;
}
bool plates_start (void) {
  if (!server_config.experimental_enable_plates) return true;
#if defined(ESP_PLATFORM) || !defined(SYNC_WORLD_TO_DISK)
  fputs("Plates require the desktop filesystem build.\n",stderr); return false;
#else
  plates_enabled = true; plate_current = 0;
  if (!plain_path("plates",true,false) && !directory_create("plates")) return false;
  if (!plain_path("plates/index.txt",false,true)) return false;
  FILE *f = fopen("plates/index.txt","rb");
  if (!f) {
    if (errno != ENOENT || !plates_create("hub",server_config.seed,PLATE_HUB)) return false;
  } else {
    char line[96]; bool ok = fgets(line,sizeof(line),f) && !strcmp(line,"LAPIS_PLATES_1\n");
    unsigned count = 0;
    while (ok && fgets(line,sizeof(line),f)) {
      char name[25], type[16], hex[17], tail; PlateType t;
      if (count == PLATE_LIMIT || sscanf(line,"%24s %16s %15s %c",name,hex,type,&tail) != 3 ||
          !strchr(line,'\n') || !valid_name(name) || !type_parse(type,&t) || strlen(hex) != 16 || plates_find(name) >= 0) { ok = false; break; }
      uint64_t seed = 0;
      for (unsigned i = 0; i < 16; i++) {
        unsigned c = (unsigned char)hex[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) { ok = false; break; }
        seed = seed*16u+(c <= '9' ? c-'0' : c-'a'+10u);
      }
      if (ok) describe(count++,name,seed,t);
    }
    if (ferror(f)) ok = false;
    if (fclose(f)) ok = false;
    if (!ok || strcmp(plates[0].name,"hub") || plate_types[0] != PLATE_HUB) return false;
  }
  if (!plain_path("plates/players.bin",false,true) || !plain_path("plates/players.bin.tmp",false,true)) return false;
  f = fopen("plates/players.bin","rb");
  if (f) {
    bool ok = fread(player_data,1,sizeof(player_data),f) == sizeof(player_data) && fgetc(f) == EOF && !ferror(f);
    if (fclose(f)) ok = false;
    if (!ok) return false;
  } else if (errno != ENOENT) return false;
  for (unsigned i = 0; i < MAX_PLAYERS; i++) { player_data[i].client_fd = -1; player_plates[i] = 0; }
  return plates_select(0);
#endif
}
void plates_shutdown (void) {
  for (unsigned i = 0; i < PLATE_LIMIT; i++) if (plates[i].world) {
    activate(i); musicbox_shutdown();
  }
  active_world = &legacy_world;
  for (unsigned i = 0; i < 9; i++) modules[i].select(NULL);
  for (unsigned i = 0; i < PLATE_LIMIT; i++) release(i);
  memset(plates,0,sizeof(plates)); memset(sessions,0,sizeof(sessions)); memset(player_plates,0,sizeof(player_plates));
  plates_enabled = false; plate_current = 0;
}
void plates_player_reset (PlayerData *p) {
  int i = slot(p); if (i < 0) return;
  memset(&sessions[i],0,sizeof(sessions[i])); player_plates[i] = 0;
  if (plates_enabled) { p->x = p->z = 8; p->y = 65; p->flags |= 2; }
}
bool plates_travel (PlayerData *p, const char *name) {
  int who = slot(p), id = plates_find(name);
  if (!plates_enabled || who < 0 || id < 0 || p->client_fd < 0 || !p->health || (p->flags&0x22) ||
      player_plates[who] == id || get_program_time() < sessions[who].travelled || !plates_load((unsigned)id)) return false;
  if (!inventory_close(p)) return false;
#if LAPIS_WORLD_EDIT == 1
  world_edit_reset_player(p);
#endif
  signs_reset_player(p); musicbox_reset_player(p); items_forget_player(p); mobs_forget_player(p);
  for (int i = 0; i < MAX_PLAYERS; i++) if (i != who && plates_player_active(&player_data[i]) && player_data[i].client_fd >= 0) {
    sc_removeEntity(player_data[i].client_fd,p->client_fd);
  }
  player_plates[who] = (uint8_t)id; activate((unsigned)id);
  signs_reset_player(p); musicbox_reset_player(p); inventory_reset(p);
  p->x = p->z = 8; p->y = (uint8_t)(getHeightAt(8,8)+1); p->grounded_y = p->y;
  p->flags &= (uint8_t)~0x92u; p->flagval_8 = 0; p->flagval_16 = 0;
  for (unsigned i = 0; i < VISITED_HISTORY; i++) p->visited_x[i] = p->visited_z[i] = 32767;
  sc_respawn(p->client_fd); spawnPlayer(p);
  for (int i = 0; i < MAX_PLAYERS; i++) if (i != who && plates_player_active(&player_data[i]) && player_data[i].client_fd >= 0 && !(player_data[i].flags&0x20)) {
    sc_playerInfoUpdateAddPlayer(p->client_fd,player_data[i]); sc_spawnEntityPlayer(p->client_fd,player_data[i]);
    sc_playerInfoUpdateAddPlayer(player_data[i].client_fd,*p); sc_spawnEntityPlayer(player_data[i].client_fd,*p);
  }
  items_sync_player(p); mobs_sync_player(p); sc_updateTime(p->client_fd,world_time);
  sessions[who].travelled = get_program_time()+2000000; sessions[who].arriving = true;
  sessions[who].remove[0] = 0; writePlayerDataToDisk(); return true;
}
bool plates_movement_guard (PlayerData *p, double x, double y, double z) {
  int i = slot(p); if (!plates_enabled || i < 0) return false;
  if (sessions[i].arriving) {
    if (fabs(x-8.5) > 1 || fabs(z-8.5) > 1 || fabs(y-p->y) > 2) return true;
    sessions[i].arriving = false;
  }
  if (y < 1 && (plates_type() == PLATE_HUB || plates_type() == PLATE_SKYBOX)) {
    world_teleport(p,8,65,8); sessions[i].arriving = true; return true;
  }
  return false;
}
static CommandResult reply (PlayerData *p, CommandResult r, const char *s) { sc_systemChat(p->client_fd,(char *)s,(uint16_t)strlen(s)); return r; }
CommandResult plates_command (PlayerData *p, int argc, char *const argv[]) {
  int who = slot(p);
  if (!plates_enabled || who < 0) return reply(p,COMMAND_DENIED,"Plates are disabled. Set experimental_enable_plates=true and restart.");
  if (argc == 2 && !strcmp(argv[1],"list")) {
    char text[128];
    for (unsigned i = 0; i < PLATE_LIMIT; i++) if (plates[i].name[0]) {
      snprintf(text,sizeof(text),"%s%.24s (%s)",i == plate_current ? "* " : "",plates[i].name,plates_type_name(plate_types[i])); reply(p,COMMAND_OK,text);
    }
    return COMMAND_OK;
  }
  if (argc == 3 && !strcmp(argv[1],"go")) {
    bool ok = plates_travel(p,argv[2]);
    return reply(p,ok ? COMMAND_OK : COMMAND_DENIED,ok ? "Plate selected." : "Cannot travel: check name, loading state, inventory space, or wait two seconds.");
  }
  if (!commands_is_admin(p)) return reply(p,COMMAND_DENIED,"Plate creation and removal require administrator access. Use /plate list or /plate go <name>.");
  if (argc == 2 && !strcmp(argv[1],"confirm")) {
    int id = plates_find(sessions[who].remove);
    bool valid = id > 0 && sessions[who].generation == plates[id].generation && get_program_time() <= sessions[who].expires;
    bool ok = valid && plates_remove(sessions[who].remove); sessions[who].remove[0] = 0;
    return reply(p,ok ? COMMAND_OK : COMMAND_DENIED,ok ? "Plate removed; its files were archived under plates/removed-*." : "No valid confirmation, plate occupied, or removal failed.");
  }
  sessions[who].remove[0] = 0;
  if (argc == 3 && !strcmp(argv[1],"remove")) {
    int id = plates_find(argv[2]);
    if (id <= 0) return reply(p,COMMAND_DENIED,"Unknown plate, or protected default hub.");
    snprintf(sessions[who].remove,sizeof(sessions[who].remove),"%s",argv[2]); sessions[who].expires = get_program_time()+30000000; sessions[who].generation = plates[id].generation;
    char text[128]; snprintf(text,sizeof(text),"Remove plate '%s'? All players must leave it. Use /plate confirm within 30 seconds.",argv[2]);
    return reply(p,COMMAND_OK,text);
  }
  if (argc == 5 && !strcmp(argv[1],"create")) {
    uint64_t seed; PlateType type;
    if (!beta173_seed_parse(argv[3],&seed) || !type_parse(argv[4],&type) || !valid_name(argv[2])) return reply(p,COMMAND_USAGE,"Use a safe lowercase name, signed 64-bit seed, and hub|betanium|flatworld|skybox|volcanic.");
    bool ok = plates_create(argv[2],seed,type);
    return reply(p,ok ? COMMAND_OK : COMMAND_DENIED,ok ? "Plate created. Use /plate go <name> to visit." : "Cannot create plate: existing name/directory, eight-plate limit, or storage error.");
  }
  return reply(p,COMMAND_USAGE,"/plate list | /plate go <name> | /plate create <name> <seed> <type> | /plate remove <name> | /plate confirm");
}
