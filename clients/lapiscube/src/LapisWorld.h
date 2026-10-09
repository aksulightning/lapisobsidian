#ifndef LC_WORLD_H
#define LC_WORLD_H
#include "LapisProtocol.h"
CC_BEGIN_HEADER
#define LAPIS_CACHE_SIDE 7
#define LAPIS_CACHE_COUNT 49
#define LAPIS_WORLD_SIDE (LAPIS_CACHE_SIDE * 16)
struct LapisChunk {
    int x, z, valid;
    cc_uint16 blocks[65536];
};
struct LapisWorld {
    struct LapisChunk chunks[LAPIS_CACHE_COUNT], staging;
    int centerX, centerZ, hasCenter;
    unsigned decoded, changes, evicted;
    cc_uint64 age;
    int dayTicks, clockValid, dayTicking;
};
void LapisWorld_Init(struct LapisWorld* w);
int LapisWorld_Center(struct LapisWorld* w, int x, int z);
int LapisWorld_Chunk(struct LapisWorld* w, const cc_uint8* data, int size);
struct LapisChunk* LapisWorld_Find(struct LapisWorld* w, int cx, int cz);
int LapisWorld_Block(struct LapisWorld* w, int x, int y, int z, int state);
int LapisWorld_Get(struct LapisWorld* w, int x, int y, int z);
int LapisWorld_Time(struct LapisWorld* w, const cc_uint8* data, int size);
/* 0..16 brightness steps; no client-side advancement of server time. */
int LapisWorld_Daylight(const struct LapisWorld* w);
int LapisWorld_ChunkCoord(int block);
void LapisWorld_Position(cc_uint64 packed, int* x, int* y, int* z);
CC_END_HEADER
#endif
