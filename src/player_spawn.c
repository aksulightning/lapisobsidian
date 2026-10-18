/* Player spawn sequence and entity metadata serialization. */
#include "procedures.h"
#include "packets.h"
#include "tools.h"
#include "plates.h"
#include "registries.h"
#include "worldgen.h"
#include "world_border.h"
#include "varnum.h"
#include "commands.h"
#include <stdlib.h>

/* Rebuild the client's world view after login/respawn. Fresh players derive Y
 * from terrain; returning players use saved coordinates. Position is sent both
 * before and after chunk streaming. Readiness is completed by handlePlayerJoin. */
void spawnPlayer (PlayerData *player) {

  world_border_reset(player);
  if (world_border_outside(player->x,player->z)) {
    player->x = player->z = 8;
    player->y = (uint8_t)(getHeightAt(8,8)+1);
    player->grounded_y = player->y;
  }

  // Player spawn coordinates, initialized to placeholders
  float spawn_x = 8.5f, spawn_y = 80.0f, spawn_z = 8.5f;
  float spawn_yaw = 0.0f, spawn_pitch = 0.0f;

  if (player->flags & 0x02) { // Is this a new player?
    // Determine spawning Y coordinate based on terrain height
    spawn_y = getHeightAt(8, 8) + 1;
    player->y = spawn_y;
    player->flags &= ~0x02;
  } else { // Not a new player
    // Calculate spawn position from player data
    spawn_x = (float)player->x + 0.5;
    spawn_y = player->y;
    spawn_z = (float)player->z + 0.5;
    spawn_yaw = player->yaw * 180 / 127;
    spawn_pitch = player->pitch * 90 / 127;
  }

  // Teleport player to spawn coordinates (first pass)
  sc_synchronizePlayerPosition(player->client_fd, spawn_x, spawn_y, spawn_z, spawn_yaw, spawn_pitch);

  task_yield(); // Check task timer between packets

  // Clear crafting grid residue, unlock craft_items
  for (int i = 0; i < 9; i++) {
    player->craft_items[i] = 0;
    player->craft_count[i] = 0;
  }
  player->flags &= ~0x80;

  // Sync client inventory and hotbar
  for (uint8_t i = 0; i < 41; i ++) {
    sc_setContainerSlot(player->client_fd, 0, serverSlotToClientSlot(0, i), player->inventory_count[i], player->inventory_items[i]);
  }
  sc_setHeldItem(player->client_fd, player->hotbar);
  // Sync client health and hunger
  sc_setHealth(player->client_fd, player->health, player->hunger, player->saturation);
  // Sync client clock time
  sc_updateTime(player->client_fd, world_time);

  sc_playerAbilities(player->client_fd,commands_abilities(player));

  // Calculate player's chunk coordinates
  short _x = div_floor(player->x, 16), _z = div_floor(player->z, 16);

  // Indicate that we're about to send chunk data
  sc_setDefaultSpawnPosition(player->client_fd, 8, 80, 8);
  sc_startWaitingForChunks(player->client_fd);
  sc_setCenterChunk(player->client_fd, _x, _z);

  task_yield(); // Check task timer between packets

  // Send spawn chunk first
  sc_chunkDataAndUpdateLight(player->client_fd, _x, _z);
  for (int i = -VIEW_DISTANCE; i <= VIEW_DISTANCE; i ++) {
    for (int j = -VIEW_DISTANCE; j <= VIEW_DISTANCE; j ++) {
      if (i == 0 && j == 0) continue;
      sc_chunkDataAndUpdateLight(player->client_fd, _x + i, _z + j);
    }
  }
  // Re-teleport player after all chunks have been sent
  sc_synchronizePlayerPosition(player->client_fd, spawn_x, spawn_y, spawn_z, spawn_yaw, spawn_pitch);

  task_yield(); // Check task timer between packets

}

/* Translate compact player flags into wire flags/pose for ready peers in the
 * selected Plate. The originating client already owns its local pose. */
