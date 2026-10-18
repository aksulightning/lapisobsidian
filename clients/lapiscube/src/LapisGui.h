#ifndef LC_GUI_H
#define LC_GUI_H
#include "Core.h"
CC_BEGIN_HEADER
struct LapisStack;
/* Shared drawing primitives; label indices identify cache entries. */
void LapisGui_Text(int index,const char* value,int x,int y);
void LapisGui_Slot(int slot,struct LapisStack stack,int x,int y,int size,int active);
int LapisGui_InventoryOpen(void);
void LapisGui_ShowInventory(void);
void LapisGui_ShowSign(void);
void LapisGui_Close(void);
void LapisGui_RenderHUD(void);
void LapisGui_ContextLost(void);
void LapisGui_TouchClickMode(int mode);
CC_END_HEADER
#endif
