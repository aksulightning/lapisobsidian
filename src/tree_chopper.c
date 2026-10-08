#include "optional_features.h"
#if LAPIS_TREE_CHOPPER == 1
#include "commands.h"
#include "items.h"
#include "packets.h"
#include "procedures.h"
#include "registries.h"
#include "world_border.h"
#include "worldgen.h"

#define TREE_LOG_LIMIT 64
static bool axe (const PlayerData *player) {
  if (player->hotbar >= 41 || !player->inventory_count[player->hotbar]) return false;
  switch (player->inventory_items[player->hotbar]) {
    case I_wooden_axe: case I_stone_axe: case I_iron_axe:
    case I_golden_axe: case I_diamond_axe: case I_netherite_axe: return true;
    default: return false;
  }
}
bool tree_chopper_try (PlayerData *player, int x, int y, int z) {
  if (!player || !player->health || (player->flags & 0x26) ||
      commands_gamemode(player) != 0 || !axe(player) ||
      y < 1 || y > 255 || world_border_outside(x,z) || getBlockAt(x,y,z) != B_oak_log) return false;
  uint8_t ground = getBlockAt(x,y-1,z);
  if (ground != B_dirt && ground != B_grass_block && ground != B_podzol) return false;
  /* Bounded, face-connected oak trunks. Require foliage to avoid chopping
   * ordinary log pillars; this is a heuristic, not tree provenance tracking. */
  int logs[TREE_LOG_LIMIT][3] = {{x,y,z}};
  unsigned count = 1; bool leaves = false;
  static const int offsets[6][3] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
  for (unsigned at = 0; at < count; at++) {
    for (unsigned d = 0; d < 6; d++) {
      int nx = logs[at][0]+offsets[d][0], ny = logs[at][1]+offsets[d][1], nz = logs[at][2]+offsets[d][2];
      if (ny < y || ny > 255 || world_border_outside(nx,nz)) continue;
      uint8_t block = getBlockAt(nx,ny,nz);
      if (block == B_oak_leaves) leaves = true;
      if (block != B_oak_log) continue;
      bool seen = false;
      for (unsigned j = 0; j < count; j++)
        if (logs[j][0] == nx && logs[j][1] == ny && logs[j][2] == nz) { seen = true; break; }
      if (seen) continue;
      if (count == TREE_LOG_LIMIT) return false;
      logs[count][0] = nx; logs[count][1] = ny; logs[count][2] = nz; count++;
    }
  }
  if (!leaves) return false;
  for (unsigned i = 0; i < count && axe(player); i++) {
    int lx = logs[i][0], ly = logs[i][1], lz = logs[i][2];
    if (!items_can_spawn(I_oak_log,1,lx,ly,lz)) {
      sc_blockUpdate(player->client_fd,(short)lx,(uint8_t)ly,(short)lz,B_oak_log);
      break;
    }
    if (makeBlockChange((short)lx,(uint8_t)ly,(short)lz,B_air)) break;
    items_spawn(I_oak_log,1,lx,ly,lz,500);
    bumpToolDurability(player);
  }
  return true;
}
#endif
