/* Test-only authenticated command scenario. The reference server is unchanged.
   This validates streaming/teleports and presentation packets, not survival travel. */
static int travel, travelStage, destination, spawnIndex, travelPending;
static unsigned travelTeleports, travelChunks, travelDamage;
static cc_uint64 travelAt;
static const int destinations[][3]={{17,150,17},{-17,150,-17},{33,150,-33},{3950,150,8},{-3950,150,-8},{8,70,8}};
static const char* const species[]={"chicken","cow","pig","sheep","zombie","skeleton","spider","creeper"};
static const int speciesIds[]={25,28,95,106,145,110,119,30};
static const int spawnPoints[][2]={{5,5},{5,8},{5,11},{8,5},{8,11},{11,8},{11,5},{11,11}};
static int Species(int type) {
    int i;for(i=0;i<LAPIS_MAX_ENTITIES;i++)if(entities.list[i].used && entities.list[i].type==type)return i;
    return -1;
}
static void Command(struct LapisProtocol* p,const char* text) {
    gameplay.message[0]=0;LapisGameplay_Chat(p,text,(int)strlen(text));travelAt=Now();
}
static void Travel(struct LapisProtocol* p) {
    char command[160];const char* token;int i,count,cx,cz;struct LapisEntity* entity;
    if(!p->loaded || p->state!=LAPIS_PLAY)return;
    if(travelStage && Now()-travelAt>15000) {
        fprintf(stderr,"travel timeout stage=%d destination=%d species=%d message=%s\n",travelStage,destination,spawnIndex,gameplay.message);
        LapisProtocol_Fail(p,"Travel/presentation acceptance deadline");return;
    }
    if(!travelStage) {
        token=getenv("LAPIS_TEST_ADMIN");if(!token || strlen(token)>100) { LapisProtocol_Fail(p,"Missing isolated test credential");return; }
        sprintf(command,"/admin %s",token);Command(p,command);travelStage=1;
    } else if(travelStage==1 && strstr(gameplay.message,"Administrator access enabled")) {
        Command(p,"/gamemode creative");travelStage=2;
    } else if(travelStage==2 && p->gamemode==1) {
        travelStage=3;travelAt=Now()-2500;
    } else if(travelStage==3 && Now()-travelAt>=2300 && !travelPending) {
        sprintf(command,"/tp %d %d %d",destinations[destination][0],destinations[destination][1],destinations[destination][2]);
        travelTeleports=p->teleports;travelChunks=world.decoded;Command(p,command);travelPending=1;
    } else if(travelStage==3 && travelPending && p->teleports>travelTeleports && world.decoded>=travelChunks+25) {
        cx=LapisWorld_ChunkCoord(destinations[destination][0]);cz=LapisWorld_ChunkCoord(destinations[destination][2]);
        if(world.centerX!=cx || world.centerZ!=cz || p->x!=destinations[destination][0]+.5 || p->z!=destinations[destination][2]+.5) {
            LapisProtocol_Fail(p,"Travel origin/teleport mismatch");return;
        }
        count=0;for(i=0;i<LAPIS_CACHE_COUNT;i++)if(world.chunks[i].valid) {
            if(world.chunks[i].x<cx-3 || world.chunks[i].x>cx+3 || world.chunks[i].z<cz-3 || world.chunks[i].z>cz+3) {
                LapisProtocol_Fail(p,"Travel retained an evicted chunk");return;
            }
            count++;
        }
        if(count<25 || !LapisWorld_Find(&world,cx,cz)) { LapisProtocol_Fail(p,"Travel view incomplete");return; }
        printf("travel: (%d,%d) center=(%d,%d) cached=%d decoded=%u evicted=%u\n",destinations[destination][0],destinations[destination][2],cx,cz,count,world.decoded,world.evicted);
        destination++;travelPending=0;
        if(destination==6) { Command(p,"/time set night");travelStage=4; }
    } else if(travelStage==4 && strstr(gameplay.message,"Time updated")) {
        travelStage=5;travelAt=Now();
    } else if(travelStage==5 && !travelPending) {
        sprintf(command,"/spawnmob %s %d 70 %d",species[spawnIndex],spawnPoints[spawnIndex][0],spawnPoints[spawnIndex][1]);
        Command(p,command);travelPending=1;
    } else if(travelStage==5 && strstr(gameplay.message,"Spawned ") && Species(speciesIds[spawnIndex])>=0) {
        printf("presentation: authoritative %s spawn decoded\n",species[spawnIndex]);spawnIndex++;travelPending=0;travelAt=Now();
        if(spawnIndex==8)travelStage=6;
    } else if(travelStage==6 && (i=Species(110))>=0 && entities.list[i].mainHand==858) {
        puts("presentation: skeleton main-hand bow equipment decoded");
        i=Species(28);if(i<0)return;entity=&entities.list[i];
        /* Cow starts three blocks away on the saved platform. */
        LapisGameplay_Move(p,p->x,p->y,p->z,0,0,1);travelDamage=entities.damageEvents;
        LapisGameplay_Attack(p,entity->id);travelStage=7;travelAt=Now();
    } else if(travelStage==7 && entities.damageEvents>travelDamage) {
        puts("presentation: real mob damage event decoded; all eight species and equipment passed");travelStage=8;
    }
}