void broadcastPlayerMetadata (PlayerData *player) {
  uint8_t sneaking = (player->flags & 0x04) != 0;
  uint8_t sprinting = (player->flags & 0x08) != 0;

  uint8_t entity_bit_mask = 0;
  if (sneaking) entity_bit_mask |= 0x02;
  if (sprinting) entity_bit_mask |= 0x08;

  int pose = 0;
  if (sneaking) pose = 5;

  EntityData metadata[] = {
    {
      0,                   // Index (Entity Bit Mask)
      0,                   // Type (Byte)
      { entity_bit_mask }, // Value
    },
    {
      6,        // Index (Pose),
      21,       // Type (Pose),
      { pose }, // Value (Standing)
    }
  };

  for (int i = 0; i < MAX_PLAYERS; i ++) if (plates_player_active(&player_data[i])) {
    PlayerData* other_player = &player_data[i];
    int client_fd = other_player->client_fd;

    if (client_fd == -1) continue;
    if (client_fd == player->client_fd) continue;
    if (other_player->flags & 0x20) continue;

    sc_setEntityMetadata(client_fd, player->client_fd, metadata, 2);
  }
}

/* Send supported non-default mob metadata (currently sheared sheep). client_fd
 * -1 broadcasts to ready players in this Plate; otherwise send to one viewer. */
void broadcastMobMetadata (int client_fd, int entity_id) {

  if (entity_id > -2 || entity_id < -1-MAX_MOBS) return;
  int mob_index = -entity_id - 2;
  if (mob_index < 0 || mob_index >= MAX_MOBS) return;
  MobData *mob = &mob_data[mob_index];

  EntityData *metadata;
  size_t length;

  switch (mob->type) {
    case 106: // Sheep
      if (!((mob->data >> 5) & 1)) // Don't send metadata if sheep isn't sheared
        return;

      metadata = malloc(sizeof *metadata);
      metadata[0] = (EntityData){
        17,                // Index (Sheep Bit Mask),
        0,                 // Type (Byte),
        { (uint8_t)0x10 }, // Value
      };
      length = 1;

      break;

    default: return;
  }

  if (client_fd == -1) {
    for (int i = 0; i < MAX_PLAYERS; i ++) if (plates_player_active(&player_data[i])) {
      PlayerData* player = &player_data[i];
      client_fd = player->client_fd;

      if (client_fd == -1) continue;
      if (player->flags & 0x20) continue;

      sc_setEntityMetadata(client_fd, entity_id, metadata, length);
    }
  } else {
    sc_setEntityMetadata(client_fd, entity_id, metadata, length);
  }

  free(metadata);
}

/* Encode one entry, without the list terminator. Only byte/pose types exist in
 * this compact model; callers must size/validate before writing the packet.
 * Returns the byte write result, 0 for pose, or -1 for an unsupported type. */
ssize_t writeEntityData (int client_fd, EntityData *data) {
  writeByte(client_fd, data->index);
  writeVarInt(client_fd, data->type);

  switch (data->type) {
    case 0: // Byte
      return writeByte(client_fd, data->value.byte);
    case 21: // Pose
      writeVarInt(client_fd, data->value.pose);
      return 0;

    default: return -1;
  }
}

/* Encoded entry size, including index/type; -1 rejects unsupported value types. */
int sizeEntityData (EntityData *data) {
  int value_size;

  switch (data->type) {
    case 0: // Byte
      value_size = 1;
      break;
    case 21: // Pose
      value_size = sizeVarInt(data->value.pose);
      break;

    default: return -1;
  }

  return 1 + sizeVarInt(data->type) + value_size;
}

/* Sum entry sizes, excluding the final 0xFF. Reject the whole list with -1
 * before sc_setEntityMetadata emits any bytes for an unsupported type. */
int sizeEntityMetadata (EntityData *metadata, size_t length) {
  int total_size = 0;
  for (size_t i = 0; i < length; i ++) {
    int size = sizeEntityData(&metadata[i]);
    if (size == -1) return -1;
    total_size += size;
  }
  return total_size;
}
