/* Isolated acceptance-save generator. It uses the pinned server's public save
 * structs, not a patched server or protocol authority bypass. Never shipped. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "globals.h"
#include "registries.h"
static BlockChange edits[MAX_BLOCK_CHANGES];
static PlayerData players[MAX_PLAYERS];
static unsigned used;
static void block(int x,int y,int z,int id) {
    BlockChange* b=&edits[used++];b->x=(short)x;b->y=(uint8_t)y;b->z=(short)z;b->block=(uint8_t)id;
    if(id==B_chest) { memset(edits+used,0,14*sizeof(BlockChange));used+=14; }
}
int main(int argc,char** argv) {
    unsigned i,v;int x,y,z;FILE* out;
    const uint16_t items[]={I_oak_log,I_cobblestone,I_coal,I_apple,I_oak_sign,I_oak_planks,I_iron_sword,I_oak_wood};
    const uint8_t counts[]={4,8,1,2,1,16,1,2};
    uint8_t meta[25]={'L','A','P','I','S','O','B','S',0,0,0,3,0,0,3,4,0,0,0,0,0,0,0,42,0};
    if(argc!=3 || strlen(argv[1])!=32 || strlen(argv[2])!=32)return 1;
    for(i=0;i<MAX_BLOCK_CHANGES;i++)edits[i].block=255;
    for(i=0;i<MAX_PLAYERS;i++)players[i].client_fd=-1;
    for(i=0;i<16;i++) { if(sscanf(argv[1]+i*2,"%2x",&v)!=1)return 1;players[0].uuid[i]=(uint8_t)v; }
    for(i=0;i<16;i++) { if(sscanf(argv[2]+i*2,"%2x",&v)!=1)return 1;players[1].uuid[i]=(uint8_t)v; }
    strcpy(players[1].name,"CombatCube");players[1].x=9;players[1].y=70;players[1].z=8;
    players[1].health=4;players[1].hunger=10;players[1].grounded_y=70;players[1].saturation=200;
    strcpy(players[0].name,"SurvivalCube");players[0].x=8;players[0].y=70;players[0].z=8;
    players[0].grounded_y=70;players[0].health=12;players[0].hunger=10;
    for(i=0;i<sizeof(counts);i++) { players[0].inventory_items[i]=items[i];players[0].inventory_count[i]=counts[i]; }
    for(x=4;x<=12;x++)for(z=4;z<=12;z++)for(y=69;y<=74;y++)block(x,y,z,y==69?B_stone:B_air);
    /* Replace the earlier air edits instead of adding duplicate coordinates. */
    for(i=0;i<used;i++)if(edits[i].y==70 && edits[i].x==10 && edits[i].z>=8 && edits[i].z<=10)edits[i].block=255;
    block(10,70,8,B_crafting_table);block(10,70,9,B_chest);block(10,70,10,B_furnace);
    out=fopen("world.bin","wb");if(!out)return 1;
    if(fwrite(edits,sizeof(edits),1,out)!=1 || fwrite(players,sizeof(players),1,out)!=1 || fclose(out))return 1;
    out=fopen("world.meta","wb");if(!out)return 1;
    if(fwrite(meta,sizeof(meta),1,out)!=1 || fclose(out))return 1;
    puts("Seeded isolated survival acceptance save: supplies, workstations, health/food; ordinary server rules");return 0;
}
