#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "world_border.h"
#include "packets.h"
#include "worldgen.h"

static unsigned teleports, messages, chunks, centers;
static int sent[4096][2];
uint8_t getHeightAt (int x, int z) { assert(x == 8 && z == 8); return 70; }
int sc_setCenterChunk (int fd, int x, int z) { (void)fd; (void)x; (void)z; centers++; return 0; }
int sc_chunkDataAndUpdateLight (int fd, int x, int z) {
  (void)fd; assert(chunks < 4096); sent[chunks][0] = x; sent[chunks++][1] = z; return 0;
}
int sc_synchronizePlayerPosition (int fd, double x, double y, double z, float yaw, float pitch) {
  (void)fd; (void)yaw; (void)pitch; assert(x == 8.5 && y == 71 && z == 8.5); teleports++; return 0;
}
int sc_teleportEntity (int fd, int id, double x, double y, double z, float yaw, float pitch) {
  (void)fd; (void)id; (void)x; (void)y; (void)z; (void)yaw; (void)pitch; return 0;
}
int sc_systemChat (int fd, char *text, uint16_t n) {
  (void)fd; assert(n && strstr(text,"Returned to spawn")); messages++; return 0;
}
static void view (int cx, int cz, int ox, int oz, bool full) {
  chunks = centers = 0; world_send_view(1,cx,cz,ox,oz,full);
  assert(centers == 1);
  unsigned expected = 0;
  for (int x = cx-VIEW_DISTANCE; x <= cx+VIEW_DISTANCE; x++) {
    for (int z = cz-VIEW_DISTANCE; z <= cz+VIEW_DISTANCE; z++) {
      bool needed = full || x < ox-VIEW_DISTANCE || x > ox+VIEW_DISTANCE || z < oz-VIEW_DISTANCE || z > oz+VIEW_DISTANCE;
      unsigned found = 0;
      for (unsigned i = 0; i < chunks; i++) if (sent[i][0] == x && sent[i][1] == z) found++;
      assert(found == (unsigned)needed); expected += needed ? 1u : 0u;
    }
  }
  assert(chunks == expected);
}
int main (void) {
  for (int i = 0; i < MAX_PLAYERS; i++) player_data[i].client_fd = -1;
  PlayerData *p = &player_data[0]; p->client_fd = 10; p->health = 20;
  assert(!world_border_outside(4067.99,-4067.99));
  assert(world_border_outside(NAN,0) && world_border_outside(0,INFINITY));
  const double edges[8][2] = {{4068,0},{-4068,0},{0,4068},{0,-4068},{4068,4068},{-4068,-4068},{9999,0},{0,-9999}};
  for (unsigned i = 0; i < 8; i++) {
    chunks = 0; world_border_reset(p); p->x = 4000; p->y = 90; p->z = 0; p->grounded_y = 110;
    assert(world_border_guard(p,edges[i][0],edges[i][1]));
    assert(p->x == 8 && p->y == 71 && p->z == 8 && p->grounded_y == 71 && p->health == 20);
    assert(teleports == i+1 && messages == i+1);
    for (unsigned j = 0; j < VISITED_HISTORY; j++) assert(p->visited_x[j] == 32767 && p->visited_z[j] == 32767);
    unsigned before = chunks;
    assert(world_border_guard(p,edges[i][0],edges[i][1]));
    assert(world_border_guard(p,4000,0)); /* Queued position inside the old region. */
    assert(chunks == before && teleports == i+1);
    assert(!world_border_guard(p,8.5,8.5));
    assert(!world_border_guard(p,4067.5,0));
  }
  view(0,0,0,0,true); view(0,0,0,0,false);
  view(1,0,0,0,false); view(0,0,1,0,false); /* Revisited chunk still sends its edge. */
  view(3,-2,0,0,false); view(-3,2,0,0,false);
  view(254,254,-254,-254,false); /* Large jump is bounded to the new view. */
  view(625,625,624,625,false); /* Reported 9999/10000 area. */
  puts("world border: four edges/corners, spawn return, queued movement, view differences and bounded jumps passed");
  return 0;
}
