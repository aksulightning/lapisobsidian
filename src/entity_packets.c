/* Entity spawning, metadata, animation and movement packet codecs. */
#include "procedures.h"
#include "packets.h"
#include "tools.h"
#include "plates.h"
#include "registries.h"
#include "varnum.h"

/* Register identity and current game mode before spawning the player entity.
 * No skin/profile properties are supplied by this compact packet. */
int sc_playerInfoUpdateAddPlayer (int client_fd, PlayerData player) {

  writeVarInt(client_fd, 22 + player_name_length(player.name)); // Packet length
  writeByte(client_fd, 0x3F); // Packet ID

  writeByte(client_fd, 0x05); // Add Player + Update Game Mode
  writeByte(client_fd, 1); // Player count (1 per packet)

  // Player UUID
  send_all(client_fd, player.uuid, 16);
  // Player name
  writeByte(client_fd, player_name_length(player.name));
  send_all(client_fd, player.name, player_name_length(player.name));
  // Properties (don't send any)
  writeByte(client_fd, 0);

  writeVarInt(client_fd,commands_mode_for_fd(player.client_fd));
  return 0;
}

/* Spawn a caller-owned protocol entity ID/UUID with zero velocity and data.
 * yaw/pitch are protocol angle bytes, unlike sc_teleportEntity's degrees. */
int sc_spawnEntity (
  int client_fd,
  int id, uint8_t *uuid, int type,
  double x, double y, double z,
  uint8_t yaw, uint8_t pitch
) {

  writeVarInt(client_fd, 51 + sizeVarInt(id) + sizeVarInt(type));
  writeByte(client_fd, 0x01);

  writeVarInt(client_fd, id); // Entity ID
  send_all(client_fd, uuid, 16); // Entity UUID
  writeVarInt(client_fd, type); // Entity type

  // Position
  writeDouble(client_fd, x);
  writeDouble(client_fd, y);
  writeDouble(client_fd, z);

  // Angles
  writeByte(client_fd, pitch);
  writeByte(client_fd, yaw);
  writeByte(client_fd, yaw);

  // Data - mostly unused
  writeByte(client_fd, 0);

  // Velocity
  writeUint16(client_fd, 0);
  writeUint16(client_fd, 0);
  writeUint16(client_fd, 0);

  return 0;
}

/* Validate/size the entire metadata list before emitting its frame; unsupported
 * types return 1 without a partial packet. Append the required 0xFF terminator. */
int sc_setEntityMetadata (int client_fd, int id, EntityData *metadata, size_t length) {
  int entity_metadata_size = sizeEntityMetadata(metadata, length);
  if (entity_metadata_size == -1) return 1;

  writeVarInt(client_fd, 2 + sizeVarInt(id) + entity_metadata_size);
  writeByte(client_fd, 0x5C);

  writeVarInt(client_fd, id); // Entity ID

  for (size_t i = 0; i < length; i ++) {
    EntityData *data = &metadata[i];
    writeEntityData(client_fd, data);
  }

  writeByte(client_fd, 0xFF); // End

  return 0;
}

/* Adapt compact saved player coordinates and angles to a player spawn packet.
 * The connection descriptor is also this player's protocol entity ID. */
int sc_spawnEntityPlayer (int client_fd, PlayerData player) {
  return sc_spawnEntity(
    client_fd,
    player.client_fd, player.uuid, 149,
    player.x > 0 ? (double)player.x + 0.5 : (double)player.x - 0.5,
    player.y,
    player.z > 0 ? (double)player.z + 0.5 : (float)player.z - 0.5,
    player.yaw, player.pitch
  );
}

/* Relay a protocol animation ID; visual feedback does not mutate game state. */
int sc_entityAnimation (int client_fd, int id, uint8_t animation) {
  writeVarInt(client_fd, 2 + sizeVarInt(id));
  writeByte(client_fd, 0x02);

  writeVarInt(client_fd, id); // Entity ID
  writeByte(client_fd, animation); // Animation

  return 0;
}

