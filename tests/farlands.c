#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "farlands.h"
#include "world_border.h"
#include "registries.h"

static Beta173Chunk chunk, repeat, base;
static uint32_t hash (const Beta173Chunk *c) {
  uint32_t h = 2166136261u;
  for (unsigned i = 0; i < sizeof(c->blocks); i++) h = (h ^ c->blocks[i])*16777619u;
  return h;
}
int main (void) {
  const int points[][2] = {{246,0},{-247,0},{0,246},{0,-247},{246,246},{-247,-247},{254,-254}};
  uint32_t hashes[7]; unsigned high_stone = 0, high_air = 0, ledges = 0;
  for (unsigned k = 0; k < 7; k++) {
    assert(beta173_generate_chunk(1,points[k][0],points[k][1],BETA_DECORATION,&base));
    chunk = base; farlands_apply(1,false,&chunk); hashes[k] = hash(&chunk);
    for (int x = 0; x < 16; x++) for (int z = 0; z < 16; z++) {
      int wx = chunk.cx*16+x, wz = chunk.cz*16+z;
      bool outside = wx <= -3940 || wx >= 3940 || wz <= -3940 || wz >= 3940;
      unsigned col = (unsigned)(x*16+z)*128;
      if (!outside) assert(!memcmp(chunk.blocks+col,base.blocks+col,128));
      assert(!memcmp(chunk.blocks+col,base.blocks+col,16));
      for (unsigned y = 80; y < 124; y++) {
        high_stone += chunk.blocks[col+y] == B_stone;
        high_air += chunk.blocks[col+y] == B_air;
        ledges += chunk.blocks[col+y] == B_grass_block;
      }
    }
    assert(hash(&chunk) != hash(&base));
  }
  assert(high_stone && high_air && ledges);
  assert(hashes[0] == UINT32_C(3367228003)); /* Seed 1, chunk 246,0, terrain v3. */
  for (unsigned k = 7; k-- > 0;) {
    assert(beta173_generate_chunk(1,points[k][0],points[k][1],BETA_DECORATION,&repeat));
    farlands_apply(1,false,&repeat); assert(hash(&repeat) == hashes[k]);
  }
  assert(beta173_generate_chunk(1,0,0,BETA_DECORATION,&chunk)); repeat = chunk;
  farlands_apply(1,false,&chunk); assert(!memcmp(&chunk,&repeat,sizeof(chunk)));
  /* Mirroring changes samples, while physical onset remains exactly +/-3940. */
  memset(&chunk,0,sizeof(chunk)); chunk.cx = 246; repeat = chunk;
  farlands_apply(1,true,&chunk); farlands_apply(2,true,&repeat);
  assert(hash(&chunk) != hash(&repeat));
  for (unsigned x = 0; x < 4; x++) for (unsigned z = 0; z < 16; z++)
    for (unsigned y = 0; y < 128; y++) assert(chunk.blocks[(x*16+z)*128+y] == B_air);
  printf("Far Lands: onset, unchanged interior/deep strata, seed/order determinism, ledges/holes and mirroring passed (vector %u)\n",hashes[0]);
  return 0;
}
