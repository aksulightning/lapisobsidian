#ifndef LC_BACKEND_H
#define LC_BACKEND_H
#include "Core.h"
#include "LapisGameplay.h"
#include "LapisSigns.h"
CC_BEGIN_HEADER
extern cc_bool LapisBackend_Enabled;
void LapisBackend_Init(void);
void LapisBackend_Close(void);
struct LapisGameplay* LapisBackend_Gameplay(void);
struct LapisProtocol* LapisBackend_Protocol(void);
struct LapisSigns* LapisBackend_Signs(void);
struct LapisSign* LapisBackend_TargetSign(void);
void LapisBackend_Dig(void);
float LapisBackend_DigProgress(void);
void LapisBackend_Use(void);
void LapisBackend_Drop(void);
CC_END_HEADER
#endif
