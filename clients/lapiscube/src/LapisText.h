#ifndef LC_TEXT_H
#define LC_TEXT_H
#include "LapisProtocol.h"
CC_BEGIN_HEADER
/* Convert a length-prefixed Java modified-UTF-8 string to strict UTF-8. */
int LapisText_Nbt(struct LapisReader* r, char* text, int capacity);
int LapisText_Valid(const char* text, int length);
CC_END_HEADER
#endif
