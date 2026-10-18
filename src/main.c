/* Round-robin client IO, simulation scheduling and orderly shutdown. */
#include "server_runtime.h"
#include "plates.h"
#include "standalone.h"
#include "webclient.h"
#include "fluids.h"
#include "items.h"
#include "drop_store.h"
#include "auth.h"
#include "packet_input.h"
#include "mobs.h"
#include "circuits.h"
#include "farming.h"
#include "musicbox.h"
#include "server_console.h"
#include "server_stats.h"
#include "tools.h"
#include "varnum.h"
#include "procedures.h"
#include "packet_dispatch.h"
#include <stdio.h>
#include <stdlib.h>

/* Own the process lifecycle and serialize world access: every tick/packet runs
 * with its Plate selected, and durable play changes are committed before their
 * queued output is released. A persistence failure terminates the server. */
int main (int argc, char **argv) {
  int clients[MAX_PLAYERS], client_index = 0;
  if (server_initialize(argc, argv, clients)) return EXIT_FAILURE;

  int server_fd = server_listen();
  if (server_fd < 0) return EXIT_FAILURE;

  printf("Experimental drops: %s; multiplayer identity: %s\n", LAPIS_ENABLE_EXPERIMENTAL_DROPSYSTEM ? "persistent" : "transient", LAPIS_ENABLE_EXPERIMENTAL_AUTH ? "verified Java sessions required" : "offline/unverified");
  if (!auth_init()) { fputs("Cannot initialize experimental authentication (OpenSSL/libcurl).\n",stderr); return EXIT_FAILURE; }

  // Track time of last server tick (in microseconds)
  int64_t last_tick_time = get_program_time();
  int64_t last_arrow_time = last_tick_time;
  int64_t last_mob_move_time = last_tick_time;
  int64_t last_music_time = last_tick_time;
  server_stats_reset();
  ServerConsole console;
  server_console_init(&console);
  if (!console.closed) puts("Console ready. Type help for commands.");
  fflush(stdout);

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
    #ifdef LAPIS_SINGLEPLAYER
    if (standalone_yield()) break;
    #else
    server_console_poll(&console,stdout);
    if (console.stop) break;
    #endif

    #if (defined(LAPIS_ENABLE_WEBCLIENT) && LAPIS_ENABLE_WEBCLIENT == 1) || defined(LAPIS_SINGLEPLAYER)
    webclient_poll(get_program_time());
    #endif
    auth_poll();
    // Attempt to accept a new connection
    for (int i = 0; i < MAX_PLAYERS; i ++) {
      if (clients[i] != -1) continue;
      clients[i] = server_accept(server_fd);
      // Accepted transports are already nonblocking.
      if (clients[i] != -1) {
        printf("New client, fd: %d\n", clients[i]);
        packet_input_reset(&inputs[i],clients[i]);
        client_count ++;
        if (!auth_accept(clients[i],get_program_time())) disconnectClient(&clients[i],5);
      }
      break;
    }

    // Look for valid connected clients
    client_index ++;
    if (client_index == MAX_PLAYERS) client_index = 0;

    // Projectiles, circuits and the bounded farm sweep use a 100 ms cadence.
    int64_t arrow_now = get_program_time();
    int64_t time_since_last_tick = arrow_now-last_tick_time;
    for (unsigned plate = 0; plate < (plates_enabled ? PLATE_LIMIT : 1u); plate++) {
      /* Only loaded worlds have active simulation. select() itself is cheap. */
      if (plates_enabled && !plates_is_loaded(plate)) continue;
      if (!plates_select(plate)) continue;
      bool transaction = arrow_now-last_mob_move_time >= 50000 || arrow_now-last_arrow_time >= 100000 || time_since_last_tick > TIME_BETWEEN_TICKS;
      if (transaction && !drop_store_begin()) return EXIT_FAILURE;
      if (arrow_now-last_mob_move_time >= 50000) mobs_tick_movement(arrow_now-last_mob_move_time);
      if (arrow_now-last_music_time >= 20000) musicbox_tick(arrow_now-last_music_time);
      if (arrow_now-last_arrow_time >= 100000) {
        mobs_tick_arrows(arrow_now-last_arrow_time); fluids_tick(arrow_now-last_arrow_time);
        circuits_tick(); farming_tick(arrow_now-last_arrow_time);
      }
      if (time_since_last_tick > TIME_BETWEEN_TICKS) handleServerTick(time_since_last_tick);
      if (transaction && !drop_store_commit()) return EXIT_FAILURE;
    }
    if (arrow_now-last_mob_move_time >= 50000) last_mob_move_time = arrow_now;
    if (arrow_now-last_music_time >= 20000) last_music_time = arrow_now;
    if (arrow_now-last_arrow_time >= 100000) last_arrow_time = arrow_now;
    if (time_since_last_tick > TIME_BETWEEN_TICKS) {
      /* One process-wide cycle, regardless of the number of loaded Plates. */
      server_stats_record_tick(time_since_last_tick);
      last_tick_time = arrow_now;
    }

    if (clients[client_index] == -1) continue;

    // Handle this individual client
    int client_fd = clients[client_index];
    plates_select_for_fd(client_fd);

    if (auth_progress(client_fd,get_program_time()) < 0) { disconnectClient(&clients[client_index],5); continue; }
    int ready = packet_input_poll(&inputs[client_index],get_program_time());
    if (ready < 0) { disconnectClient(&clients[client_index],2); continue; }
    if (!ready) continue;
    recv_count = 1;
    int packet_id = readVarInt(client_fd);
    if (packet_id < 0 || recv_count <= 0) {
      packet_input_end(); disconnectClient(&clients[client_index],3); continue;
    }
    int state = getClientState(client_fd);
    bool persistent_action = state == STATE_PLAY;
    if (persistent_action && !drop_store_begin()) return EXIT_FAILURE;
    handlePacket(client_fd,(int)packet_input_remaining(),packet_id,state);
    /* Release the frame even on rejection. Disconnect cleanup may return held
     * window stacks, so it remains inside the same persistence transaction. */
    bool complete = packet_input_end();
    if (!complete || recv_count <= 0) disconnectClient(&clients[client_index],4);
    if (persistent_action && !drop_store_commit()) return EXIT_FAILURE;

  }

  for (int i = 0; i < MAX_PLAYERS; i++) if (clients[i] >= 0) {
    plates_select_for_fd(clients[i]);
    if (!drop_store_begin()) return EXIT_FAILURE;
    disconnectClient(&clients[i],0);
    if (!drop_store_commit()) return EXIT_FAILURE;
  }
  #if (defined(LAPIS_ENABLE_WEBCLIENT) && LAPIS_ENABLE_WEBCLIENT == 1) || defined(LAPIS_SINGLEPLAYER)
  webclient_stop();
  #endif
  bool saved = server_save_worlds();
  server_close(server_fd);

  auth_shutdown();
  printf("Server closed.\n");
  return saved ? EXIT_SUCCESS : EXIT_FAILURE;

}
