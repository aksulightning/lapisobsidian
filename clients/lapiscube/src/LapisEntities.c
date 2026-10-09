#include "LapisEntities.h"
#include <string.h>
void LapisEntities_Init(struct LapisEntities* e) { memset(e,0,sizeof(*e)); }
int LapisEntities_Find(struct LapisEntities* e,int id) {
    int i;
    for(i=0;i<LAPIS_MAX_ENTITIES;i++)if(e->list[i].used && e->list[i].id==id)return i;
    return -1;
}
static float Angle(struct LapisReader* r) { return (float)LapisReader_Byte(r)*360.0f/256; }
static int SignedShort(struct LapisReader* r) { int n=(int)LapisReader_Big(r,2);return n>=32768?n-65536:n; }
int LapisEntities_Packet(struct LapisEntities* e,int packet,const cc_uint8* data,int size) {
    struct LapisReader r;struct LapisEntity value;int id,index,i,n,tag,type,v=0,remove[LAPIS_MAX_ENTITIES];
    if(packet!=0x01 && packet!=0x1F && packet!=0x2F && packet!=0x31 && packet!=0x4C && packet!=0x5C && packet!=0x46 &&
       packet!=0x02 && packet!=0x19 && packet!=0x1E && packet!=0x5F)return 2;
    LapisReader_Init(&r,data,size);
    if(packet==0x46) {
        n=LapisReader_Count(&r,LAPIS_MAX_ENTITIES);
        for(i=0;i<n;i++)remove[i]=(cc_int32)LapisReader_VarInt(&r);
        if(!LapisReader_Done(&r))return 0;
        for(i=0;i<n;i++) { index=LapisEntities_Find(e,remove[i]);if(index>=0)e->list[index].used=0; }
        e->revision++;return 1;
    }
    id=(cc_int32)(packet==0x1E?LapisReader_Big(&r,4):LapisReader_VarInt(&r));index=LapisEntities_Find(e,id);
    memset(&value,0,sizeof(value));if(index>=0)value=e->list[index];
    if(packet==0x01) {
        memset(&value,0,sizeof(value));
        if(!LapisReader_Skip(&r,16))return 0;
        value.used=1;value.id=id;value.type=LapisReader_Count(&r,255);
        value.x=LapisReader_Double(&r);value.y=LapisReader_Double(&r);value.z=LapisReader_Double(&r);
        value.pitch=Angle(&r);value.yaw=Angle(&r);Angle(&r);LapisReader_VarInt(&r);LapisReader_Skip(&r,6);
        if(index<0)for(i=0;i<LAPIS_MAX_ENTITIES;i++)if(!e->list[i].used) { index=i;break; }
        if(index<0)return 0; /* Profile exceeds the documented bounded entity budget. */
    } else if(packet==0x19) {
        /* Pinned server: registry-local damage type, absent sources/position. */
        LapisReader_Count(&r,48);
        if(LapisReader_VarInt(&r) || LapisReader_VarInt(&r) || LapisReader_Byte(&r))return 0;
        value.hurt++;
    } else if(packet==0x02) {
        v=LapisReader_Byte(&r);if(v!=0 && v!=2)return 0;
        if(!v)value.swing++;
    } else if(packet==0x1E) {
        v=LapisReader_Byte(&r);
        if(v==3)value.dead=1; /* Food/hand status 9/47 needs no remote animation. */
    } else if(packet==0x5F) {
        /* Lapis only emits a single main-hand entry with a component-free stack. */
        if(LapisReader_Byte(&r)!=0)return 0;
        v=LapisReader_Count(&r,1);value.mainHand=0;
        if(v) {
            value.mainHand=LapisReader_Count(&r,1415);
            if(!value.mainHand || LapisReader_VarInt(&r) || LapisReader_VarInt(&r))return 0;
        }
    } else if(packet==0x1F) {
        value.x=LapisReader_Double(&r);value.y=LapisReader_Double(&r);value.z=LapisReader_Double(&r);
        LapisReader_Double(&r);LapisReader_Double(&r);LapisReader_Double(&r);
        value.yaw=LapisReader_Float(&r);value.pitch=LapisReader_Float(&r);value.ground=LapisReader_Byte(&r);
    } else if(packet==0x2F) {
        value.x+=(double)SignedShort(&r)/4096;value.y+=(double)SignedShort(&r)/4096;value.z+=(double)SignedShort(&r)/4096;
        value.yaw=Angle(&r);value.pitch=Angle(&r);value.ground=LapisReader_Byte(&r);
    } else if(packet==0x31) {
        value.yaw=Angle(&r);value.pitch=Angle(&r);value.ground=LapisReader_Byte(&r);
    } else if(packet==0x4C) {
        value.yaw=Angle(&r);
    } else if(packet==0x5C) {
        for(n=0;n<64;n++) {
            tag=LapisReader_Byte(&r);if(r.failed)return 0;if(tag==255)break;
            type=LapisReader_Count(&r,64);
            if(type==0 || type==8) {
                v=LapisReader_Byte(&r);
                if(type==8 && v>1)return 0;
                if(tag==0 && type==0)value.flags=v;
                if(tag==17 && type==0)value.sheep=v;
            } else if(type==1 || type==21) {
                v=(cc_int32)LapisReader_VarInt(&r);
                if(tag==6 && type==21)value.pose=v;
                if(tag==16 && type==1)value.fuse=v;
            } else if(type==7) {
                v=LapisReader_Count(&r,64);value.count=v;value.item=0;
                if(v) { value.item=LapisReader_Count(&r,1415);if(!value.item || LapisReader_VarInt(&r) || LapisReader_VarInt(&r))return 0; }
            } else return 0; /* No arbitrary NBT/components in the Lapis entity profile. */
        }
        if(n==64)return 0;
    }
    if(!LapisReader_Done(&r) || value.ground>1 || value.x < -32769 || value.x>32769 ||
       value.z < -32769 || value.z>32769 || value.y < -1024 || value.y>1024)return 0;
    if(packet==0x19)e->damageEvents++;
    if(packet==0x02)e->animations++;
    if(packet==0x5F)e->equipmentUpdates++;
    if(packet==0x1E && v==3)e->deaths++;
    if(index>=0) { value.revision=++e->revision;e->list[index]=value; }
    return 1;
}
