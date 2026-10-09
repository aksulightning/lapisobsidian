#ifndef LC_ENTITIES_H
#define LC_ENTITIES_H
#include "LapisProtocol.h"
CC_BEGIN_HEADER
#define LAPIS_MAX_ENTITIES 255
struct LapisEntity {
    int used, id, type, item, count, flags, pose, sheep, fuse, ground, mainHand, dead;
    double x,y,z;
    float yaw,pitch;
    unsigned revision, hurt, swing;
};
struct LapisEntities {
    struct LapisEntity list[LAPIS_MAX_ENTITIES];
    unsigned revision, damageEvents, animations, equipmentUpdates, deaths;
};
void LapisEntities_Init(struct LapisEntities* e);
int LapisEntities_Packet(struct LapisEntities* e, int id, const cc_uint8* data, int size);
int LapisEntities_Find(struct LapisEntities* e,int id);
CC_END_HEADER
#endif
