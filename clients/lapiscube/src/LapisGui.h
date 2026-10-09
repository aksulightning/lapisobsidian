#ifndef LC_GUI_H
#define LC_GUI_H
#include "Core.h"
CC_BEGIN_HEADER
void LapisGui_ShowInventory(void);
void LapisGui_ShowSign(void);
void LapisGui_Close(void);
void LapisGui_RenderHUD(void);
void LapisGui_ContextLost(void);
void LapisGui_TouchClickMode(int mode);
CC_END_HEADER
#endif
