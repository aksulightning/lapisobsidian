#ifndef LC_SIGNS_H
#define LC_SIGNS_H
#include "LapisProtocol.h"
CC_BEGIN_HEADER
#define LAPIS_SIGN_COUNT 128
#define LAPIS_SIGN_LINE 97
struct LapisSign { int valid,x,y,z; char lines[2][4][LAPIS_SIGN_LINE]; };
struct LapisSigns {
    struct LapisSign list[LAPIS_SIGN_COUNT], edit;
    int editing,front,next;
    unsigned revision;
};
void LapisSigns_Init(struct LapisSigns* s);
struct LapisSign* LapisSigns_Find(struct LapisSigns* s,int x,int y,int z);
int LapisSigns_Packet(struct LapisSigns* s,int id,const cc_uint8* data,int size);
int LapisSigns_Submit(struct LapisSigns* s,struct LapisProtocol* p,const char lines[4][LAPIS_SIGN_LINE]);
void LapisSigns_Block(struct LapisSigns* s,int x,int y,int z,int state);
/* Fresh chunk data supersedes old text; authoritative overlays follow it. */
void LapisSigns_Chunk(struct LapisSigns* s,int x,int z);
CC_END_HEADER
#endif
