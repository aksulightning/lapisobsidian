#include "LapisMining.h"
#include <string.h>
static int Has(const char* value,const char* part) { return strstr(value,part)!=NULL; }
static int Instant(const char* block) {
    static const char* names[]={"dandelion","torchflower","poppy","blue_orchid","allium","azure_bluet",
        "red_tulip","orange_tulip","white_tulip","pink_tulip","oxeye_daisy","cornflower",
        "wither_rose","lily_of_the_valley","brown_mushroom","red_mushroom","fern","dead_bush",
        "short_grass","torch","redstone_torch","lever","lily_pad","oak_sapling"};
    unsigned i;for(i=0;i<sizeof(names)/sizeof(names[0]);i++)if(!strcmp(block,names[i]))return 1;
    return 0;
}
int LapisMining_Delay(const char* block,const char* tool,int mode) {
    int time=1200,speed=1,correct=0;
    if(mode<0 || mode>1 || !strcmp(block,"air") || Has(block,"water") || Has(block,"lava"))return -1;
    if(mode==1)return 0;
    if(!strcmp(block,"bedrock"))return -1;
    /* Instant cases follow isInstantlyMined in the pinned server. */
    if(!strcmp(block,"snow") || !strcmp(block,"snow_block")) {
        if(Has(tool,"_shovel") && !Has(tool,"wooden_"))return 0;
    }
    if(!strcmp(block,"oak_leaves") && !strcmp(tool,"shears"))return 0;
    if(Instant(block))return 0;
    if(Has(block,"dirt") || !strcmp(block,"grass_block") || !strcmp(block,"sand") || !strcmp(block,"gravel") ||
       Has(block,"snow") || !strcmp(block,"farmland") || !strcmp(block,"clay")) {
        time=750;correct=Has(tool,"_shovel");
    } else if(Has(block,"leaves") || !strcmp(block,"wheat") || !strcmp(block,"sugar_cane")) {
        time=250;correct=!strcmp(tool,"shears");
    } else if(Has(block,"oak") || Has(block,"wood") || Has(block,"chest") ||
              !strcmp(block,"crafting_table") || !strcmp(block,"note_block") || !strcmp(block,"jukebox")) {
        time=1800;correct=Has(tool,"_axe");
    } else if(Has(block,"stone") || Has(block,"ore") || Has(block,"brick") || Has(block,"furnace") ||
              Has(block,"obsidian") || Has(block,"iron") || Has(block,"gold") || Has(block,"diamond")) {
        correct=Has(tool,"_pickaxe");time=Has(block,"obsidian")?16000:3000;
        if(!correct)time*=3;
    }
    if(correct) {
        if(Has(tool,"wooden_"))speed=2;
        else if(Has(tool,"stone_"))speed=3;
        else if(Has(tool,"iron_"))speed=4;
        else if(Has(tool,"diamond_"))speed=5;
        else if(Has(tool,"netherite_"))speed=6;
        else if(Has(tool,"golden_"))speed=7;
        else speed=2;
    }
    time/=speed;return time<100?100:time;
}
