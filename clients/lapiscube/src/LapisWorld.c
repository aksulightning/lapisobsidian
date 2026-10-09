#include "LapisWorld.h"
#include <string.h>

/* Bounded moving cache; no client terrain generation and no unbounded map. */
void LapisWorld_Init(struct LapisWorld* w) { memset(w, 0, sizeof(*w)); }
int LapisWorld_ChunkCoord(int block) { return block >= 0 ? block / 16 : (block + 1) / 16 - 1; }
void LapisWorld_Position(cc_uint64 v, int* x, int* y, int* z) {
    *x = (int)(v >> 38); *z = (int)((v >> 12) & 0x3FFFFFF); *y = (int)(v & 4095);
    if (*x & 0x2000000) *x -= 0x4000000;
    if (*z & 0x2000000) *z -= 0x4000000;
    if (*y & 2048) *y -= 4096;
}
static int InRange(struct LapisWorld* w, int x, int z) {
    return !w->hasCenter || (x >= w->centerX - 3 && x <= w->centerX + 3 &&
                             z >= w->centerZ - 3 && z <= w->centerZ + 3);
}
int LapisWorld_Center(struct LapisWorld* w, int x, int z) {
    int i, changed;
    if (x < -2048 || x > 2047 || z < -2048 || z > 2047) return -1;
    changed = !w->hasCenter || w->centerX != x || w->centerZ != z;
    w->centerX = x; w->centerZ = z; w->hasCenter = 1;
    for (i = 0; i < LAPIS_CACHE_COUNT; i++) {
        struct LapisChunk* c = &w->chunks[i];
        if (c->valid && !InRange(w, c->x, c->z)) { c->valid = 0; w->evicted++; }
    }
    return changed;
}
struct LapisChunk* LapisWorld_Find(struct LapisWorld* w, int x, int z) {
    int i;
    for (i = 0; i < LAPIS_CACHE_COUNT; i++)
        if (w->chunks[i].valid && w->chunks[i].x == x && w->chunks[i].z == z) return &w->chunks[i];
    return NULL;
}
/* Protocol 772 infers word count. Entries never straddle a 64-bit word. */
static int Palette(struct LapisReader* r, cc_uint16* dst, int count, int biome) {
    cc_uint16 palette[256]; cc_uint64 word, mask;
    int bits = LapisReader_Byte(r), i, j, n = 0, perWord, index = 0, indirect;
    cc_uint32 value;
    if (!bits) {
        value = LapisReader_VarInt(r);
        if (value > 65535) return 0;
        if (dst) for (i = 0; i < count; i++) dst[i] = (cc_uint16)value;
        return !r->failed;
    }
    if (bits > (biome ? 6 : 15) || bits < (biome ? 1 : 4)) return 0;
    indirect = bits <= (biome ? 3 : 8);
    if (indirect) {
        n = LapisReader_Count(r, 1 << bits);
        if (!n) return 0;
        for (i = 0; i < n; i++) {
            value = LapisReader_VarInt(r);
            if (value > 65535) return 0;
            palette[i] = (cc_uint16)value;
        }
    }
    perWord = 64 / bits; mask = ((cc_uint64)1 << bits) - 1;
    while (index < count && !r->failed) {
        word = LapisReader_Big(r, 8);
        for (j = 0; j < perWord && index < count; j++, index++) {
            value = (cc_uint32)(word & mask); word >>= bits;
            if (indirect) { if (value >= (cc_uint32)n) return 0; value = palette[value]; }
            if (dst) dst[index] = (cc_uint16)value;
        }
    }
    return !r->failed;
}
static int PopCount(cc_uint64 mask) {
    int n = 0;
    while (mask) { n += (int)(mask & 1); mask >>= 1; }
    return n;
}
static int Light(struct LapisReader* r) {
    cc_uint64 masks[4]; int i, j, n;
    for (i = 0; i < 4; i++) {
        n = LapisReader_Count(r, 1);
        masks[i] = n ? LapisReader_Big(r, 8) : 0;
        if (masks[i] >> 26) return 0;
    }
    if ((masks[0] & masks[2]) || (masks[1] & masks[3])) return 0;
    for (i = 0; i < 2; i++) {
        n = LapisReader_Count(r, 26);
        if (n != PopCount(masks[i])) return 0;
        for (j = 0; j < n; j++) {
            if (LapisReader_VarInt(r) != 2048 || !LapisReader_Skip(r, 2048)) return 0;
        }
    }
    /* Lapis sends placeholder skylight and no block light. Native engine
       calculates terrain shadowing and emissive light from the actual blocks. */
    return LapisReader_Done(r);
}
int LapisWorld_Chunk(struct LapisWorld* w, const cc_uint8* data, int size) {
    struct LapisReader r, sections; struct LapisChunk* c;
    int x, z, n, i, nonAir;
    LapisReader_Init(&r, data, size);
    x = (cc_int32)LapisReader_Big(&r, 4); z = (cc_int32)LapisReader_Big(&r, 4);
    if (x < -2048 || x > 2047 || z < -2048 || z > 2047) return 0;
    /* Pinned Lapis sends an empty heightmap collection and zero block entities. */
    if (LapisReader_VarInt(&r) != 0) return 0;
    n = LapisReader_Count(&r, LAPIS_MAX_PACKET);
    if (r.failed || n > r.size - r.pos) return 0;
    LapisReader_Init(&sections, r.data + r.pos, n); r.pos += n;
    for (i = 0; i < 24; i++) {
        nonAir = (int)LapisReader_Big(&sections, 2);
        if (nonAir > 4096 || !Palette(&sections, i >= 4 && i < 20 ? w->staging.blocks + (i-4)*4096 : NULL, 4096, 0) ||
            !Palette(&sections, NULL, 64, 1)) return 0;
    }
    if (!LapisReader_Done(&sections) || LapisReader_VarInt(&r) != 0 || !Light(&r)) return 0;
    if (!InRange(w, x, z)) return 1; /* Validate even packets outside the cache. */
    c = LapisWorld_Find(w, x, z);
    if (!c) for (i = 0; i < LAPIS_CACHE_COUNT; i++) if (!w->chunks[i].valid) { c = &w->chunks[i]; break; }
    if (!c) return 0;
    memcpy(c->blocks, w->staging.blocks, sizeof(c->blocks));
    c->x = x; c->z = z; c->valid = 1; w->decoded++;
    return 1;
}
int LapisWorld_Block(struct LapisWorld* w, int x, int y, int z, int state) {
    int cx = LapisWorld_ChunkCoord(x), cz = LapisWorld_ChunkCoord(z);
    struct LapisChunk* c;
    if (y < 0 || y > 255 || state < 0 || state > 65535) return 0;
    c = LapisWorld_Find(w, cx, cz);
    if (c) { c->blocks[y * 256 + (z-cz*16)*16 + x-cx*16] = (cc_uint16)state; w->changes++; }
    return 1;
}
int LapisWorld_Get(struct LapisWorld* w, int x, int y, int z) {
    int cx = LapisWorld_ChunkCoord(x), cz = LapisWorld_ChunkCoord(z);
    struct LapisChunk* c = LapisWorld_Find(w, cx, cz);
    if (!c || y < 0 || y > 255) return -1;
    return c->blocks[y*256 + (z-cz*16)*16 + x-cx*16];
}
