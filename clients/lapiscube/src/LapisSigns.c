#include "LapisSigns.h"
#include "LapisText.h"
#include "LapisWorld.h"
#include <string.h>
void LapisSigns_Init(struct LapisSigns* s) { memset(s,0,sizeof(*s)); }
struct LapisSign* LapisSigns_Find(struct LapisSigns* s,int x,int y,int z) {
    int i;
    for(i=0;i<LAPIS_SIGN_COUNT;i++)if(s->list[i].valid && s->list[i].x==x && s->list[i].y==y && s->list[i].z==z)return &s->list[i];
    return NULL;
}
static int Coordinates(struct LapisReader* r,struct LapisSign* sign) {
    LapisWorld_Position(LapisReader_Big(r,8),&sign->x,&sign->y,&sign->z);
    return !r->failed && sign->x>=-32768 && sign->x<=32767 && sign->z>=-32768 && sign->z<=32767 && sign->y>=0 && sign->y<=255;
}
static int Tag(struct LapisReader* r,int type,const char* name) {
    char text[32];
    return LapisReader_Byte(r)==type && LapisText_Nbt(r,text,sizeof(text)) && !strcmp(text,name);
}
int LapisSigns_Packet(struct LapisSigns* s,int id,const cc_uint8* data,int size) {
    struct LapisReader r;struct LapisSign value,*found;int front,side,line;char text[32];
    if(id!=0x06 && id!=0x35)return 2;
    memset(&value,0,sizeof(value));LapisReader_Init(&r,data,size);
    if(!Coordinates(&r,&value))return 0;
    found=LapisSigns_Find(s,value.x,value.y,value.z);
    if(id==0x35) {
        front=LapisReader_Byte(&r);if(front>1 || !LapisReader_Done(&r))return 0;
        s->edit=found?*found:value;s->edit.valid=1;s->front=front;s->editing=1;return 1;
    }
    /* Exact anonymous-NBT profile emitted by signs_nbt at the pinned server. */
    if(LapisReader_VarInt(&r)!=7 || LapisReader_Byte(&r)!=10 || !Tag(&r,8,"id") ||
       !LapisText_Nbt(&r,text,sizeof(text)) || strcmp(text,"minecraft:sign") ||
       !Tag(&r,1,"is_waxed") || LapisReader_Byte(&r)!=0)return 0;
    for(side=0;side<2;side++) {
        if(!Tag(&r,10,side?"back_text":"front_text") || !Tag(&r,8,"color") ||
           !LapisText_Nbt(&r,text,sizeof(text)) || strcmp(text,"black") ||
           !Tag(&r,1,"has_glowing_text") || LapisReader_Byte(&r)!=0 ||
           !Tag(&r,9,"messages") || LapisReader_Byte(&r)!=8 || LapisReader_Big(&r,4)!=4)return 0;
        for(line=0;line<4;line++)if(!LapisText_Nbt(&r,value.lines[side][line],LAPIS_SIGN_LINE))return 0;
        if(LapisReader_Byte(&r)!=0)return 0;
    }
    if(LapisReader_Byte(&r)!=0 || !LapisReader_Done(&r))return 0;
    if(!found) { found=&s->list[s->next];s->next=(s->next+1)%LAPIS_SIGN_COUNT; }
    value.valid=1;*found=value;s->revision++;return 1;
}
int LapisSigns_Submit(struct LapisSigns* s,struct LapisProtocol* p,const char lines[4][LAPIS_SIGN_LINE]) {
    cc_uint8 data[420];cc_uint64 position;int i,n=0,length;
    if(!s->editing || !p->loaded || p->state!=LAPIS_PLAY)return 0;
    position=(((cc_uint64)s->edit.x&0x3FFFFFF)<<38)|(((cc_uint64)s->edit.z&0x3FFFFFF)<<12)|((cc_uint64)s->edit.y&4095);
    for(i=7;i>=0;i--)data[n++]=(cc_uint8)(position>>(8*i));
    data[n++]=(cc_uint8)s->front;
    for(i=0;i<4;i++) {
        for(length=0;length<LAPIS_SIGN_LINE && lines[i][length];length++) { }
        if(length>=LAPIS_SIGN_LINE || !LapisText_Valid(lines[i],length))return 0;
        n+=LapisProtocol_EncodeVarInt(data+n,(cc_uint32)length);
        memcpy(data+n,lines[i],(size_t)length);n+=length;
    }
    if(!LapisProtocol_Queue(p,0x3B,data,n))return 0;
    s->editing=0;return 1; /* Render only the subsequent authoritative sign update. */
}
void LapisSigns_Block(struct LapisSigns* s,int x,int y,int z,int state) {
    struct LapisSign* sign=LapisSigns_Find(s,x,y,z);
    if((state>=4367 && state<=4397 && (state&1)) || (state>=4859 && state<=4865 && (state&1)))return;
    if(sign)sign->valid=0;
}
void LapisSigns_Chunk(struct LapisSigns* s,int x,int z) {
    int i;struct LapisSign* sign;
    for(i=0;i<LAPIS_SIGN_COUNT;i++) {
        sign=&s->list[i];
        if(sign->valid && LapisWorld_ChunkCoord(sign->x)==x && LapisWorld_ChunkCoord(sign->z)==z)sign->valid=0;
    }
}
