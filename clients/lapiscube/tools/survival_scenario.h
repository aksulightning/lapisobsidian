/* Real-server scenario using the product's authoritative state and action API.
   Test supplies come from an explicitly seeded server save, never local client slots. */
#include "LapisFacts.h"
#include "LapisSigns.h"
static struct LapisSigns signs;
static int scenario,combat,combatStage;
static unsigned combatAt;
static int combatReady;
static int Item(const char* name) {
    int i;
    for(i=0;i<LAPIS_ITEM_FACT_COUNT;i++)if(!strcmp(Lapis_ItemFacts[i].name,name))return Lapis_ItemFacts[i].id;
    return 0;
}
static int CountItem(int item) {
    int i,n=0;
    for(i=9;i<45;i++)if(gameplay.slots[0][i].item==item)n+=gameplay.slots[0][i].count;
    return n;
}
static int FindItem(int window,int item) {
    int i,start=window==2?27:window==12?10:window==14?3:9,end=window==2?63:window==14?39:window==12?46:45;
    for(i=start;i<end;i++)if(gameplay.slots[window][i].item==item && gameplay.slots[window][i].count)return i;
    return -1;
}
static void Next(const char* message) {
    if(message)printf("survival: %s\n",message);
    stage++;actionTime=Now();
}
static void PickItem(struct LapisProtocol* p,int item) {
    int found=FindItem(gameplay.window,item);
    if(found<0) { LapisProtocol_Fail(p,"Scenario inventory item missing");return; }
    LapisGameplay_Click(&gameplay,p,found,0,0);Next(NULL);
}
static void Survival(struct LapisProtocol* p) {
    int item;struct LapisSign* sign;
    static const char signText[4][LAPIS_SIGN_LINE]={"LapisCube","Hyv\xC3\xA4\xC3\xA4 iltaa","Server-owned","\xF0\x9F\x8C\x9F"};
    if(p->state!=LAPIS_PLAY)return;
    if(stage && Now()-actionTime>15000) { fprintf(stderr,"survival timed out at stage %d window=%d cursor=%d/%d\n",stage,gameplay.window,gameplay.cursor.item,gameplay.cursor.count);LapisProtocol_Fail(p,"Survival scenario deadline");return; }
    if(!p->loaded)return;
    switch(stage) {
    case 0: if(CountItem(Item("oak_log"))==4)PickItem(p,Item("oak_log"));break;
    case 1: if(gameplay.cursor.count==4) { LapisGameplay_Click(&gameplay,p,1,0,0);Next(NULL); }break;
    case 2: if(!gameplay.cursor.count && gameplay.slots[0][0].item==Item("oak_planks")) { LapisGameplay_Click(&gameplay,p,0,0,1);Next(NULL); }break;
    case 3: if(!gameplay.slots[0][1].count && CountItem(Item("oak_planks"))==32) { LapisGameplay_Close(&gameplay,p);LapisGameplay_UseOn(&gameplay,p,10,70,8,1,.5f,1,.5f);Next("2x2 crafting consumed four logs and produced sixteen planks"); }break;
    case 4: if(gameplay.window==12)PickItem(p,Item("cobblestone"));break;
    case 5: if(gameplay.cursor.count==8) { for(item=1;item<=9;item++)if(item!=5)LapisGameplay_Click(&gameplay,p,item,1,0);Next(NULL); }break;
    case 6: if(!gameplay.cursor.count && gameplay.slots[12][0].item==Item("furnace")) { LapisGameplay_Click(&gameplay,p,0,0,1);Next(NULL); }break;
    case 7: if(CountItem(Item("furnace"))==1 && !gameplay.slots[12][1].count) { LapisGameplay_Close(&gameplay,p);LapisGameplay_UseOn(&gameplay,p,10,70,9,1,.5f,1,.5f);Next("3x3 crafting produced a furnace with no local predictions"); }break;
    case 8: if(gameplay.window==2)PickItem(p,Item("furnace"));break;
    case 9: if(gameplay.cursor.item==Item("furnace")) { LapisGameplay_Click(&gameplay,p,0,0,0);Next(NULL); }break;
    case 10: if(!gameplay.cursor.count && gameplay.slots[2][0].item==Item("furnace")) { LapisGameplay_Close(&gameplay,p);LapisGameplay_UseOn(&gameplay,p,10,70,9,1,.5f,1,.5f);Next(NULL); }break;
    case 11: if(gameplay.window==2 && gameplay.slots[2][0].item==Item("furnace")) { LapisGameplay_Click(&gameplay,p,0,0,1);Next(NULL); }break;
    case 12: if(!gameplay.slots[2][0].count && CountItem(Item("furnace"))==1) { LapisGameplay_Close(&gameplay,p);LapisGameplay_UseOn(&gameplay,p,10,70,10,1,.5f,1,.5f);Next("chest deposit, reopen and withdrawal preserved the stack"); }break;
    case 13: if(gameplay.window==14)PickItem(p,Item("oak_planks"));break;
    case 14: if(gameplay.cursor.count==32) { /* fuel: planks; input: apple is unsupported, verify no invented result */ LapisGameplay_Click(&gameplay,p,1,1,0);LapisGameplay_Click(&gameplay,p,30,0,0);Next(NULL); }break;
    case 15: if(!gameplay.cursor.count && gameplay.slots[14][1].count==1)PickItem(p,Item("apple"));break;
    case 16: if(gameplay.cursor.count==2) { LapisGameplay_Click(&gameplay,p,0,0,0);Next(NULL); }break;
    case 17: if(!gameplay.cursor.count && gameplay.slots[14][0].count==2) { if(gameplay.slots[14][2].count) { LapisProtocol_Fail(p,"Invalid furnace recipe produced output");return; }LapisGameplay_Click(&gameplay,p,0,0,1);Next(NULL); }break;
    case 18: if(!gameplay.slots[14][0].count && CountItem(Item("apple"))==2) { LapisGameplay_Close(&gameplay,p);Next("unsupported smelting recipe retained inputs and produced no output"); }break;
    case 19: /* Reopen with a valid recipe after the negative recipe test. */
        LapisGameplay_UseOn(&gameplay,p,10,70,10,1,.5f,1,.5f);Next(NULL);break;
    case 20: if(gameplay.window==14)PickItem(p,Item("coal"));break;
    case 21: if(gameplay.cursor.count==1) { LapisGameplay_Click(&gameplay,p,1,0,0);Next(NULL); }break;
    case 22: if(!gameplay.cursor.count && gameplay.slots[14][1].item==Item("coal"))PickItem(p,Item("oak_wood"));break;
    case 23: if(gameplay.cursor.count) { LapisGameplay_Click(&gameplay,p,0,0,0);Next(NULL); }break;
    case 24: if(gameplay.slots[14][2].item==Item("charcoal") && !gameplay.slots[14][0].count) { LapisGameplay_Click(&gameplay,p,2,0,1);Next(NULL); }break;
    case 25: if(CountItem(Item("charcoal"))>0) { LapisGameplay_Close(&gameplay,p);Next("furnace consumed fuel and input and returned server charcoal"); }break;
    case 26: PickItem(p,Item("apple"));break;
    case 27: if(gameplay.cursor.item==Item("apple")) { LapisGameplay_Click(&gameplay,p,44,0,0);Next(NULL); }break;
    case 28: if(!gameplay.cursor.count && gameplay.slots[0][44].item==Item("apple")) { LapisGameplay_Close(&gameplay,p);LapisGameplay_Select(p,8);LapisGameplay_Use(&gameplay,p,0,0);Next(NULL); }break;
    case 29: if(CountItem(Item("apple"))==1 && gameplay.food>10) { LapisGameplay_Dig(&gameplay,p,5,0,0,0,0);LapisGameplay_Select(p,4);LapisGameplay_UseOn(&gameplay,p,7,69,8,1,.5f,1,.5f);Next("held food consumed once and raised authoritative hunger"); }break;
    case 30: if(signs.editing) { LapisSigns_Submit(&signs,p,signText);Next(NULL); }break;
    case 31: sign=LapisSigns_Find(&signs,7,70,8);if(sign && !strcmp(sign->lines[0][1],signText[1]) && !strcmp(sign->lines[0][3],signText[3])) { Next("sign placement and UTF-8/modified-UTF-8 edit echo passed"); }break;
    case 32:
        for(item=0;item<LAPIS_MAX_ENTITIES;item++)if(entities.list[item].used && entities.list[item].type==149) {
            LapisGameplay_Select(p,6);LapisGameplay_Attack(p,entities.list[item].id);Next("sent sword attack to the second test client");break;
        }
        break;
    case 33: if(Now()-actionTime>=1200) { LapisGameplay_Chat(p,"/spawn",6);Next(NULL); }break;
    case 34: if(p->teleports>=3 && p->y!=70) { LapisGameplay_Respawn(p);Next("server command teleport synchronized"); }break;
    case 35: if(p->loaded && p->teleports==2 && gameplay.health==20 && !CountItem(Item("charcoal")))Next("respawn rebuilt the world and reset inventory/health");break;
    default:break;
    }
}

static void Combat(struct LapisProtocol* p) {
    if(!combatReady && p->loaded) { puts("combat: ready");combatReady=1; }
    if(!combatStage && p->loaded && gameplay.health==0) {
        puts("combat: authoritative lethal damage received");LapisGameplay_Respawn(p);combatAt=p->packets;combatStage=1;
    } else if(combatStage==1 && p->packets>combatAt && p->loaded && p->teleports==2 && gameplay.health==20) {
        puts("combat: death and respawn round trip passed");combatStage=2;
    }
}
