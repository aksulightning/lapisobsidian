/* Initialize configured worlds and persist loaded worlds on shutdown. */
#include "server_runtime.h"
#include "registry.h"
#include "server_config.h"
#include "world_metadata.h"
#include "plates.h"
#include "drop_store.h"
#include "items.h"
#include "doors.h"
#include "signs.h"
#include "farming.h"
#include "circuits.h"
#include "musicbox.h"
#include "fluids.h"
#include "commands.h"
#include "procedures.h"
#include "serialize.h"
#include "tools.h"
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#ifdef _WIN32
#include <winsock2.h>
#endif

int server_initialize(int argc, char **argv, int clients[MAX_PLAYERS]) {
  if (!registry_validate()) {
    fputs("Lapis Obsidian: invalid protocol registry snapshot\n", stderr);
    return EXIT_FAILURE;
  }
  #ifdef _WIN32 //initialize windows socket
    WSADATA wsa;
      if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        fprintf(stderr, "WSAStartup failed\n");
        exit(EXIT_FAILURE);
      }
  #endif

  if (!commands_configure(getenv("LAPIS_ADMIN_TOKEN"))) {
    fputs("LAPIS_ADMIN_TOKEN must contain 32..128 printable non-space ASCII bytes\n",stderr);
    return EXIT_FAILURE;
  }

  char config_error[200];
  #ifdef ESP_PLATFORM
  /* LittleFS is mounted by the embedded serializer, after desktop startup. */
  const char *config_path = NULL;
  #else
  const char *config_path = "server.txt";
  #endif
  if ((config_path && !server_config_load(config_path,&server_config,config_error,sizeof(config_error))) ||
      !server_config_arguments(&server_config,argc,argv,config_error,sizeof(config_error))) {
    fprintf(stderr,"Lapis Obsidian: %s\n",config_error); return EXIT_FAILURE;
  }
  world_seed = server_config.seed;
  world_mirror_horizontal = server_config.mirror_horizontal ? 1 : 0;
  bool explicit_seed = server_config.seed_set;
  #if defined(SYNC_WORLD_TO_DISK) && !defined(ESP_PLATFORM)
  if (!server_config.experimental_enable_plates && !world_metadata_open("world.meta", "world.bin", &world_seed, explicit_seed, world_mirror_horizontal != 0)) return EXIT_FAILURE;
  #else
  (void)explicit_seed;
  #endif
  printf("Lapis Obsidian world seed (64-bit hex): %016" PRIx64 "\n", world_seed);

  printf("Horizontal world mirroring: %s\n", world_mirror_horizontal ? "enabled (X axis)" : "disabled");

  rng_seed = splitmix64(rng_seed);
  printf("\nRNG seed (hashed): ");
  for (int i = 3; i >= 0; i --) printf("%X", (unsigned int)((rng_seed >> (8 * i)) & 255));
  printf("\n\n");

  // Initialize block changes entries as unallocated
  for (int i = 0; i < MAX_BLOCK_CHANGES; i ++) {
    block_changes[i].block = 0xFF;
  }

  if (!server_config.experimental_enable_plates && !drop_store_recover(".",false)) return EXIT_FAILURE;
  // Start the disk/flash serializer (if applicable)
  if (server_config.experimental_enable_plates) {
    if (!plates_start()) { fputs("Cannot initialize Plates; check plates/index.txt and world saves.\n",stderr); return EXIT_FAILURE; }
  } else if (initSerializer()) exit(EXIT_FAILURE);
  // Initialize all file descriptor references to -1 (unallocated)

  for (int i = 0; i < MAX_PLAYERS; i ++) {
    clients[i] = -1;
    client_states[i * 2] = -1;
    player_data[i].client_fd = -1;
  }

  if (!plates_enabled) {
  #ifdef SYNC_WORLD_TO_DISK
    if (!items_load("drops.bin")) return EXIT_FAILURE;
  #else
    if (!items_load(NULL)) return EXIT_FAILURE;
  #endif
  #ifdef SYNC_WORLD_TO_DISK
    #ifdef ESP_PLATFORM
    if (!doors_load("/littlefs/doors.bin")) exit(EXIT_FAILURE);
    if (!signs_load("/littlefs/signs.bin")) exit(EXIT_FAILURE);
    #else
    if (!doors_load("doors.bin")) exit(EXIT_FAILURE);
    if (!signs_load("signs.bin")) exit(EXIT_FAILURE);
    #endif
  #else
    doors_load(NULL);
    signs_load(NULL);
  #endif

  #ifdef SYNC_WORLD_TO_DISK
    #ifdef ESP_PLATFORM
    if (!farming_load("/littlefs/farming.bin") || !circuits_load("/littlefs/circuits.bin") || !musicbox_init("/littlefs/songs")) exit(EXIT_FAILURE);
    #else
    if (!farming_load("farming.bin") || !circuits_load("circuits.bin") || !musicbox_init("songs")) { fputs("Invalid farming.bin, circuits.bin or songs directory.\n",stderr); exit(EXIT_FAILURE); }
    #endif
  #else
    farming_load(NULL); circuits_load(NULL); musicbox_init("songs");
  #endif
  fluids_init();
  if (!drop_store_begin()) return EXIT_FAILURE;
  circuits_tick();
  if (!drop_store_commit()) return EXIT_FAILURE;

  }

  return EXIT_SUCCESS;
}

bool server_save_worlds(void) {
  bool saved = true;
  for (unsigned plate = 0; plate < (plates_enabled ? PLATE_LIMIT : 1u); plate++) {
    if (plates_enabled && !plates_is_loaded(plate)) continue;
    if (!plates_select(plate)) continue;
    writeBlockChangesToDisk(0,block_changes_count-1);
    if (!items_save()) saved = false;
    if (!doors_save()) saved = false;
    if (!signs_save()) saved = false;
    if (!farming_save()) saved = false;
    if (!circuits_save()) saved = false;
  }
  writePlayerDataToDisk();
  if (plates_enabled) plates_shutdown(); else musicbox_shutdown();
  if (!saved) fputs("Could not save one or more world sidecar files.\n",stderr);

  return saved;
}