/* Absolute update for other entities: angles are degrees, velocity is zero and
 * the wire on-ground flag is always set. Local-player correction uses 0x41. */
int sc_teleportEntity (
  int client_fd, int id,
  double x, double y, double z,
  float yaw, float pitch
) {

  // Packet length and ID
  writeVarInt(client_fd, 58 + sizeVarInt(id));
  writeByte(client_fd, 0x1F);

  // Entity ID
  writeVarInt(client_fd, id);
  // Position
  writeDouble(client_fd, x);
  writeDouble(client_fd, y);
  writeDouble(client_fd, z);
  // Velocity
  writeUint64(client_fd, 0);
  writeUint64(client_fd, 0);
  writeUint64(client_fd, 0);
  // Angles
  writeFloat(client_fd, yaw);
  writeFloat(client_fd, pitch);
  // On ground flag
  writeByte(client_fd, 1);

  return 0;
}

/* Head direction is separate from body rotation; yaw is a protocol angle byte. */
int sc_setHeadRotation (int client_fd, int id, uint8_t yaw) {

  // Packet length and ID
  writeByte(client_fd, 2 + sizeVarInt(id));
  writeByte(client_fd, 0x4C);
  // Entity ID
  writeVarInt(client_fd, id);
  // Head yaw
  writeByte(client_fd, yaw);

  return 0;
}

/* Body-only rotation in protocol angle bytes; position is unchanged and the
 * wire on-ground flag is always set. Send head rotation separately if needed. */
int sc_updateEntityRotation (int client_fd, int id, uint8_t yaw, uint8_t pitch) {

  // Packet length and ID
  writeByte(client_fd, 4 + sizeVarInt(id));
  writeByte(client_fd, 0x31);
  // Entity ID
  writeVarInt(client_fd, id);
  // Angles
  writeByte(client_fd, yaw);
  writeByte(client_fd, pitch);
  // "On ground" flag
  writeByte(client_fd, 1);

  return 0;
}

/* type is a damage-registry ID. Omit source entity/position; health changes are
 * authoritative gameplay work and are not performed by this visual event. */
int sc_damageEvent (int client_fd, int entity_id, int type) {

  writeVarInt(client_fd, 4 + sizeVarInt(entity_id) + sizeVarInt(type));
  writeByte(client_fd, 0x19);

  writeVarInt(client_fd, entity_id);
  writeVarInt(client_fd, type);
  writeByte(client_fd, 0);
  writeByte(client_fd, 0);
  writeByte(client_fd, false);

  return 0;
}

/* Entity status events use a fixed-width entity ID, unlike most entity packets.
 * status is the protocol event code (for example death or item-break feedback). */
int sc_entityEvent (int client_fd, int entity_id, uint8_t status) {

  writeVarInt(client_fd, 6);
  writeByte(client_fd, 0x1E);

  writeUint32(client_fd, entity_id);
  writeByte(client_fd, status);

  return 0;
}

/* Remove one entity from this viewer only; lifetime/storage belongs to gameplay. */
int sc_removeEntity (int client_fd, int entity_id) {

  writeVarInt(client_fd, 2 + sizeVarInt(entity_id));
  writeByte(client_fd, 0x46);

  writeByte(client_fd, 1);
  writeVarInt(client_fd, entity_id);

  return 0;
}

/* Animate count items moving from collected to collector; this neither removes
 * the entity nor inserts inventory, so callers must synchronize remaining state. */
int sc_pickupItem (int client_fd, int collected, int collector, uint8_t count) {

  writeVarInt(client_fd, 1 + sizeVarInt(collected) + sizeVarInt(collector) + sizeVarInt(count));
  writeByte(client_fd, 0x75);

  writeVarInt(client_fd, collected);
  writeVarInt(client_fd, collector);
  writeVarInt(client_fd, count);

  return 0;
}
