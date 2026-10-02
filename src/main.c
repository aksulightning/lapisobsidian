#include "items.h"
#include "server_config.h"
#include "packet_input.h"
#include "mobs.h"
#include "doors.h"
#include "circuits.h"
#include "farming.h"
#include "musicbox.h"
#include "signs.h"
#include "commands.h"
#include "world_border.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include "beta173_rng.h"
#include "beta173_worldgen.h"
#include "world_metadata.h"

#ifndef CLOCK_REALTIME
#define CLOCK_REALTIME 0
#endif

#ifdef ESP_PLATFORM
  #include "freertos/FreeRTOS.h"
  #include "freertos/task.h"
  #include "nvs_flash.h"
  #include "esp_wifi.h"
  #include "esp_event.h"
  #include "esp_timer.h"
  #include "lwip/sockets.h"
  #include "lwip/netdb.h"
#else
  #include <sys/types.h>
  #ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
  #else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
  #endif
  #include <unistd.h>
  #include <time.h>
#endif

#include "globals.h"
#include "tools.h"
#include "varnum.h"
#include "packets.h"
#include "worldgen.h"
#include "registries.h"
#include "registry.h"
#include "procedures.h"
#include "serialize.h"

/* Dispatch only complete, bounded frames. recv_all cannot cross the current
 * frame; individual handlers validate fields before applying game actions. */
