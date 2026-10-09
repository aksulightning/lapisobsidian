#ifndef LC_BACKEND_H
#define LC_BACKEND_H
#include "Core.h"
#include "LapisGameplay.h"
CC_BEGIN_HEADER
extern cc_bool LapisBackend_Enabled;
void LapisBackend_Init(void);
void LapisBackend_Close(void);
struct LapisGameplay* LapisBackend_Gameplay(void);
struct LapisProtocol* LapisBackend_Protocol(void);
void LapisBackend_Dig(void);
void LapisBackend_Use(void);
void LapisBackend_Drop(void);
CC_END_HEADER
#endif
