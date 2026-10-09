#ifndef LC_MINING_H
#define LC_MINING_H
#include "Core.h"
CC_BEGIN_HEADER
/* Local presentation time in ms, -1 if unavailable. Server owns the result.
   These are LapisCube pacing choices, not a vanilla hardness specification. */
int LapisMining_Delay(const char* block, const char* tool, int mode);
CC_END_HEADER
#endif
