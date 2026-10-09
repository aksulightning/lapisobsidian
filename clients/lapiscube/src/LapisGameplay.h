#ifndef LC_GAMEPLAY_H
#define LC_GAMEPLAY_H
#include "LapisProtocol.h"
CC_BEGIN_HEADER
struct LapisStack { int item, count; };
struct LapisGameplay {
    struct LapisStack slots[16][64], cursor;
    int window, menu, selected, food, abilities, stateId[16];
    float health, saturation;
    cc_uint32 sequence, acknowledged;
    unsigned revision;
    char title[128], message[2048];
};
void LapisGameplay_Init(struct LapisGameplay* g);
/* 1 = handled, 0 = malformed, 2 = packet owned by another module. */
int LapisGameplay_Packet(struct LapisGameplay* g, struct LapisProtocol* p, int id, const cc_uint8* data, int size);
int LapisGameplay_Move(struct LapisProtocol* p, double x, double y, double z, float yaw, float pitch, int ground);
int LapisGameplay_Dig(struct LapisGameplay* g, struct LapisProtocol* p, int action, int x, int y, int z, int face);
int LapisGameplay_UseOn(struct LapisGameplay* g, struct LapisProtocol* p, int x, int y, int z, int face, float dx, float dy, float dz);
int LapisGameplay_Use(struct LapisGameplay* g, struct LapisProtocol* p, float yaw, float pitch);
int LapisGameplay_Select(struct LapisProtocol* p, int slot);
int LapisGameplay_Click(struct LapisGameplay* g, struct LapisProtocol* p, int slot, int right, int shift);
int LapisGameplay_Close(struct LapisGameplay* g, struct LapisProtocol* p);
int LapisGameplay_Chat(struct LapisProtocol* p, const char* text, int length);
int LapisGameplay_Attack(struct LapisProtocol* p, int entity);
int LapisGameplay_Respawn(struct LapisProtocol* p);
CC_END_HEADER
#endif
