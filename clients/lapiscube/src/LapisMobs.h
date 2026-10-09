#ifndef LC_MOBS_H
#define LC_MOBS_H
#include "LapisEntities.h"
CC_BEGIN_HEADER
void LapisMobs_Init(void);
void LapisMobs_Update(struct LapisEntities* e,int originX,int originZ);
void LapisMobs_Clear(void);
int LapisMobs_Target(struct LapisEntities* e);
CC_END_HEADER
#endif
