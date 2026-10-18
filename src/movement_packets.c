/* Player position, rotation, movement flags and arm animation codecs. */
#include "procedures.h"
#include "packets.h"
#include "tools.h"
#include "plates.h"
#include "registries.h"
#include "varnum.h"
#include "commands.h"

/* Correct the local player using absolute coordinates/degree angles, zero
 * velocity and the fixed teleport ID -1. This does not mutate saved position. */
int sc_synchronizePlayerPosition (int client_fd, double x, double y, double z, float yaw, float pitch) {

  writeVarInt(client_fd, 61 + sizeVarInt(-1));
  writeByte(client_fd, 0x41);

  // Teleport ID
  writeVarInt(client_fd, -1);

  // Position
  writeDouble(client_fd, x);
  writeDouble(client_fd, y);
  writeDouble(client_fd, z);

  // Velocity
  writeDouble(client_fd, 0);
  writeDouble(client_fd, 0);
  writeDouble(client_fd, 0);

  // Angles (Yaw/Pitch)
  writeFloat(client_fd, yaw);
  writeFloat(client_fd, pitch);

  // Flags
  writeUint32(client_fd, 0);

  return 0;

}

/* Set the client's spawn marker, not the player's current position. Coordinates
 * are packed as 26-bit X/Z and 12-bit Y; callers provide representable values. */
int sc_setDefaultSpawnPosition (int client_fd, int64_t x, int64_t y, int64_t z) {

  writeVarInt(client_fd, sizeVarInt(0x5A) + 12);
  writeVarInt(client_fd, 0x5A);

  writeUint64(client_fd, (((uint64_t)x & 0x3FFFFFFu) << 38) | (((uint64_t)z & 0x3FFFFFFu) << 12) | ((uint64_t)y & 0xFFFu));
  writeFloat(client_fd, 0);

  return 0;
}

/* flags is the protocol ability mask; walking/flying speeds stay fixed here. */
int sc_playerAbilities (int client_fd, uint8_t flags) {

  writeVarInt(client_fd, 10);
  writeByte(client_fd, 0x39);

  writeByte(client_fd, flags);
  writeFloat(client_fd, 0.05f);
  writeFloat(client_fd, 0.1f);

  return 0;
}

/* These movement codecs decode wire fields only; handlePlayerMovement owns
 * packet-length, finite-number and world-boundary validation. */
/* Decode position and degree angles; retain only bit 0 of movement flags. */
int cs_setPlayerPositionAndRotation (int client_fd, double *x, double *y, double *z, float *yaw, float *pitch, uint8_t *on_ground) {

  *x = readDouble(client_fd);
  *y = readDouble(client_fd);
  *z = readDouble(client_fd);

  *yaw = readFloat(client_fd);
  *pitch = readFloat(client_fd);

  *on_ground = readByte(client_fd) & 0x01;

  return 0;
}

/* Position-only form; rotation remains the movement handler's saved value. */
int cs_setPlayerPosition (int client_fd, double *x, double *y, double *z, uint8_t *on_ground) {

  *x = readDouble(client_fd);
  *y = readDouble(client_fd);
  *z = readDouble(client_fd);

  *on_ground = readByte(client_fd) & 0x01;

  return 0;
}

/* Rotation-only form; decoding must not trigger position/chunk updates. */
int cs_setPlayerRotation (int client_fd, float *yaw, float *pitch, uint8_t *on_ground) {

  *yaw = readFloat(client_fd);
  *pitch = readFloat(client_fd);

  *on_ground = readByte(client_fd) & 0x01;

  return 0;
}

/* Decode ground state and refresh peer metadata from existing player flags;
 * sneak/sprint state itself arrives through the input/command handlers. */
int cs_setPlayerMovementFlags (int client_fd, uint8_t *on_ground) {

  *on_ground = readByte(client_fd) & 0x01;

  PlayerData *player;
  if (!getPlayerData(client_fd, &player))
    broadcastPlayerMetadata(player);

  return 0;
}

/* Translate main/off-hand input to animation IDs and relay to ready peers.
 * Unsupported hands return 1; swinging alone does not apply combat damage. */
int cs_swingArm (int client_fd) {

  PlayerData *player;
  if (getPlayerData(client_fd, &player)) return 1;

  uint8_t hand = readVarInt(client_fd);

  uint8_t animation = 255;
  switch (hand) {
    case 0: {
      animation = 0;
      break;
    }
    case 1: {
      animation = 2;
      break;
    }
  }

  if (animation == 255)
    return 1;

  // Forward animation to all connected players
  for (int i = 0; i < MAX_PLAYERS; i ++) {
    PlayerData* other_player = &player_data[i];

    if (other_player->client_fd == -1) continue;
    if (other_player->client_fd == player->client_fd) continue;
    if (other_player->flags & 0x20) continue;

    sc_entityAnimation(other_player->client_fd, player->client_fd, animation);
  }

  return 0;
}

/* Map only the sneaking bit into compact player flags, then publish pose.
 * Position and other input bits are handled elsewhere or ignored. */
int cs_playerInput (int client_fd) {

  uint8_t flags = readByte(client_fd);

  PlayerData *player;
  if (getPlayerData(client_fd, &player)) return 1;

  // Set or clear sneaking flag
  if (flags & 0x20) player->flags |= 0x04;
  else player->flags &= ~0x04;

  broadcastPlayerMetadata(player);

  return 0;
}

/* Trust the connection's player identity, not the packet entity ID; actions
 * 1/2 toggle sprinting and all other actions leave it unchanged. */
int cs_playerCommand (int client_fd) {

  readVarInt(client_fd); // Ignore entity ID
  uint8_t action = readByte(client_fd);
  readVarInt(client_fd); // Ignore "Jump Boost" value

  PlayerData *player;
  if (getPlayerData(client_fd, &player)) return 1;

  // Handle sprinting
  if (action == 1) player->flags |= 0x08;
  else if (action == 2) player->flags &= ~0x08;

  broadcastPlayerMetadata(player);

  return 0;
}
