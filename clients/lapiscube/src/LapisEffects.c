#include "LapisEffects.h"
int LapisEffects_Sound(struct LapisSoundEvent* e,const cc_uint8* data,int size) {
    struct LapisReader r;int range;
    LapisReader_Init(&r,data,size);
    if(LapisReader_VarInt(&r)!=0)return 0; /* Lapis uses inline identifiers. */
    LapisReader_String(&r,e->name,sizeof(e->name));range=LapisReader_Byte(&r);
    if(range>1)return 0;
    if(range)LapisReader_Float(&r);
    e->category=LapisReader_Count(&r,9);
    e->x=(double)(cc_int32)LapisReader_Big(&r,4)/8;
    e->y=(double)(cc_int32)LapisReader_Big(&r,4)/8;
    e->z=(double)(cc_int32)LapisReader_Big(&r,4)/8;
    e->volume=LapisReader_Float(&r);e->pitch=LapisReader_Float(&r);LapisReader_Big(&r,8);
    return LapisReader_Done(&r) && e->name[0] && e->volume>=0 && e->volume<=16 && e->pitch>0 && e->pitch<=4;
}
