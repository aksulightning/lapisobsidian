/* Capture the production serializer's transport output, then decode independently. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "globals.h"
#include "packets.h"
#include "registry.h"
#include "registries.h"
#include "worldgen.h"
#include "beta173_worldgen.h"
#include "beta173_features.h"
static Beta173Chunk expected_chunk;

static uint8_t wire[200000];
static size_t written, cursor;
ssize_t send_all (int fd, const void *data, ssize_t length) {
  assert(length >= 0);
  size_t size = (size_t)length;
  (void)fd;
  assert(size <= sizeof(wire) - written);
  memcpy(wire + written, data, size); written += size;
  return (ssize_t)size;
}
ssize_t writeByte (int fd, uint8_t value) { return send_all(fd, &value, 1); }
ssize_t writeUint16 (int fd, uint16_t value) {
  uint8_t b[2] = {(uint8_t)(value >> 8), (uint8_t)value}; return send_all(fd, b, 2);
}
ssize_t writeUint32 (int fd, uint32_t value) {
  uint8_t b[4]; for (unsigned i = 0; i < 4; i ++) b[i] = (uint8_t)(value >> (24-8*i));
  return send_all(fd, b, 4);
}
ssize_t writeUint64 (int fd, uint64_t value) {
  uint8_t b[8]; for (unsigned i = 0; i < 8; i ++) b[i] = (uint8_t)(value >> (56-8*i));
  return send_all(fd, b, 8);
}
static unsigned byte (void) { assert(cursor < written); return wire[cursor ++]; }
static uint32_t varint (void) {
  uint32_t value = 0;
  for (unsigned shift = 0; shift < 35; shift += 7) {
    unsigned b = byte(); assert(shift < 28 || !(b & 240u));
    value |= (b & 127u) << shift;
    if (!(b & 128u)) return value;
  }
  assert(0); return 0;
}
static uint16_t u16 (void) { unsigned hi = byte(); return (uint16_t)(hi*256 + byte()); }
static void string (const char *expected) {
  size_t size = varint(); assert(size == strlen(expected) && size <= written-cursor);
  assert(memcmp(wire+cursor, expected, size) == 0); cursor += size;
}
int main (void) {
  world_seed = UINT64_C(1);
  for (int i = 0; i < MAX_BLOCK_CHANGES; i ++) block_changes[i].block = 0xff;
  block_changes_count = 1;
  block_changes[0] = (BlockChange){.x=2,.y=80,.z=3,.block=B_obsidian};
  assert(beta173_generate_chunk(world_seed,0,0,BETA_DECORATION,&expected_chunk));
  assert(sc_chunkDataAndUpdateLight(0, 0, 0) == 0);
  size_t size = varint(); assert(size == written-cursor);
  assert(varint() == 0x27);
  for (unsigned i = 0; i < 8; i ++) assert(byte() == 0); // chunk coordinates
  assert(varint() == 0); // heightmaps
  size_t data_size = varint(), start = cursor;
  for (unsigned section = 0; section < 24; section ++) {
    unsigned non_air = u16();
    assert(non_air <= 4096);
    unsigned bits = byte();
    if (section < 4) { assert(non_air == 4096 && bits == 0 && varint() == 85); }
    else {
      assert(bits == 8 && varint() == 256);
      for (unsigned id = 0; id < 256; id ++) {
        uint16_t state; assert(registry_block_state(id, &state)); assert(varint() == state);
      }
      unsigned expected_count = 0;
      for (unsigned index = 0; index < 4096; index ++) {
        unsigned local = index ^ 7u;
        int x = (int)(local % 16), z = (int)((local / 16) % 16);
        int y = (int)((section-4)*16 + local/256);
        unsigned expected = x == 2 && y == 80 && z == 3 ? B_obsidian : y < 128 ? expected_chunk.blocks[(x*16+z)*128+y] : B_air;
        assert(byte() == expected);
        if (expected != B_air) expected_count ++;
      }
      assert(non_air == expected_count);
    }
    assert(byte() == 0); assert(registry_biome_by_id(varint()));
  }
  assert(cursor-start == data_size);
  assert(varint() == 0); // block entities
  assert(varint() == 1); cursor += 8; // sky mask
  assert(varint() == 0 && varint() == 0 && varint() == 0);
  assert(varint() == 26);
  for (unsigned i = 0; i < 26; i ++) { assert(varint() == 2048); cursor += 2048; assert(cursor <= written); }
  assert(varint() == 0 && cursor == written);

  written = cursor = 0;
  sc_registries(0);
  const char *registries[] = {"cat_variant","chicken_variant","cow_variant","frog_variant",
    "painting_variant","pig_variant","wolf_sound_variant","wolf_variant","damage_type",
    "worldgen/biome","dimension_type"};
  for (size_t i = 0; i < sizeof(registries)/sizeof(registries[0]); i ++) {
    size_t len = varint(), end = cursor + len;
    assert(varint() == 7); string(registries[i]);
    unsigned count = varint(); assert(count > 0);
    for (unsigned j = 0; j < count; j ++) {
      if (i == 9) string(registry_biome_by_id(j));
      else if (i == 10) string(registry_dimension_by_id(j));
      else { size_t n = varint(); assert(n > 0 && n <= written-cursor); cursor += n; }
      assert(byte() == 0); // known-pack data omitted
    }
    assert(cursor == end);
  }
  size_t tag_length = varint(); assert(cursor + tag_length == written && varint() == 0x0d);
  /* Exercise the four-corner cache, eviction and seed invalidation through
   * production terrain queries; compare whole columns with the phased generator. */
  const int coords[6][2] = {{0,0},{-1,0},{0,-1},{-1,-1},{10,20},{-2048,-2048}};
  for (unsigned pass = 0; pass < 2; pass ++) {
    world_seed = pass == 0 ? 1 : UINT64_MAX;
    for (unsigned k = 0; k < 6; k ++) {
      int cx = coords[k][0], cz = coords[k][1];
      assert(beta173_generate_chunk(world_seed,cx,cz,BETA_DECORATION,&expected_chunk));
      for (int y = 0; y < 128; y ++) {
        assert(getTerrainAt(cx*16,y,cz*16,(ChunkAnchor){0}) == expected_chunk.blocks[y]);
      }
    }
  }
  puts("production chunk packet: framing, 24 sections, palette, edit overlay, biomes and registries passed");
  return 0;
}
