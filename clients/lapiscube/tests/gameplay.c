#include "LapisWorld.h"
#include "LapisGameplay.h"
#include "LapisEntities.h"
#include "LapisEffects.h"
#include "LapisText.h"
#include "LapisSigns.h"
#include "LapisMining.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>
#ifdef LAPIS_SANDBOX_SANITIZERS
const char* __asan_default_options(void) { return "detect_leaks=0"; }
const char* __ubsan_default_options(void) { return "halt_on_error=1"; }
#endif
static unsigned checks;
#define CHECK(x) do { checks++; if(!(x)) { fprintf(stderr,"gameplay:%d: %s\n",__LINE__,#x);assert(x); } } while(0)
static struct LapisWorld world;
static struct LapisProtocol protocol;
static struct LapisGameplay game;
static struct LapisEntities entities;
static cc_uint8 data[LAPIS_MAX_PACKET], section[LAPIS_MAX_PACKET];
static int n;
static void Byte(int v) { data[n++]=(cc_uint8)v; }
static void Var(cc_uint32 v) { n+=LapisProtocol_EncodeVarInt(data+n,v); }
static void Big(cc_uint64 v,int bytes) { int i;for(i=bytes-1;i>=0;i--)Byte((int)((v>>(8*i))&255)); }
static void Double(double v) { cc_uint64 bits;memcpy(&bits,&v,8);Big(bits,8); }
static void Float(float v) { cc_uint32 bits;memcpy(&bits,&v,4);Big(bits,4); }
static void String(const char* text) { int len=(int)strlen(text);Var((cc_uint32)len);memcpy(data+n,text,(size_t)len);n+=len; }
static int Chunk(int bits,int x,int z) {
    int i,j,k,count,per,bytes;cc_uint64 word;
    n=0;
    for(i=0;i<24;i++) {
        Big(4096,2);Byte(bits);
        if(!bits)Var((cc_uint32)(i+1));
        else {
            if(bits<=8) { Var(3);Var(1);Var(86);Var(24002); }
            per=64/bits;count=0;
            while(count<4096) {
                word=0;
                for(j=0;j<per && count<4096;j++,count++) { k=count%3;word|=(cc_uint64)(bits<=8?k:k+100)<<(j*bits); }
                Big(word,8);
            }
        }
        Byte(0);Var(0);
    }
    bytes=n;memcpy(section,data,(size_t)n);n=0;
    Big((cc_uint32)x,4);Big((cc_uint32)z,4);Var(0);Var((cc_uint32)bytes);
    memcpy(data+n,section,(size_t)bytes);n+=bytes;Var(0);
    for(i=0;i<6;i++)Var(0); /* masks and both empty light arrays */
    return n;
}
static void WorldTests(void) {
    int i,x,y,z,bits,size,old;struct LapisChunk* chunk;
    LapisWorld_Init(&world);CHECK(LapisWorld_Center(&world,-1,-1)==1);
    CHECK(LapisWorld_ChunkCoord(-1)==-1);CHECK(LapisWorld_ChunkCoord(-16)==-1);CHECK(LapisWorld_ChunkCoord(-17)==-2);
    for(bits=0;bits<=15;bits++) {
        if(bits>0 && bits<4)continue;
        size=Chunk(bits,-1,-1);CHECK(LapisWorld_Chunk(&world,data,size));chunk=LapisWorld_Find(&world,-1,-1);CHECK(chunk);
        for(i=0;i<65536;i+=127) {
            old=bits?(bits<=8?(i%4096%3==0?1:i%4096%3==1?86:24002):i%4096%3+100):i/4096+5;
            CHECK(chunk->blocks[i]==old);
        }
    }
    size=Chunk(8,-1,-1);old=world.chunks[0].blocks[0];
    for(i=0;i<size;i+=97)CHECK(!LapisWorld_Chunk(&world,data,i));
    CHECK(world.chunks[0].blocks[0]==old);CHECK(!LapisWorld_Chunk(&world,data,size-1));
    data[size++]=0;CHECK(!LapisWorld_Chunk(&world,data,size));
    CHECK(LapisWorld_Block(&world,-1,255,-1,1234));CHECK(LapisWorld_Get(&world,-1,255,-1)==1234);
    CHECK(!LapisWorld_Block(&world,0,256,0,1));CHECK(!LapisWorld_Block(&world,0,-1,0,1));
    LapisWorld_Position((((cc_uint64)-32768&0x3FFFFFF)<<38)|(((cc_uint64)32767&0x3FFFFFF)<<12)|255,&x,&y,&z);
    CHECK(x==-32768 && y==255 && z==32767);
    CHECK(LapisWorld_Center(&world,10,10)==1);CHECK(!LapisWorld_Find(&world,-1,-1));CHECK(world.evicted==1);
    CHECK(LapisWorld_Center(&world,2048,0)==-1);
}
static void SlotPacket(int window,int slot,int item,int count) {
    n=0;Var((cc_uint32)window);Var(0);Big((cc_uint64)slot,2);Var((cc_uint32)count);
    if(count) { Var((cc_uint32)item);Var(0);Var(0); }
}
static void GameplayTests(void) {
    struct LapisReader r;struct LapisSoundEvent sound;int before;
    LapisProtocol_Init(&protocol,NULL,NULL);LapisGameplay_Init(&game);
    protocol.state=LAPIS_PLAY;protocol.loaded=1;
    SlotPacket(0,36,35,64);CHECK(LapisGameplay_Packet(&game,&protocol,0x14,data,n));
    CHECK(game.slots[0][36].item==35 && game.slots[0][36].count==64);
    CHECK(!LapisGameplay_Packet(&game,&protocol,0x14,data,n-1));CHECK(game.slots[0][36].count==64);
    SlotPacket(0,64,1,1);CHECK(!LapisGameplay_Packet(&game,&protocol,0x14,data,n));
    SlotPacket(0,36,1,65);CHECK(!LapisGameplay_Packet(&game,&protocol,0x14,data,n));
    SlotPacket(2,54,28,6);CHECK(LapisGameplay_Packet(&game,&protocol,0x14,data,n));CHECK(game.slots[0][36].count==6);
    before=game.slots[0][36].count;CHECK(LapisGameplay_Click(&game,&protocol,36,0,0));CHECK(game.slots[0][36].count==before);
    LapisReader_Init(&r,protocol.output,protocol.outputSize);LapisReader_VarInt(&r);CHECK(LapisReader_VarInt(&r)==0x11);
    CHECK(LapisReader_VarInt(&r)==0);CHECK(LapisReader_VarInt(&r)==0);CHECK(LapisReader_Big(&r,2)==36);
    CHECK(LapisReader_Byte(&r)==0);CHECK(LapisReader_VarInt(&r)==0);CHECK(LapisReader_VarInt(&r)==0);CHECK(LapisReader_Byte(&r)==0);CHECK(LapisReader_Done(&r));
    CHECK(!LapisGameplay_Click(&game,&protocol,100,0,0));
    CHECK(LapisGameplay_Move(&protocol,-.5,65,2,0,-30,1));CHECK(!LapisGameplay_Move(&protocol,0,65,0,0,91,1));
    CHECK(LapisGameplay_Dig(&game,&protocol,0,-32768,255,32767,1));CHECK(game.sequence==1);
    CHECK(!LapisGameplay_Dig(&game,&protocol,0,0,256,0,1));
    CHECK(LapisGameplay_UseOn(&game,&protocol,0,0,0,1,.5f,1,.5f));CHECK(game.sequence==2);
    CHECK(!LapisGameplay_UseOn(&game,&protocol,0,0,0,1,2,0,0));
    n=0;Float(7);Var(13);Float(1.5f);CHECK(LapisGameplay_Packet(&game,&protocol,0x61,data,n));CHECK(game.health==7 && game.food==13);
    n=0;Var(0);String("minecraft:block.note_block.harp");Byte(0);Var(4);Big(68,4);Big(640,4);Big(68,4);Float(1);Float(1.5f);Big(0,8);
    CHECK(LapisEffects_Sound(&sound,data,n));CHECK(sound.x==8.5 && sound.pitch==1.5f);
    CHECK(!LapisEffects_Sound(&sound,data,n-1));
    n=0;Var(0);String("minecraft:overworld");Big(0,8);Byte(0);Byte(255);Byte(0);Byte(0);Byte(0);Var(0);Var(63);Byte(0);
    protocol.teleports=2;protocol.chunks=25;
    CHECK(!LapisGameplay_Packet(&game,&protocol,0x4B,data,n-1));CHECK(protocol.loaded);
    CHECK(LapisGameplay_Packet(&game,&protocol,0x4B,data,n));
    CHECK(!protocol.loaded && !protocol.teleports && !protocol.chunks && !game.slots[0][36].count);

}
static void EntityTests(void) {
    int index,i;unsigned revision;
    LapisEntities_Init(&entities);n=0;Var((cc_uint32)-2);for(i=0;i<16;i++)Byte(0);Var(28);
    Double(-.5);Double(64);Double(8.5);Byte(0);Byte(64);Byte(64);Var(0);Big(0,6);
    CHECK(LapisEntities_Packet(&entities,0x01,data,n));index=LapisEntities_Find(&entities,-2);CHECK(index>=0);
    CHECK(entities.list[index].type==28 && entities.list[index].x==-.5);
    n=0;Var((cc_uint32)-2);Big(4096,2);Big(0,2);Big((cc_uint16)-4096,2);Byte(128);Byte(0);Byte(1);
    CHECK(LapisEntities_Packet(&entities,0x2F,data,n));CHECK(entities.list[index].x==.5 && entities.list[index].z==7.5);
    revision=entities.revision;CHECK(!LapisEntities_Packet(&entities,0x2F,data,n-1));CHECK(entities.revision==revision);
    n=0;Var((cc_uint32)-2);Byte(0);Var(0);Byte(2);Byte(6);Var(21);Var(5);Byte(255);
    CHECK(LapisEntities_Packet(&entities,0x5C,data,n));CHECK(entities.list[index].flags==2 && entities.list[index].pose==5);
    n=0;Var(1);Var((cc_uint32)-2);CHECK(LapisEntities_Packet(&entities,0x46,data,n));CHECK(LapisEntities_Find(&entities,-2)==-1);
}

