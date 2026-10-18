/* Dispatch complete protocol frames and consume any unread payload. */
#include "signs.h"
#include "procedures.h"
#include "packets.h"
#include "tools.h"
#include "plates.h"
#include "registries.h"
#include "packet_dispatch.h"
#include "auth.h"
#include "commands.h"
#include "registry.h"
#include <stdio.h>

/* Dispatch only complete, bounded frames. recv_all cannot cross the current
 * frame; individual handlers validate fields before applying game actions. */
void handlePacket (int client_fd, int length, int packet_id, int state) {

#if LAPIS_ENABLE_EXPERIMENTAL_AUTH
  if(state==STATE_LOGIN) {
    if(!auth_login_packet(client_fd,packet_id)) recv_count=0;
    return;
  }
#endif
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
        for (int i = 0; i < MAX_PLAYERS; i ++) if (plates_player_active(&player_data[i])) {
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

        handlePlayerMovement(client_fd, length, packet_id);
        if (recv_count <= 0) return;
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
      if (state == STATE_PLAY && cs_useItem(client_fd,length)) { recv_count = 0; return; }
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
