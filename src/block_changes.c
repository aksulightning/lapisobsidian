/* Apply authoritative block edits and persist their side effects. */
#include "musicbox.h"
#include "procedures.h"
#include "packets.h"
#include "tools.h"
#include "plates.h"
#include "registries.h"
#include "worldgen.h"
#include "fluids.h"
#include "farming.h"
#include "circuits.h"
#include "doors.h"
#include "signs.h"
#include "serialize.h"
#include <string.h>

uint8_t getBlockChange (short x, uint8_t y, short z) {
  for (int i = 0; i < block_changes_count; i ++) {
    if (block_changes[i].block == 0xFF) continue;
    if (
      block_changes[i].x == x &&
      block_changes[i].y == y &&
      block_changes[i].z == z
    ) return block_changes[i].block;
    #ifdef ALLOW_CHESTS
      // Skip chest contents
      if (block_changes[i].block == B_chest) i += 14;
    #endif
  }
  return 0xFF;
}

// Handle running out of memory for new block changes
void failBlockChange (short x, uint8_t y, short z, uint8_t block) {
  (void)block;

  // Get previous block at this location
  uint8_t before = getBlockAt(x, y, z);

  // Broadcast a new update to all players
  for (int i = 0; i < MAX_PLAYERS; i ++) if (plates_player_active(&player_data[i])) {
    if (player_data[i].client_fd == -1) continue;
    if (player_data[i].flags & 0x20) continue;
    // Reset the block they tried to change
    sc_blockUpdate(player_data[i].client_fd, x, y, z, before);
    // Broadcast a chat message warning about the limit
    sc_systemChat(player_data[i].client_fd, "Block changes limit exceeded. Restore original terrain to continue.", 67);
  }

}