static void Text16(const char* text) { int len=(int)strlen(text);Big((cc_uint64)len,2);memcpy(data+n,text,(size_t)len);n+=len; }
static void Tag(int type,const char* name) { Byte(type);Text16(name); }
static void SignTests(void) {
    static struct LapisSigns signs;struct LapisReader r;struct LapisSign* sign;
    char text[97];int i,j,size;unsigned before;
    const char lines[4][97]={"edited","", "Hyv\xC3\xA4", "\xF0\x9F\x8C\x9F"};
    LapisSigns_Init(&signs);
    n=0;Text16("Hyv\xC3\xA4 \xED\xA0\xBC\xED\xBC\x9F");LapisReader_Init(&r,data,n);
    CHECK(LapisText_Nbt(&r,text,sizeof(text)));CHECK(!strcmp(text,"Hyv\xC3\xA4 \xF0\x9F\x8C\x9F"));
    n=0;Text16("\xED\xA0\xBC");LapisReader_Init(&r,data,n);CHECK(!LapisText_Nbt(&r,text,sizeof(text)));
    CHECK(!LapisText_Valid("\xC0\xAF",2));CHECK(!LapisText_Valid("\xED\xA0\x80",3));CHECK(LapisText_Valid(lines[3],4));
    n=0;Big(70,8);Var(7);Byte(10);Tag(8,"id");Text16("minecraft:sign");Tag(1,"is_waxed");Byte(0);
    for(i=0;i<2;i++) {
        Tag(10,i?"back_text":"front_text");Tag(8,"color");Text16("black");Tag(1,"has_glowing_text");Byte(0);
        Tag(9,"messages");Byte(8);Big(4,4);
        for(j=0;j<4;j++)Text16(j?"":"Original");
        Byte(0);
    }
    Byte(0);size=n;
    CHECK(LapisSigns_Packet(&signs,0x06,data,size));sign=LapisSigns_Find(&signs,0,70,0);CHECK(sign && !strcmp(sign->lines[0][0],"Original"));
    before=signs.revision;
    for(i=0;i<size;i++)CHECK(!LapisSigns_Packet(&signs,0x06,data,i));
    CHECK(signs.revision==before && !strcmp(sign->lines[0][0],"Original"));
    n=0;Big(70,8);Byte(1);CHECK(LapisSigns_Packet(&signs,0x35,data,n));CHECK(signs.editing && signs.front);
    LapisProtocol_Init(&protocol,NULL,NULL);protocol.state=LAPIS_PLAY;protocol.loaded=1;
    CHECK(LapisSigns_Submit(&signs,&protocol,lines));CHECK(!signs.editing && !strcmp(sign->lines[0][0],"Original"));
    CHECK(!LapisSigns_Submit(&signs,&protocol,lines));
    LapisSigns_Block(&signs,0,70,0,0);CHECK(!LapisSigns_Find(&signs,0,70,0));
    signs.list[0].valid=1;signs.list[0].x=-1;signs.list[0].z=-17;
    LapisSigns_Chunk(&signs,0,0);CHECK(signs.list[0].valid);
    LapisSigns_Chunk(&signs,-1,-2);CHECK(!signs.list[0].valid);
}
static void ContainerTests(void) {
    int w;
    LapisProtocol_Init(&protocol,NULL,NULL);LapisGameplay_Init(&game);
    SlotPacket(0,36,28,7);CHECK(LapisGameplay_Packet(&game,&protocol,0x14,data,n));
    for(w=2;w<=14;w+=w==2?10:2) {
        n=0;Var((cc_uint32)w);Var((cc_uint32)w);Byte(8);Text16("Container");
        CHECK(LapisGameplay_Packet(&game,&protocol,0x34,data,n));
        CHECK(game.slots[w][w==2?54:w==12?37:30].item==28);
        CHECK(game.slots[w][w==2?54:w==12?37:30].count==7);
    }
    SlotPacket(-2,36,28,6);CHECK(LapisGameplay_Packet(&game,&protocol,0x14,data,n));CHECK(game.slots[14][30].count==6);
    n=0;Float(12);Var(10);Float(-.4f);CHECK(LapisGameplay_Packet(&game,&protocol,0x61,data,n));CHECK(game.saturation==-.4f);
    n=0;Float(12);Var(10);Float(-1);CHECK(!LapisGameplay_Packet(&game,&protocol,0x61,data,n));CHECK(game.saturation==-.4f);
}

