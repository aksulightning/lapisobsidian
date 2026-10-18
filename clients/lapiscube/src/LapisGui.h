#ifndef LC_GUI_H
#define LC_GUI_H
#include "Core.h"
CC_BEGIN_HEADER
struct LapisStack;
/* Drawing owns cached text textures. Callers supply a stable label index in
 * 0..98 (also used by slot counts); distinct visible labels need distinct IDs. */
void LapisGui_Text(int index,const char* value,int x,int y);
void LapisGui_Slot(int slot,struct LapisStack stack,int x,int y,int size,int active);
int LapisGui_InventoryOpen(void);
void LapisGui_ShowInventory(void);
void LapisGui_ShowSign(void);
void LapisGui_Close(void);
void LapisGui_RenderHUD(void);
/* Release cached GPU resources and font; the next draw recreates them lazily. */
void LapisGui_ContextLost(void);
void LapisGui_TouchClickMode(int mode);
CC_END_HEADER
#endif