void handlePacket (int client_fd, int length, int packet_id, int state) {

  // Count the amount of bytes received to catch length discrepancies
  uint64_t bytes_received_start = total_bytes_received;

  switch (packet_id) {

    case 0x00:
      if (state == STATE_NONE) {
        if (cs_handshake(client_fd)) break;
      } else if (state == STATE_STATUS) {
        if (sc_statusResponse(client_fd)) break;
      } if (state == STATE_LOGIN) {
        uint8_t uuid[16];
        char name[16];
        if (cs_loginStart(client_fd, uuid, name)) break;
        if (reservePlayerData(client_fd, uuid, name)) {
          recv_count = 0;
          return;
        }
        if (sc_loginSuccess(client_fd, uuid, name)) break;
      } else if (state == STATE_CONFIGURATION) {
        if (cs_clientInformation(client_fd)) break;
        if (sc_knownPacks(client_fd)) break;

        #ifdef SEND_BRAND
        if (sc_sendPluginMessage(client_fd, "minecraft:brand", (uint8_t *)brand, brand_len)) break;
        #endif
      }
      break;

    case 0x01:
      // Handle status ping
      if (state == STATE_STATUS) {
        // No need for a packet handler, just echo back the long verbatim
        writeByte(client_fd, 9);
        writeByte(client_fd, 0x01);
        writeUint64(client_fd, readUint64(client_fd));
        // Close connection after this
        recv_count = 0;
        return;
      }
      break;

    case 0x02:
      if (state == STATE_CONFIGURATION) cs_pluginMessage(client_fd);
      break;

    case 0x03:
      if (state == STATE_LOGIN) {
        printf("Client Acknowledged Login\n\n");
        setClientState(client_fd, STATE_CONFIGURATION);
      } else if (state == STATE_CONFIGURATION) {
        printf("Client Acknowledged Configuration\n\n");

        // Enter client into "play" state
        setClientState(client_fd, STATE_PLAY);
        sc_loginPlay(client_fd);

        PlayerData *player;
        if (getPlayerData(client_fd, &player)) break;

        // Send full client spawn sequence
        spawnPlayer(player);
        sc_commands(client_fd);

        // Register all existing players and spawn their entities
        for (int i = 0; i < MAX_PLAYERS; i ++) {
          if (player_data[i].client_fd == -1) continue;
          // Note that this will also filter out the joining player
          if (player_data[i].flags & 0x20) continue;
          sc_playerInfoUpdateAddPlayer(client_fd, player_data[i]);
          sc_spawnEntityPlayer(client_fd, player_data[i]);
        }


      }
      break;

    case 0x06:
      if (state == STATE_PLAY && cs_chatCommand(client_fd,length,false)) { recv_count = 0; return; }
      break;

    case 0x07:
      if (state == STATE_PLAY && cs_chatCommand(client_fd,length,true)) { recv_count = 0; return; }
      if (state == STATE_CONFIGURATION) {
        if (cs_knownPacks(client_fd)) { recv_count = 0; return; }
        if (sc_registries(client_fd)) { recv_count = 0; return; }
        printf("Received Client's Known Packs\n");
        printf("  Finishing configuration\n\n");
        sc_finishConfiguration(client_fd);
      }
      break;

    case 0x08:
      if (state == STATE_PLAY && cs_chat(client_fd,length)) { recv_count = 0; return; }
      break;

    case 0x0B:
      if (state == STATE_PLAY) cs_clientStatus(client_fd);
      break;

    case 0x0C: // Client tick (ignored)
      break;

    case 0x11:
      if (state == STATE_PLAY && cs_clickContainer(client_fd,length)) { recv_count = 0; return; }
      break;

    case 0x12:
      if (state == STATE_PLAY && (length != 1 || cs_closeContainer(client_fd))) { recv_count = 0; return; }
      break;

    case 0x1B:
      if (state == STATE_PLAY) {
        // Serverbound keep-alive (ignored)
        discard_all(client_fd, length, false);
      }
      break;

    case 0x19:
      if (state == STATE_PLAY && cs_interact(client_fd,length)) { recv_count = 0; return; }
      break;

    case 0x1D:
    case 0x1E:
    case 0x1F:
    case 0x20:
      if (state == STATE_PLAY) {

        int expected = packet_id == 0x1D ? 25 : packet_id == 0x1E ? 33 : packet_id == 0x1F ? 9 : 1;
        if (length != expected) { recv_count = 0; return; }
        double x = 0, y = 0, z = 0;
        float yaw = 0, pitch = 0;
        uint8_t on_ground;

        // Read player position (and rotation)
        if (packet_id == 0x1D) cs_setPlayerPosition(client_fd, &x, &y, &z, &on_ground);
        else if (packet_id == 0x1F) cs_setPlayerRotation (client_fd, &yaw, &pitch, &on_ground);
        else if (packet_id == 0x20) cs_setPlayerMovementFlags (client_fd, &on_ground);
        else cs_setPlayerPositionAndRotation(client_fd, &x, &y, &z, &yaw, &pitch, &on_ground);

        if (recv_count <= 0) { recv_count = 0; return; }
        if ((packet_id == 0x1D || packet_id == 0x1E) &&
            (!isfinite(x) || !isfinite(y) || !isfinite(z) ||
             y < 0 || y >= 256)) {
          recv_count = 0;
          return;
        }
        if ((packet_id == 0x1E || packet_id == 0x1F) && (!isfinite(yaw) || !isfinite(pitch))) {
          recv_count = 0;
          return;
        }
        PlayerData *player;
        if (getPlayerData(client_fd, &player)) break;

        if ((packet_id == 0x1D || packet_id == 0x1E) && world_border_guard(player,x,z)) break;

        uint8_t block_feet = getBlockAt(player->x, player->y, player->z);
        uint8_t swimming = block_feet >= B_water && block_feet < B_water + 8;

        // Handle fall damage
        if (on_ground) {
          int16_t damage = player->grounded_y - player->y - 3;
          if (damage > 0 && (commands_gamemode(player) == 0 || commands_gamemode(player) == 2) && !swimming) {
            hurtEntity(client_fd, -1, D_fall, damage);
          }
          player->grounded_y = player->y;
        } else if (swimming) {
          player->grounded_y = player->y;
        }

        // Don't continue if all we got were flags
        if (packet_id == 0x20) break;

        // Update rotation in player data (if applicable)
        if (packet_id != 0x1D) {
          double angle = fmod((double)yaw + 180.0, 360.0);
          if (angle < 0) angle += 360.0;
          player->yaw = (int8_t)((angle - 180.0) * 127.0 / 180.0);
          if (pitch < -90) pitch = -90;
          if (pitch > 90) pitch = 90;
          player->pitch = (int8_t)(pitch / 90.0f * 127.0f);
        }

        // Whether to broadcast player position to other players
        uint8_t should_broadcast = true;

        #ifndef BROADCAST_ALL_MOVEMENT
          // If applicable, tie movement updates to the tickrate by using
          // a flag that gets reset on every tick. It might sound better
          // to just make the tick handler broadcast position updates, but
          // then we lose precision. While position is stored using integers,
          // here the client gives us doubles and floats directly.
          should_broadcast = !(player->flags & 0x40);
          if (should_broadcast) player->flags |= 0x40;
        #endif

        #ifdef SCALE_MOVEMENT_UPDATES_TO_PLAYER_COUNT
          // If applicable, broadcast only every client_count-th movement update
          if (++player->packets_since_update < client_count) {
            should_broadcast = false;
          } else {
            // Note that this does not explicitly set should_broadcast to true
            // This allows the above BROADCAST_ALL_MOVEMENT check to compound
            // Whether that's ever favorable is up for debate
            player->packets_since_update = 0;
          }
        #endif

        if (should_broadcast) {
          // If the packet had no rotation data, calculate it from player data
          if (packet_id == 0x1D) {
            yaw = player->yaw * 180 / 127;
            pitch = player->pitch * 90 / 127;
          }
          // Send current position data to all connected players
          for (int i = 0; i < MAX_PLAYERS; i ++) {
            if (player_data[i].client_fd == -1) continue;
            if (player_data[i].flags & 0x20) continue;
            if (player_data[i].client_fd == client_fd) continue;
            if (packet_id == 0x1F) {
              sc_updateEntityRotation(player_data[i].client_fd, client_fd, player->yaw, player->pitch);
            } else {
              sc_teleportEntity(player_data[i].client_fd, client_fd, x, y, z, yaw, pitch);
            }
            sc_setHeadRotation(player_data[i].client_fd, client_fd, player->yaw);
          }
        }

        // Don't continue if all we got was rotation data
        if (packet_id == 0x1F) break;

        // Players send movement packets roughly 20 times per second when
        // moving, and much less frequently when standing still. We can
        // use this correlation between actions and packet count to cheaply
        // simulate hunger with a timer-based system, where the timer ticks
        // down with each position packet. The timer value itself then
        // naturally works as a substitute for saturation.
        if (commands_gamemode(player) != 1 && commands_gamemode(player) != 3) {
          if (player->saturation == 0) {
            if (player->hunger > 0) player->hunger--;
            player->saturation = 200;
            sc_setHealth(client_fd, player->health, player->hunger, player->saturation);
          } else if (player->flags & 0x08) {
            player->saturation -= 1;
          }
        }

        // Cast the values to short to get integer position
        short cx = (short)floor(x), cy = (short)y, cz = (short)floor(z);
        // Determine the player's chunk coordinates
        short _x = (short)div_floor(cx, 16), _z = (short)div_floor(cz, 16);
        // Calculate distance between previous and current chunk coordinates
        short dx = (short)(_x - div_floor(player->x, 16));
        short dz = (short)(_z - div_floor(player->z, 16));

        // Prevent players from leaving the world
        if (cy < 0) {
          cy = 0;
          player->grounded_y = 0;
          sc_synchronizePlayerPosition(client_fd, cx, 0, cz, player->yaw * 180 / 127, player->pitch * 90 / 127);
        } else if (cy > 255) {
          cy = 255;
          sc_synchronizePlayerPosition(client_fd, cx, 255, cz, player->yaw * 180 / 127, player->pitch * 90 / 127);
        }

        // Update position in player data
        player->x = cx;
        player->y = cy;
        player->z = cz;

        // Exit early if no chunk borders were crossed
        if (dx == 0 && dz == 0) break;

        // Check if the player has recently been in this chunk
        int found = false;
        for (int i = 0; i < VISITED_HISTORY; i ++) {
          if (player->visited_x[i] == _x && player->visited_z[i] == _z) {
            found = true;
            break;
          }
        }
        /* History suppresses repeat natural spawns, never chunk transmission. */

        // Update player's recently visited chunks
        for (int i = 0; i < VISITED_HISTORY - 1; i ++) {
          player->visited_x[i] = player->visited_x[i + 1];
          player->visited_z[i] = player->visited_z[i + 1];
        }
        player->visited_x[VISITED_HISTORY - 1] = _x;
        player->visited_z[VISITED_HISTORY - 1] = _z;

        if (!found) mobs_spawn_exploration(_x,_z,dx,dz,cy,fast_rand());

        world_send_view(client_fd,_x,_z,_x-dx,_z-dz,false);

      }
      break;

    case 0x29:
      if (state == STATE_PLAY) cs_playerCommand(client_fd);
      break;

    case 0x2A:
      if (state == STATE_PLAY) cs_playerInput(client_fd);
      break;

    case 0x2B:
      if (state == STATE_PLAY) cs_playerLoaded(client_fd);
      break;

    case 0x37:
      if (state == STATE_PLAY && cs_creativeSlot(client_fd,length)) { recv_count = 0; return; }
      break;

    case 0x34:
      if (state == STATE_PLAY && (length != 2 || cs_setHeldItem(client_fd))) { recv_count = 0; return; }
      break;
	
    case 0x3B:
      if (state == STATE_PLAY && cs_updateSign(client_fd,length)) { recv_count = 0; return; }
      break;

    case 0x3C:
      if (state == STATE_PLAY) cs_swingArm(client_fd);
      break;

    case 0x28:
      if (state == STATE_PLAY && cs_playerAction(client_fd,length)) { recv_count = 0; return; }
      break;

    case 0x3F:
      if (state == STATE_PLAY && cs_useItemOn(client_fd,length)) { recv_count = 0; return; }
      break;

    case 0x40:
      if (state == STATE_PLAY) cs_useItem(client_fd);
      break;

    default:
      #ifdef DEV_LOG_UNKNOWN_PACKETS
        printf("Unknown packet: 0x");
        if (packet_id < 16) printf("0");
        printf("%X, length: %d, state: %d\n\n", packet_id, length, state);
      #endif
      discard_all(client_fd, length, false);
      break;

  }

  // Detect and fix incorrectly parsed packets
  int processed_length = total_bytes_received - bytes_received_start;
  if (processed_length == length) return;

  if (length > processed_length) {
    discard_all(client_fd, length - processed_length, false);
  }

  #ifdef DEV_LOG_LENGTH_DISCREPANCY
  if (processed_length != 0) {
    printf("WARNING: Packet 0x");
    if (packet_id < 16) printf("0");
    printf("%X parsed incorrectly!\n  Expected: %d, parsed: %d\n\n", packet_id, length, processed_length);
  }
  #endif
  #ifdef DEV_LOG_UNKNOWN_PACKETS
  if (processed_length == 0) {
    printf("Unknown packet: 0x");
    if (packet_id < 16) printf("0");
    printf("%X, length: %d, state: %d\n\n", packet_id, length, state);
  }
  #endif

}