static void MiningTests(void) {
    int wood=LapisMining_Delay("stone","wooden_pickaxe",0),iron=LapisMining_Delay("stone","iron_pickaxe",0);
    CHECK(wood>iron && iron>=100);
    CHECK(LapisMining_Delay("stone","empty",0)>wood);
    CHECK(LapisMining_Delay("stone","iron_shovel",0)>wood);
    CHECK(LapisMining_Delay("sandstone","iron_pickaxe",0)<LapisMining_Delay("sandstone","iron_shovel",0));
    CHECK(LapisMining_Delay("oak_log","wooden_axe",0)<LapisMining_Delay("oak_log","iron_pickaxe",0));
    CHECK(LapisMining_Delay("dirt","wooden_shovel",0)<LapisMining_Delay("dirt","iron_axe",0));
    CHECK(LapisMining_Delay("bedrock","diamond_pickaxe",0)==-1);
    CHECK(LapisMining_Delay("bedrock","empty",1)==0);
    CHECK(LapisMining_Delay("stone","iron_pickaxe",2)==-1);
    CHECK(LapisMining_Delay("stone","iron_pickaxe",3)==-1);
    CHECK(LapisMining_Delay("torch","empty",0)==0);
    CHECK(LapisMining_Delay("brown_mushroom_block","empty",0)>0);
    CHECK(LapisMining_Delay("oak_leaves","shears",0)==0);
    CHECK(LapisMining_Delay("snow","stone_shovel",0)==0);
    CHECK(LapisMining_Delay("snow","wooden_shovel",0)>0);
    CHECK(LapisMining_Delay("water","empty",1)==-1);
}
static void TimeTests(void) {
    int i,before=world.dayTicks;
    n=0;Big(123456,8);Big(13000,8);Byte(1);
    for(i=0;i<n;i++)CHECK(!LapisWorld_Time(&world,data,i));
    CHECK(world.dayTicks==before);
    CHECK(LapisWorld_Time(&world,data,n));CHECK(world.age==123456 && world.dayTicks==13000 && world.dayTicking);
    CHECK(LapisWorld_Daylight(&world)==8);
    n=0;Big(123457,8);Big(18000,8);Byte(0);
    CHECK(LapisWorld_Time(&world,data,n));CHECK(!world.dayTicking && LapisWorld_Daylight(&world)==0);
    n=0;Big(123458,8);Big(6000,8);Byte(1);
    CHECK(LapisWorld_Time(&world,data,n));CHECK(LapisWorld_Daylight(&world)==16);
    n=0;Big(123459,8);Big(23000,8);Byte(1);
    CHECK(LapisWorld_Time(&world,data,n));CHECK(LapisWorld_Daylight(&world)==8);
    n=0;Big(123460,8);Big(24000,8);Byte(1);
    CHECK(!LapisWorld_Time(&world,data,n));CHECK(world.dayTicks==23000);
}
static void InventoryControlsTests(void) {
    struct LapisReader r;int i,size;
    LapisProtocol_Init(&protocol,NULL,NULL);LapisGameplay_Init(&game);
    protocol.state=LAPIS_PLAY;protocol.loaded=1;
    SlotPacket(0,1,134,4);CHECK(LapisGameplay_Packet(&game,&protocol,0x14,data,n));game.stateId[0]=7;
    CHECK(LapisGameplay_Refresh(&game,&protocol));CHECK(game.refreshMask==31 && game.slots[0][1].count==4);
    LapisReader_Init(&r,protocol.output,protocol.outputSize);LapisReader_VarInt(&r);
    CHECK(LapisReader_VarInt(&r)==0x11 && LapisReader_VarInt(&r)==0 && LapisReader_VarInt(&r)==7);
    CHECK(LapisReader_Big(&r,2)==36 && LapisReader_Byte(&r)==0 && LapisReader_VarInt(&r)==2);
    CHECK(LapisReader_VarInt(&r)==4);
    for(i=1;i<=4;i++) {
        CHECK(LapisReader_Big(&r,2)==(cc_uint64)i);CHECK(LapisReader_Byte(&r)==(i==1));
        if(i==1) { CHECK(LapisReader_VarInt(&r)==134 && LapisReader_VarInt(&r)==4);CHECK(!LapisReader_VarInt(&r) && !LapisReader_VarInt(&r)); }
    }
    CHECK(!LapisReader_Byte(&r) && LapisReader_Done(&r));
    SlotPacket(0,1,0,0);CHECK(!LapisGameplay_Packet(&game,&protocol,0x14,data,n-1));CHECK(game.refreshMask==31);
    for(i=0;i<=4;i++) { SlotPacket(0,i,0,0);CHECK(LapisGameplay_Packet(&game,&protocol,0x14,data,n)); }
    CHECK(!game.refreshMask && !game.slots[0][1].count);
    protocol.outputSize=0;game.window=12;
    CHECK(!LapisGameplay_Refresh(&game,&protocol));
    CHECK(LapisGameplay_Swap(&game,&protocol,10,7));
    LapisReader_Init(&r,protocol.output,protocol.outputSize);LapisReader_VarInt(&r);CHECK(LapisReader_VarInt(&r)==0x11);
    CHECK(LapisReader_VarInt(&r)==12 && !LapisReader_VarInt(&r) && LapisReader_Big(&r,2)==10);
    CHECK(LapisReader_Byte(&r)==7 && LapisReader_VarInt(&r)==2 && !LapisReader_VarInt(&r) && !LapisReader_Byte(&r) && LapisReader_Done(&r));
    size=protocol.outputSize;
    CHECK(!LapisGameplay_Swap(&game,&protocol,46,0));CHECK(!LapisGameplay_Swap(&game,&protocol,1,9));CHECK(protocol.outputSize==size);
    protocol.outputSize=0;game.window=14;
    CHECK(!LapisGameplay_Click(&game,&protocol,39,0,0));CHECK(!LapisGameplay_DropSlot(&game,&protocol,-999,1));
    CHECK(LapisGameplay_DropSlot(&game,&protocol,38,1));
    LapisReader_Init(&r,protocol.output,protocol.outputSize);LapisReader_VarInt(&r);CHECK(LapisReader_VarInt(&r)==0x11);
    CHECK(LapisReader_VarInt(&r)==14 && !LapisReader_VarInt(&r) && LapisReader_Big(&r,2)==38);
    CHECK(LapisReader_Byte(&r)==1 && LapisReader_VarInt(&r)==4 && !LapisReader_VarInt(&r) && !LapisReader_Byte(&r) && LapisReader_Done(&r));
    game.window=15;CHECK(!LapisGameplay_Click(&game,&protocol,-999,0,0));
    game.window=2;protocol.state=LAPIS_FAILED;CHECK(!LapisGameplay_Close(&game,&protocol));CHECK(game.window==2);
}
int main(void) {
    WorldTests();GameplayTests();EntityTests();SignTests();ContainerTests();MiningTests();TimeTests();InventoryControlsTests();
    printf("world/gameplay: %u assertions passed (palettes, height, boundaries, truncation, inventory authority, actions, entities, sounds)\n",checks);return 0;
}
