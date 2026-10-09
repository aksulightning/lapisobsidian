#ifndef LC_AUDIO_H
#define LC_AUDIO_H
#include "LapisEffects.h"
CC_BEGIN_HEADER
void LapisAudio_Play(const struct LapisSoundEvent* sound,double x,double y,double z);
void LapisAudio_Free(void);
CC_END_HEADER
#endif
