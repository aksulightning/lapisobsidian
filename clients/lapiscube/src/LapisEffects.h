#ifndef LC_EFFECTS_H
#define LC_EFFECTS_H
#include "LapisProtocol.h"
CC_BEGIN_HEADER
struct LapisSoundEvent { char name[128]; int category; double x,y,z; float volume,pitch; };
int LapisEffects_Sound(struct LapisSoundEvent* e,const cc_uint8* data,int size);
CC_END_HEADER
#endif
