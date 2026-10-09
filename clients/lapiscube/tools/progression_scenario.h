/* Milestone 7 acceptance: empty inventory, natural terrain, ordinary requests.
   This test-only input driver is not game AI, terrain generation or a recipe engine. */
#include "LapisMining.h"
static int progression, resumeProgression, freshStage, freshSub, freshSource, freshBefore;
static int freshX,freshY,freshZ,treeX,treeY,treeZ,logsMined,mineY,mineDelay,mineStone;
static cc_uint64 freshAt, freshStepAt;
struct FreshNode { int x,y,z,parent; };
static struct FreshNode freshNodes[2048];
static int freshRoute[128],freshRouteSize,freshRouteAt,freshWalkStep;
static const char* FreshBlock(int state) {
    int i;for(i=0;i<256;i++)if(Lapis_BlockFacts[i].state==state)return Lapis_BlockFacts[i].name;
    return "unknown";
}
static int FreshAir(int x,int y,int z) {
    const char* name=FreshBlock(LapisWorld_Get(&world,x,y,z));
    return !strcmp(name,"air") || !strcmp(name,"short_grass") || !strcmp(name,"fern");
}
static int FreshStand(int x,int y,int z) {
    int ground=LapisWorld_Get(&world,x,y-1,z);
    return ground>0 && !(ground>=86 && ground<=117) && FreshAir(x,y,z) && FreshAir(x,y+1,z);
}
static void FreshNext(const char* message) {
    if(message)printf("progression: %s\n",message);
    freshStage++;freshSub=0;freshAt=Now();
}
static int FreshRoute(void) {
    static const int dx[4]={1,-1,0,0},dz[4]={0,0,1,-1};
    int head,tail=1,i,j,k,y,n,found=-1;
    struct FreshNode node;
    freshNodes[0].x=freshX;freshNodes[0].y=freshY;freshNodes[0].z=freshZ;freshNodes[0].parent=-1;
    for(head=0;head<tail && tail<2044;head++) {
        node=freshNodes[head];
        for(i=0;i<4;i++) {
            for(y=node.y-1;y<=node.y+1;y++) {
                for(k=0;k<4;k++)if(strcmp(FreshBlock(LapisWorld_Get(&world,node.x+dx[i],y+k,node.z+dz[i])),"oak_log"))break;
                if(k==4) { treeX=node.x+dx[i];treeY=y;treeZ=node.z+dz[i];found=head;break; }
            }
            if(found>=0)break;
            if(abs(node.x+dx[i]-freshX)>12 || abs(node.z+dz[i]-freshZ)>12)continue;
            for(j=0;j<3;j++) {
                y=node.y+(j==1?1:j==2?-1:0);
                if(abs(y-freshY)>6 || !FreshStand(node.x+dx[i],y,node.z+dz[i]))continue;
                for(n=0;n<tail;n++)if(freshNodes[n].x==node.x+dx[i] && freshNodes[n].y==y && freshNodes[n].z==node.z+dz[i])break;
                if(n==tail) { freshNodes[tail].x=node.x+dx[i];freshNodes[tail].y=y;freshNodes[tail].z=node.z+dz[i];freshNodes[tail++].parent=head; }
                break;
            }
        }
        if(found>=0)break;
    }
    if(found<0)return 0;
    freshRouteSize=0;
    while(found>0 && freshRouteSize<128) { freshRoute[freshRouteSize++]=found;found=freshNodes[found].parent; }
    if(found<0 || freshRouteSize==128)return 0;
    freshRouteAt=freshRouteSize-1;return 1;
}
/* Fill exact empty cells with one ingredient each, returning surplus to its
   original slot. Every subsequent step waits for server cursor/slot updates. */
