#include "LapisBlocks.h"
#include "LapisAudio.h"
#include "LapisFacts.h"
#include "Block.h"
#include "Audio.h"
#include "String_.h"
#include <string.h>

static BlockID states[65536], items[1416];
static int nextBlock;
static BlockID Named(const char* name) {
    int i;
    for (i = 0; i < 256; i++) if (!strcmp(Lapis_BlockFacts[i].name,name)) return (BlockID)(256+i);
    return 256;
}
static void Box(BlockID b, float x0,float y0,float z0,float x1,float y1,float z1) {
    Vec3_Set(Blocks.MinBB[b],x0,y0,z0); Vec3_Set(Blocks.MaxBB[b],x1,y1,z1);
}
static void Define(BlockID b) { Block_DefineCustom(b, false); }
static BlockID Clone(BlockID source) {
    BlockID b = (BlockID)nextBlock++; int f;
    cc_string name = Block_UNSAFE_GetName(source);
    Block_ResetProps(b); Block_SetName(b,&name);
    for (f=0; f<FACE_COUNT; f++) Block_Tex(b,f)=Block_Tex(source,f);
    Blocks.Draw[b]=Blocks.Draw[source]; Blocks.Collide[b]=Blocks.ExtendedCollide[source];
    Blocks.BlocksLight[b]=Blocks.BlocksLight[source]; Blocks.Brightness[b]=Blocks.Brightness[source];
    Blocks.MinBB[b]=Blocks.MinBB[source]; Blocks.MaxBB[b]=Blocks.MaxBB[source];
    Blocks.StepSounds[b]=Blocks.StepSounds[source]; Blocks.DigSounds[b]=Blocks.DigSounds[source];
    Blocks.CanPlace[b]=Blocks.CanDelete[b]=false;
    return b;
}
void LapisBlocks_Init(void) {
    int i,j, facing, open, base; BlockID b, source;
    const struct LapisBlockFact* f; const char* name; cc_string text;
    nextBlock=512;
    for(i=0;i<65536;i++) states[i]=256; /* Visible solid unknown state, never guessed air. */
    memset(items,0,sizeof(items));
    for(i=0;i<256;i++) {
        b=(BlockID)(256+i);f=&Lapis_BlockFacts[i];name=f->name;
        Block_ResetProps(b);text=String_FromReadonly(name);Block_SetName(b,&text);
        Block_SetSide((TextureLoc)f->side,b);
        Block_Tex(b,FACE_YMAX)=(TextureLoc)f->top;Block_Tex(b,FACE_YMIN)=(TextureLoc)f->bottom;
        Blocks.Draw[b]=DRAW_OPAQUE;Blocks.Collide[b]=COLLIDE_SOLID;Blocks.BlocksLight[b]=true;
        Blocks.StepSounds[b]=Blocks.DigSounds[b]=LapisAudio_Material(name);
        Blocks.SpeedMultiplier[b]=1;Box(b,0,0,0,1,1,1);
        if (strstr(name,"water") || strstr(name,"lava")) {
            Blocks.Draw[b]=DRAW_TRANSLUCENT;Blocks.Collide[b]=strstr(name,"water")?COLLIDE_WATER:COLLIDE_LAVA;
            Blocks.BlocksLight[b]=false;Blocks.StepSounds[b]=Blocks.DigSounds[b]=SOUND_NONE;
            Blocks.FogCol[b]=strstr(name,"water")?PackedCol_Make(45,95,150,255):PackedCol_Make(235,90,25,255);
            Blocks.FogDensity[b]=0.2f;Blocks.Brightness[b]=strstr(name,"lava")?15:0;
        }
        if (strstr(name,"leaves") || strstr(name,"glass")) { Blocks.Draw[b]=DRAW_TRANSPARENT;Blocks.BlocksLight[b]=false; }
        if (strstr(name,"ice")) { Blocks.Draw[b]=DRAW_TRANSLUCENT;Blocks.BlocksLight[b]=false;Blocks.Collide[b]=COLLIDE_ICE; }
        if (strstr(name,"sapling") || strstr(name,"flower") || strstr(name,"mushroom") || !strcmp(name,"short_grass") ||
            !strcmp(name,"fern") || !strcmp(name,"dead_bush") || !strcmp(name,"wheat") || !strcmp(name,"sugar_cane")) {
            Blocks.Draw[b]=DRAW_SPRITE;Blocks.Collide[b]=COLLIDE_NONE;Blocks.BlocksLight[b]=false;
        }
        if (strstr(name,"torch")) { Box(b,.4f,0,.4f,.6f,.65f,.6f);Blocks.Collide[b]=COLLIDE_NONE;Blocks.BlocksLight[b]=false;Blocks.Brightness[b]=15; }
        if (strstr(name,"slab")) Box(b,0,0,0,1,.5f,1);
        if (!strcmp(name,"farmland")) Box(b,0,0,0,1,.9375f,1);
        if (!strcmp(name,"snow") || strstr(name,"pressure_plate") || strstr(name,"rail")) {
            Box(b,0,0,0,1,.0625f,1);Blocks.Collide[b]=COLLIDE_NONE;Blocks.BlocksLight[b]=false;
        }
        if (!strcmp(name,"ladder")) { Box(b,0,0,.875f,1,1,1);Blocks.Collide[b]=COLLIDE_CLIMB;Blocks.BlocksLight[b]=false; }
        if (strstr(name,"sign") || strstr(name,"door") || !strcmp(name,"lever")) Blocks.BlocksLight[b]=false;
        if (strstr(name,"glowstone") || !strcmp(name,"jack_o_lantern")) Blocks.Brightness[b]=15;
        Blocks.CanPlace[b]=Blocks.CanDelete[b]=false;
        Define(b);states[f->state]=b;
        if (f->item) items[f->item]=b;
    }
    states[0]=0; /* Real air only. */
    /* Server-specific state ranges, including open collision shapes. */
    source=Named("oak_door");
    for (i=0;i<64;i++) {
        b=Clone(source);facing=i/16;open=!(i&2);
        if(open) facing=(facing==0?3:facing==1?2:facing==2?0:1);
        if(facing==0) Box(b,0,0,0,1,1,.1875f);
        if(facing==1) Box(b,0,0,.8125f,1,1,1);
        if(facing==2) Box(b,0,0,0,.1875f,1,1);
        if(facing==3) Box(b,.8125f,0,0,1,1,1);
        Define(b);states[4686+i]=b;
    }
    for (j=0;j<2;j++) {
        base=j?11288:6140;source=Named(j?"iron_trapdoor":"oak_trapdoor");
        for(i=0;i<64;i++) {
            b=Clone(source);facing=i/16;open=!(i&4);
            if (!open) Box(b,0,(i&8)?0:.8125f,0,1,(i&8)?.1875f:1,1);
            else if(facing==0) Box(b,0,0,.8125f,1,1,1);
            else if(facing==1) Box(b,0,0,0,1,1,.1875f);
            else if(facing==2) Box(b,.8125f,0,0,1,1,1);
            else Box(b,0,0,0,.1875f,1,1);
            Define(b);states[base+i]=b;
        }
    }
    for(i=0;i<8;i++) {
        b=Clone(Named("wheat"));Block_SetSide((TextureLoc)Lapis_WheatTiles[i/2],b);
        Box(b,0,0,0,1,(float)(i+1)/8,1);Define(b);states[4342+i]=b;
        states[4350+i]=Named("farmland");
    }
    for(i=0;i<16;i++) {
        b=Clone(Named("oak_sign"));Box(b,.1f,.4f,.4f,.9f,1,.6f);Blocks.Collide[b]=COLLIDE_NONE;
        Define(b);states[4367+i*2]=b;
    }
    for(i=0;i<4;i++) states[4859+i*2]=states[4367];
    for(i=581;i<1731;i++) states[i]=Named("note_block");
    for(i=3042;i<4338;i++) {
        /* Redstone connections are flattened initially; power remains visible. */
        j=((i-3042)/9)%16;
        states[i]=(BlockID)(nextBlock+j);
    }
    for(i=0;i<16;i++) {
        b=Clone(Named("redstone_block"));Box(b,0,0,0,1,.025f,1);Blocks.Collide[b]=COLLIDE_NONE;
        Blocks.BlocksLight[b]=false;Blocks.FogCol[b]=PackedCol_Make(90+i*10,20,20,255);
        text=String_FromReadonly("redstone #");Block_SetName(b,&text);Define(b);
    }
    for(i=0;i<8;i++) states[5918+i]=Named("redstone_torch");
    states[5917]=Clone(Named("redstone_torch"));Blocks.Brightness[states[5917]]=0;Define(states[5917]);
    states[5802]=states[5803]=Named("lever");
    states[5826]=states[5827]=Named("stone_pressure_plate");
    states[5892]=states[5893]=Named("oak_pressure_plate");
    for(i=0;i<16;i++) { states[86+i]=Named("water");states[102+i]=Named("lava"); }
    for(i=0;i<9;i++) states[2402+i]=Named("torch");
}
BlockID LapisBlocks_State(int state) { return state>=0 && state<65536?states[state]:256; }
BlockID LapisBlocks_Item(int item) { return item>=0 && item<1416?items[item]:0; }
const char* LapisBlocks_ItemName(int item) {
    int i;
    if(!item)return "empty";
    for(i=0;i<LAPIS_ITEM_FACT_COUNT;i++) if(Lapis_ItemFacts[i].id==item) return Lapis_ItemFacts[i].name;
    return item?"unknown":"empty";
}
int LapisBlocks_ItemIcon(int item) {
    int i;
    for(i=0;i<LAPIS_ITEM_FACT_COUNT;i++) if(Lapis_ItemFacts[i].id==item) return Lapis_ItemFacts[i].icon;
    return -1;
}
