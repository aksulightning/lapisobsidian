#ifndef LC_BLOCKS_H
#define LC_BLOCKS_H
#include "BlockID.h"
CC_BEGIN_HEADER
void LapisBlocks_Init(void);
BlockID LapisBlocks_State(int state);
BlockID LapisBlocks_Item(int item);
const char* LapisBlocks_ItemName(int item);
int LapisBlocks_ItemIcon(int item);
CC_END_HEADER
#endif