static int FreshFill(struct LapisProtocol* p,const char* name,const int* slots,int count) {
    int i,item=Item(name);
    if(!freshSub) {
        freshSource=FindItem(gameplay.window,item);
        if(freshSource<0 || gameplay.slots[gameplay.window][freshSource].count<count) { LapisProtocol_Fail(p,"Progression ingredient missing");return 0; }
        for(i=0;i<count;i++)if(gameplay.slots[gameplay.window][slots[i]].count) { LapisProtocol_Fail(p,"Progression expected empty crafting cells");return 0; }
        LapisGameplay_Click(&gameplay,p,freshSource,0,0);freshSub=1;
    } else if(freshSub==1 && gameplay.cursor.item==item && gameplay.cursor.count>=count) {
        for(i=0;i<count;i++)LapisGameplay_Click(&gameplay,p,slots[i],1,0);
        LapisGameplay_Click(&gameplay,p,freshSource,0,0);freshSub=2;
    } else if(freshSub==2 && !gameplay.cursor.count) {
        for(i=0;i<count;i++)if(gameplay.slots[gameplay.window][slots[i]].item!=item || gameplay.slots[gameplay.window][slots[i]].count!=1)return 0;
        freshSub=0;return 1;
    }
    return 0;
}
static int FreshTake(struct LapisProtocol* p,const char* name,int count) {
    int i,item=Item(name);
    if(!freshSub && gameplay.slots[gameplay.window][0].item==item) {
        freshBefore=CountItem(item);LapisGameplay_Click(&gameplay,p,0,0,1);freshSub=1;
    } else if(freshSub && CountItem(item)==freshBefore+count && !gameplay.cursor.count) {
        /* items_insert arrives before the consumed crafting-cell updates. */
        for(i=0;i<=(gameplay.window==12?9:4);i++)if(gameplay.slots[gameplay.window][i].count)return 0;
        freshSub=0;return 1;
    }
    return 0;
}
static void Fresh(struct LapisProtocol* p) {
    static const int quad[4]={1,2,3,4},vertical[2]={1,3},top[3]={1,2,3},handle[2]={5,8};
    int i,item,state;struct FreshNode node;double fraction;
    if(!p->loaded || p->state!=LAPIS_PLAY)return;
    if(freshStage && Now()-freshAt>20000) { fprintf(stderr,"progression timed out stage=%d sub=%d window=%d cursor=%d/%d\n",freshStage,freshSub,gameplay.window,gameplay.cursor.item,gameplay.cursor.count);LapisProtocol_Fail(p,"Progression stage deadline");return; }
    switch(freshStage) {
    case 0:
        if(!p->keepalives)return; /* Initial inventory synchronization precedes it. */
        if(resumeProgression) {
            if(CountItem(Item("cobblestone"))!=1 || CountItem(Item("oak_planks"))!=7 || CountItem(Item("stick"))!=2) { LapisProtocol_Fail(p,"Reconnect lost earned inventory");return; }
            freshStage=30;puts("progression: earned inventory survived same-identity reconnect");return;
        }
        for(i=0;i<46;i++)if(gameplay.slots[0][i].count) { LapisProtocol_Fail(p,"Fresh progression must start empty");return; }
        freshX=(int)p->x;freshY=(int)p->y;freshZ=(int)p->z;
        if(!FreshRoute()) { LapisProtocol_Fail(p,"No walkable route to natural wood in test area");return; }
        printf("progression: empty inventory, spawn=%d,%d,%d natural tree=%d,%d,%d path=%d steps\n",freshX,freshY,freshZ,treeX,treeY,treeZ,freshRouteSize);
        FreshNext(NULL);break;
    case 1:
        if(freshRouteAt<0) { FreshNext("walked a terrain-checked route to a natural tree");break; }
        if(Now()-freshStepAt<50)return;
        freshStepAt=Now();node=freshNodes[freshRoute[freshRouteAt]];fraction=(double)++freshWalkStep/5;
        LapisGameplay_Move(p,freshX+.5+(node.x-freshX)*fraction,freshY+(node.y-freshY)*fraction,freshZ+.5+(node.z-freshZ)*fraction,0,0,freshWalkStep==5);
        if(freshWalkStep==5) { freshX=node.x;freshY=node.y;freshZ=node.z;freshWalkStep=0;freshRouteAt--; }
        break;
    case 2:
        LapisGameplay_Select(p,8);LapisGameplay_Dig(&gameplay,p,0,treeX,treeY+logsMined,treeZ,1);freshStepAt=Now();FreshNext(NULL);break;
    case 3:
        if(Now()-freshStepAt>=(cc_uint64)LapisMining_Delay("oak_log","empty",0)) { LapisGameplay_Dig(&gameplay,p,2,treeX,treeY+logsMined,treeZ,1);FreshNext(NULL); }break;
    case 4:
        if(CountItem(Item("oak_log"))==logsMined+1 && LapisWorld_Get(&world,treeX,treeY+logsMined,treeZ)==0) {
            logsMined++;if(logsMined<4) { freshStage=2;freshAt=Now(); }else FreshNext("mined and picked up four natural logs");
        }break;
    case 5:
        i=FindItem(0,Item("oak_log"));if(i>=0) { LapisGameplay_Click(&gameplay,p,i,0,0);FreshNext(NULL); }break;
    case 6: if(gameplay.cursor.count==4) { LapisGameplay_Click(&gameplay,p,1,0,0);FreshNext(NULL); }break;
    case 7: if(!gameplay.cursor.count && gameplay.slots[0][1].count==4) { LapisGameplay_Close(&gameplay,p);LapisGameplay_Refresh(&gameplay,p);FreshNext(NULL); }break;
    case 8:
        if(!gameplay.refreshMask && CountItem(Item("oak_log"))==4 && !gameplay.slots[0][1].count && !gameplay.slots[0][0].count) {
            i=FindItem(0,Item("oak_log"));LapisGameplay_Click(&gameplay,p,i,0,0);FreshNext("close/reopen refreshed crafting cells without losing logs");
        }break;
    case 9: if(gameplay.cursor.count==4) { LapisGameplay_Click(&gameplay,p,1,0,0);FreshNext(NULL); }break;
    case 10: if(FreshTake(p,"oak_planks",16))FreshNext("crafted sixteen planks from harvested logs");break;
    case 11: if(FreshFill(p,"oak_planks",quad,4))FreshNext(NULL);break;
    case 12: if(FreshTake(p,"crafting_table",1))FreshNext("crafted the first workbench");break;
    case 13: if(FreshFill(p,"oak_planks",vertical,2))FreshNext(NULL);break;
    case 14: if(FreshTake(p,"stick",4)) { i=FindItem(0,Item("crafting_table"));LapisGameplay_Swap(&gameplay,p,i,8);FreshNext(NULL); }break;
    case 15:
        if(gameplay.slots[0][44].item==Item("crafting_table")) { LapisGameplay_Close(&gameplay,p);LapisGameplay_Select(p,8);LapisGameplay_UseOn(&gameplay,p,treeX,treeY-1,treeZ,1,.5f,1,.5f);FreshNext(NULL); }break;
    case 16:
        if(!strcmp(FreshBlock(LapisWorld_Get(&world,treeX,treeY,treeZ)),"crafting_table") && !CountItem(Item("crafting_table"))) {
            LapisGameplay_UseOn(&gameplay,p,treeX,treeY,treeZ,1,.5f,1,.5f);FreshNext("placed the crafted workbench with authoritative item consumption");
        }break;
    case 17: if(gameplay.window==12 && FreshFill(p,"oak_planks",top,3))FreshNext(NULL);break;
    case 18: if(FreshFill(p,"stick",handle,2))FreshNext(NULL);break;
    case 19: if(FreshTake(p,"wooden_pickaxe",1)) { i=FindItem(12,Item("wooden_pickaxe"));LapisGameplay_Swap(&gameplay,p,i,7);FreshNext("crafted a wooden pickaxe in the placed workbench"); }break;
    case 20:
        if(gameplay.slots[0][43].item==Item("wooden_pickaxe")) { LapisGameplay_Close(&gameplay,p);freshBefore=CountItem(Item("oak_planks"));i=FindItem(0,Item("oak_planks"));LapisGameplay_DropSlot(&gameplay,p,i,1);FreshNext(NULL); }break;
    case 21: if(CountItem(Item("oak_planks"))==0)FreshNext(NULL);break;
    case 22: if(CountItem(Item("oak_planks"))==freshBefore)FreshNext("whole-stack drop and pickup preserved the earned planks");break;
    case 23:
        mineY=freshY-1;state=LapisWorld_Get(&world,freshX,mineY,freshZ);
        if(mineY<treeY-10 || (state!=1 && state!=9 && state!=10)) { LapisProtocol_Fail(p,"Unexpected terrain in bounded progression dig");return; }
        mineStone=state==1;item=mineStone?7:8;LapisGameplay_Select(p,item);
        mineDelay=LapisMining_Delay(FreshBlock(state),state==1?"wooden_pickaxe":"empty",0);
        LapisGameplay_Dig(&gameplay,p,0,freshX,mineY,freshZ,1);freshStepAt=Now();FreshNext(NULL);break;
    case 24: if(Now()-freshStepAt>=(cc_uint64)mineDelay) { LapisGameplay_Dig(&gameplay,p,2,freshX,mineY,freshZ,1);FreshNext(NULL); }break;
    case 25:
        if(LapisWorld_Get(&world,freshX,mineY,freshZ)==0) { freshWalkStep=0;FreshNext(NULL); }break;
    case 26:
        if(Now()-freshStepAt<50)return;
        freshStepAt=Now();freshWalkStep++;LapisGameplay_Move(p,freshX+.5,freshY-(double)freshWalkStep/5,freshZ+.5,0,0,freshWalkStep==5);
        if(freshWalkStep==5) { freshY--;FreshNext(NULL); }break;
    case 27:
        if(CountItem(Item("cobblestone"))==1)FreshNext("used the crafted pickaxe to mine and collect natural stone");
        else if(!mineStone && Now()-freshAt>750) { freshStage=23;freshAt=Now(); }
        break;
    case 28: LapisGameplay_Refresh(&gameplay,p);FreshNext(NULL);break;
    case 29: if(!gameplay.refreshMask && !gameplay.cursor.count)FreshNext("fresh-world wood-to-tools progression passed");break;
    default:break;
    }
}