uint8_t makeBlockChange (short x, uint8_t y, short z, uint8_t block) {

  // Transmit block update to all in-game clients
  for (int i = 0; i < MAX_PLAYERS; i ++) if (plates_player_active(&player_data[i])) {
    if (player_data[i].client_fd == -1) continue;
    if (player_data[i].flags & 0x20) continue;
    sc_blockUpdate(player_data[i].client_fd, x, y, z, block);
  }

  // Calculate terrain at these coordinates and compare it to the input block.
  // Since block changes get overlayed on top of terrain, we don't want to
  // store blocks that don't differ from the base terrain.
  ChunkAnchor anchor = {
    .x = x / CHUNK_SIZE,
    .z = z / CHUNK_SIZE
  };
  if (x % CHUNK_SIZE < 0) anchor.x --;
  if (z % CHUNK_SIZE < 0) anchor.z --;
  anchor.hash = getChunkHash(anchor.x, anchor.z);
  anchor.biome = getChunkBiome(anchor.x, anchor.z);

  uint8_t is_base_block = block == getTerrainAt(x, y, z, anchor);

  // In the block_changes array, 0xFF indicates a missing/restored entry.
  // We track the position of the first such "gap" for when the operation
  // isn't replacing an existing block change.
  int first_gap = block_changes_count;

  // Prioritize replacing entries with matching coordinates
  // This prevents having conflicting entries for one set of coordinates
  for (int i = 0; i < block_changes_count; i ++) {
    if (block_changes[i].block == 0xFF) {
      if (first_gap == block_changes_count) first_gap = i;
      continue;
    }
    if (
      block_changes[i].x == x &&
      block_changes[i].y == y &&
      block_changes[i].z == z
    ) {
      #ifdef ALLOW_CHESTS
      // When replacing chests, clear following 14 entries too (item data)
      if (block_changes[i].block == B_chest) {
        for (int j = 1; j < 15; j ++) block_changes[i + j].block = 0xFF;
      }
      #endif
      if (is_base_block) block_changes[i].block = 0xFF;
      else {
        #ifdef ALLOW_CHESTS
        // When placing chests, just unallocate the target block and fall
        // through to the chest-specific routine below.
        if (block == B_chest) {
          block_changes[i].block = 0xFF;
          if (first_gap > i) first_gap = i;
          #ifndef DISK_SYNC_BLOCKS_ON_INTERVAL
          writeBlockChangesToDisk(i, i);
          #endif
          break;
        }
        #endif
        block_changes[i].block = block;
      }
      #ifndef DISK_SYNC_BLOCKS_ON_INTERVAL
      writeBlockChangesToDisk(i, i);
      #endif
      signs_block_changed(x,y,z,block); doors_block_changed(x,y,z); circuits_block_changed(x,y,z,block); musicbox_block_changed(x,y,z); farming_block_changed(x,y,z); fluids_block_changed(x,y,z); return 0;
    }
    #ifdef ALLOW_CHESTS
    if (block_changes[i].block == B_chest) i += 14;
    #endif
  }

  // Don't create a new entry if it contains the base terrain block
  if (is_base_block) { signs_block_changed(x,y,z,block); doors_block_changed(x,y,z); circuits_block_changed(x,y,z,block); musicbox_block_changed(x,y,z); farming_block_changed(x,y,z); fluids_block_changed(x,y,z); return 0; }

  #ifdef ALLOW_CHESTS
  if (block == B_chest) {
    // Chests require 15 entries total, so for maximum space-efficiency,
    // we have to find a continuous gap that's at least 15 slots wide.
    // By design, this loop also continues past the current search range,
    // which naturally appends the chest to the end if a gap isn't found.
    int last_real_entry = first_gap - 1;
    for (int i = first_gap; i <= block_changes_count + 15; i ++) {
      if (i >= MAX_BLOCK_CHANGES) break; // No more space, trigger failBlockChange

      if (block_changes[i].block != 0xFF) {
        last_real_entry = i;
        continue;
      }
      if (i - last_real_entry != 15) continue;
      // A wide enough gap has been found, assign the chest
      block_changes[last_real_entry + 1].x = x;
      block_changes[last_real_entry + 1].y = y;
      block_changes[last_real_entry + 1].z = z;
      block_changes[last_real_entry + 1].block = block;
      // Zero out the following 14 entries for item data
      for (int i = 2; i <= 15; i ++) {
        block_changes[last_real_entry + i].x = 0;
        block_changes[last_real_entry + i].y = 0;
        block_changes[last_real_entry + i].z = 0;
        block_changes[last_real_entry + i].block = 0;
      }
      // Extend future search range if necessary
      if (i >= block_changes_count) {
        block_changes_count = i + 1;
      }
      // Write changes to disk (if applicable)
      #ifndef DISK_SYNC_BLOCKS_ON_INTERVAL
      writeBlockChangesToDisk(last_real_entry + 1, last_real_entry + 15);
      #endif
      signs_block_changed(x,y,z,block); doors_block_changed(x,y,z); circuits_block_changed(x,y,z,block); musicbox_block_changed(x,y,z); farming_block_changed(x,y,z); fluids_block_changed(x,y,z); return 0;
    }
    // If we're here, no changes were made
    failBlockChange(x, y, z, block);
    return 1;
  }
  #endif

  // Handle running out of memory for new block changes
  if (first_gap == MAX_BLOCK_CHANGES) {
    failBlockChange(x, y, z, block);
    return 1;
  }

  // Fall back to storing the change at the first possible gap
  block_changes[first_gap].x = x;
  block_changes[first_gap].y = y;
  block_changes[first_gap].z = z;
  block_changes[first_gap].block = block;
  // Write change to disk (if applicable)
  #ifndef DISK_SYNC_BLOCKS_ON_INTERVAL
  writeBlockChangesToDisk(first_gap, first_gap);
  #endif
  // Extend future search range if we've appended to the end
  if (first_gap == block_changes_count) {
    block_changes_count ++;
  }

  signs_block_changed(x,y,z,block); doors_block_changed(x,y,z); circuits_block_changed(x,y,z,block); musicbox_block_changed(x,y,z); farming_block_changed(x,y,z); fluids_block_changed(x,y,z); return 0;
}

// Returns the result of mining a block, taking into account the block type and tools
// Probability numbers obtained with this formula: N = floor(P * (2 ^ 32))