int main (int argc, char **argv) {
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
  if (!world_metadata_open("world.meta", "world.bin", &world_seed, explicit_seed, world_mirror_horizontal != 0)) return EXIT_FAILURE;
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

  // Start the disk/flash serializer (if applicable)
  if (initSerializer()) exit(EXIT_FAILURE);
  // Initialize all file descriptor references to -1 (unallocated)
  int clients[MAX_PLAYERS], client_index = 0;
  for (int i = 0; i < MAX_PLAYERS; i ++) {
    clients[i] = -1;
    client_states[i * 2] = -1;
    player_data[i].client_fd = -1;
  }

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
  circuits_tick();

  // Create server TCP socket
  int server_fd, opt = 1;
  struct sockaddr_in server_addr, client_addr;
  socklen_t addr_len = sizeof(client_addr);

  server_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd == -1) {
    perror("socket failed");
    exit(EXIT_FAILURE);
  }
#ifdef _WIN32
  if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR,
      (const char*)&opt, sizeof(opt)) < 0) {
#else
  if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
#endif    
    perror("socket options failed");
    exit(EXIT_FAILURE);
  }

  // Bind socket to IP/port
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = INADDR_ANY;
  server_addr.sin_port = htons(server_config.port);

  if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
    perror("bind failed");
    close(server_fd);
    exit(EXIT_FAILURE);
  }

  // Listen for incoming connections
  if (listen(server_fd, 5) < 0) {
    perror("listen failed");
    close(server_fd);
    exit(EXIT_FAILURE);
  }
  printf("Server listening on port %u...\n", (unsigned)server_config.port);

  // Make the socket non-blocking
  // This is necessary to not starve the idle task during slow connections
  #ifdef _WIN32
    u_long mode = 1;  // 1 = non-blocking
    if (ioctlsocket(server_fd, FIONBIO, &mode) != 0) {
      fprintf(stderr, "Failed to set non-blocking mode\n");
      exit(EXIT_FAILURE);
    }
  #else
  int flags = fcntl(server_fd, F_GETFL, 0);
  fcntl(server_fd, F_SETFL, flags | O_NONBLOCK);
  #endif

  // Track time of last server tick (in microseconds)
  int64_t last_tick_time = get_program_time();
  int64_t last_arrow_time = last_tick_time;
  int64_t last_mob_move_time = last_tick_time;
  int64_t last_music_time = last_tick_time;

  /**
   * Cycles through all connected clients, handling one packet at a time
   * from each player. With every iteration, attempts to accept a new
   * client connection.
   */
  PacketInput inputs[MAX_PLAYERS];
  for (int i = 0; i < MAX_PLAYERS; i++) packet_input_reset(&inputs[i],-1);
  while (true) {
    // Check if it's time to yield to the idle task
    task_yield();

    // Attempt to accept a new connection
    for (int i = 0; i < MAX_PLAYERS; i ++) {
      if (clients[i] != -1) continue;
      clients[i] = accept(server_fd, (struct sockaddr *)&client_addr, &addr_len);
      // If the accept was successful, make the client non-blocking too
      if (clients[i] != -1) {
        printf("New client, fd: %d\n", clients[i]);
      #ifdef _WIN32
        u_long mode = 1;
        ioctlsocket(clients[i], FIONBIO, &mode);
      #else
        int flags = fcntl(clients[i], F_GETFL, 0);
        fcntl(clients[i], F_SETFL, flags | O_NONBLOCK);
      #endif
        packet_input_reset(&inputs[i],clients[i]);
        client_count ++;
      }
      break;
    }

    // Look for valid connected clients
    client_index ++;
    if (client_index == MAX_PLAYERS) client_index = 0;

    // Projectiles, circuits and the bounded farm sweep use a 100 ms cadence.
    int64_t arrow_now = get_program_time();
    if (arrow_now-last_mob_move_time >= 50000) {
      mobs_tick_movement(arrow_now-last_mob_move_time);
      last_mob_move_time = arrow_now;
    }
    if (arrow_now-last_music_time >= 20000) { musicbox_tick(arrow_now-last_music_time); last_music_time = arrow_now; }
    if (arrow_now-last_arrow_time >= 100000) {
      mobs_tick_arrows(arrow_now-last_arrow_time);
      circuits_tick();
      farming_tick(arrow_now-last_arrow_time);
      last_arrow_time = arrow_now;
    }
    // Handle periodic events (server ticks)
    int64_t time_since_last_tick = get_program_time() - last_tick_time;
    if (time_since_last_tick > TIME_BETWEEN_TICKS) {
      handleServerTick(time_since_last_tick);
      last_tick_time = get_program_time();
    }

    if (clients[client_index] == -1) continue;

    // Handle this individual client
    int client_fd = clients[client_index];

    int ready = packet_input_poll(&inputs[client_index],get_program_time());
    if (ready < 0) { disconnectClient(&clients[client_index],2); continue; }
    if (!ready) continue;
    recv_count = 1;
    int packet_id = readVarInt(client_fd);
    if (packet_id < 0 || recv_count <= 0) {
      packet_input_end(); disconnectClient(&clients[client_index],3); continue;
    }
    int state = getClientState(client_fd);
    handlePacket(client_fd,(int)packet_input_remaining(),packet_id,state);
    bool complete = packet_input_end();
    if (!complete || recv_count <= 0) { disconnectClient(&clients[client_index],4); continue; }

  }

  close(server_fd);
 
  #ifdef _WIN32 //cleanup windows socket
    WSACleanup();
  #endif

  printf("Server closed.\n");

}

#ifdef ESP_PLATFORM

void lapis_obsidian_main (void *pvParameters) {
  main(0, NULL);
  vTaskDelete(NULL);
}

static void wifi_event_handler (void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
    esp_wifi_connect();
  } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
    esp_wifi_connect();
  } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    printf("Got IP, starting server...\n\n");
    xTaskCreate(lapis_obsidian_main, "Lapis Obsidian", 8192, NULL, 5, NULL);
  }
}

void wifi_init () {
  nvs_flash_init();
  esp_netif_init();
  esp_event_loop_create_default();
  esp_netif_create_default_wifi_sta();

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  esp_wifi_init(&cfg);

  esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL);
  esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL);

  wifi_config_t wifi_config = {
    .sta = {
      .ssid = WIFI_SSID,
      .password = WIFI_PASS,
      .threshold.authmode = WIFI_AUTH_WPA2_PSK
    }
  };

  esp_wifi_set_mode(WIFI_MODE_STA);
  esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
  esp_wifi_set_ps(WIFI_PS_NONE);
  esp_wifi_start();
}

void app_main () {
  esp_timer_early_init();
  wifi_init();
}

#endif
