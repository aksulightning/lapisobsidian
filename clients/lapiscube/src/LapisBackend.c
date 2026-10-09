#include "LapisBackend.h"
#include "LapisSession.h"
#include "LapisIdentity.h"
#include "Server.h"
#include "Game.h"
#include "Platform.h"
#include "String_.h"
#include "Screens.h"
#include "Event.h"
#include "Block.h"
#include "Chat.h"
#include "LapisWorld.h"
#include "LapisSigns.h"
#include "LapisMining.h"
#include "LapisBlocks.h"
#include "LapisGui.h"
#include "LapisMobs.h"
#include "LapisAudio.h"
#include "TexturePack.h"
#include "World.h"
#include "MapRenderer.h"
#include "Lighting.h"
#include "Entity.h"
#include "Inventory.h"
#include "Picking.h"
#include "InputHandler.h"
#include "Input.h"
#include "Gui.h"
#include "ExtMath.h"
#include <string.h>

cc_bool LapisBackend_Enabled;
#ifdef CC_BUILD_NETWORKING
static struct LapisSession session;
static cc_socket connection = -1;
static cc_bool connecting;
static unsigned lastStage;
static struct LapisWorld world;
static struct LapisGameplay gameplay;
static struct LapisEntities entities;
static struct LapisSigns signs;
static int originX, originZ, mapDirty, ready, selected, sneak, sprint;
static int digging, digX, digY, digZ, digFace, digState, digItem, digSlot, digDuration;
static int usingItem;
static cc_uint64 digStart, lastUse;

struct LapisGameplay* LapisBackend_Gameplay(void) { return &gameplay; }
struct LapisSigns* LapisBackend_Signs(void) { return &signs; }
struct LapisProtocol* LapisBackend_Protocol(void) { return &session.protocol; }
static cc_uint64 Now(void);
static float JavaYaw(float yaw) { return yaw - 180.0f; }
static float JavaPitch(float pitch) { return pitch > 180 ? pitch - 360 : pitch; }
static void Position(struct LapisProtocol* p) {
    struct LocationUpdate u; struct Entity* e=&Entities.CurPlayer->Base;
    memset(&u,0,sizeof(u));
    u.flags=LU_HAS_POS|LU_HAS_YAW|LU_HAS_PITCH;
    Vec3_Set(u.pos,(float)(p->x-originX),(float)p->y,(float)(p->z-originZ));
    u.yaw=p->yaw+180;u.pitch=p->pitch;
    e->VTABLE->SetLocation(e,&u);Vec3_Set(e->Velocity,0,0,0);
}
static void Recenter(void) {
    int x=(world.centerX-3)*16,z=(world.centerZ-3)*16;
    struct LocationUpdate u;struct Entity* e=&Entities.CurPlayer->Base;
    if(ready && (x!=originX || z!=originZ)) {
        memset(&u,0,sizeof(u));u.flags=LU_HAS_POS|LU_POS_RELATIVE_SHIFT;
        Vec3_Set(u.pos,(float)(originX-x),0,(float)(originZ-z));e->VTABLE->SetLocation(e,&u);
    }
    originX=x;originZ=z;mapDirty=1;
}
static void FillMap(void) {
    int i,x,y,z,dx,dz,index;BlockID b;struct LapisChunk* c;
    if(!World.Blocks)return;
    /* Unknown chunks are invisible solid barriers until their authoritative data arrives. */
    memset(World.Blocks,255,(size_t)World.Volume);
    memset(World.Blocks2,0,(size_t)World.Volume);
    for(i=0;i<LAPIS_CACHE_COUNT;i++) {
        c=&world.chunks[i];if(!c->valid)continue;
        dx=c->x*16-originX;dz=c->z*16-originZ;
        if(dx<0 || dz<0 || dx+16>World.Width || dz+16>World.Length)continue;
        for(y=0;y<256;y++)for(z=0;z<16;z++)for(x=0;x<16;x++) {
            b=LapisBlocks_State(c->blocks[y*256+z*16+x]);index=World_Pack(dx+x,y,dz+z);
            World.Blocks[index]=(BlockRaw)b;World.Blocks2[index]=(BlockRaw)(b>>8);
        }
    }
    if(ready) { Lighting.Refresh();MapRenderer_Refresh(); }
    mapDirty=0;
}
static void Daylight(void) {
    int light=LapisWorld_Daylight(&world);
    /* Quantize changes to avoid rebuilding terrain colours every network tick.
       This remains engine shading, not modern per-voxel light simulation. */
    Env_SetSkyCol(PackedCol_Make(13+7*light,20+10*light,38+12*light,255));
    Env_SetFogCol(PackedCol_Make(18+11*light,25+12*light,42+12*light,255));
    Env_SetSunCol(PackedCol_Make(72+11*light,76+11*light,90+10*light,255));
    Env_SetShadowCol(PackedCol_Make(42+7*light,46+7*light,59+6*light,255));
}
static void LoadWorld(struct LapisProtocol* p) {
    BlockRaw *lower,*upper;struct Screen* s;int volume=LAPIS_WORLD_SIDE*256*LAPIS_WORLD_SIDE;
    static const cc_string model=String_FromConst("lapis-person");
    lower=(BlockRaw*)Mem_TryAlloc(volume,1);upper=(BlockRaw*)Mem_TryAlloc(volume,1);
    if(!lower || !upper) { Mem_Free(lower);Mem_Free(upper);LapisProtocol_Fail(p,"Not enough memory for the bounded world window");return; }
    World_SetDimensions(LAPIS_WORLD_SIDE,256,LAPIS_WORLD_SIDE);World.Blocks=lower;World_SetMapUpper(upper);
    FillMap();
    Env.EdgeBlock=0;Env.SidesBlock=0;Env.EdgeHeight=0;Env.CloudsHeight=132;
    World_SetNewMap(lower,LAPIS_WORLD_SIDE,256,LAPIS_WORLD_SIDE);
    Daylight();
    ready=1;Position(p);Inventory.Offset=0;
    Entity_SetModel(&Entities.CurPlayer->Base,&model);
    s=Gui_GetScreen(GUI_PRIORITY_LOADING);if(s)Gui_Remove(s);
    LapisProtocol_MarkLoaded(p);
    Platform_LogConst("LapisCube native: world rendered and player loaded (112x256x112 window)");
}

static void Packet(struct LapisProtocol* p, int state, int id, const cc_uint8* data, int size, void* context) {
    struct LapisReader r;struct LapisSoundEvent sound;int chunks,x,y,z,value,result;
    (void)context;
    if (state != LAPIS_PLAY) return;
    LapisReader_Init(&r,data,size);
    if(id==0x6A) {
        if(!LapisWorld_Time(&world,data,size))goto malformed;
        if(ready)Daylight();
    } else if(id==0x57) {
        x=(cc_int32)LapisReader_VarInt(&r);z=(cc_int32)LapisReader_VarInt(&r);
        if(!LapisReader_Done(&r) || LapisWorld_Center(&world,x,z)<0)goto malformed;
        Recenter();
    } else if(id==0x27) {
        if(!LapisWorld_Chunk(&world,data,size))goto malformed;
        x=(cc_int32)LapisReader_Big(&r,4);z=(cc_int32)LapisReader_Big(&r,4);LapisSigns_Chunk(&signs,x,z);
        mapDirty=1;
    } else if(id==0x08) {
        LapisWorld_Position(LapisReader_Big(&r,8),&x,&y,&z);value=LapisReader_Count(&r,65535);
        if(!LapisReader_Done(&r) || !LapisWorld_Block(&world,x,y,z,value))goto malformed;
        LapisSigns_Block(&signs,x,y,z,value);
        if(ready && x>=originX && x<originX+World.Width && z>=originZ && z<originZ+World.Length)
            Game_UpdateBlock(x-originX,y,z-originZ,LapisBlocks_State(value));
    } else if(id==0x41) {
        if(!world.hasCenter) { LapisWorld_Center(&world,LapisWorld_ChunkCoord((int)p->x),LapisWorld_ChunkCoord((int)p->z));Recenter(); }
        if(ready)Position(p);
    }
    result=LapisGameplay_Packet(&gameplay,p,id,data,size);
    if(!result)goto malformed;
    if(!LapisEntities_Packet(&entities,id,data,size) || !LapisSigns_Packet(&signs,id,data,size))goto malformed;
    if(id==0x35)LapisGui_ShowSign();
    if(id==0x4B) {
        LapisGui_Close();LapisMobs_Clear();LapisEntities_Init(&entities);
        World_NewMap();LapisSigns_Init(&signs);LapisWorld_Init(&world);ready=0;digging=0;mapDirty=0;
    }
    if(id==0x6E) {
        if(!LapisEffects_Sound(&sound,data,size))goto malformed;
        LapisAudio_Play(&sound,Entities.CurPlayer->Base.Position.x+originX,Entities.CurPlayer->Base.Position.y,Entities.CurPlayer->Base.Position.z+originZ);
    }
    if(id==0x34)LapisGui_ShowInventory();
    if(id==0x72) {
        char buffer[2048];cc_string message=String_FromArray(buffer);
        String_AppendUtf8(&message,gameplay.message,(int)strlen(gameplay.message));Chat_Add(&message);
    }
    if(id==0x62) { Inventory.SelectedIndex=gameplay.selected;selected=gameplay.selected; }
    if (id == 0x2B) Platform_LogConst("LapisCube native: Play login accepted");
    if (id == 0x41 && p->teleports == 2) {
        chunks = (int)p->chunks;
        Platform_Log1("LapisCube native: spawn synchronized, received %i chunk packets", &chunks);
    }
    return;
malformed:
    LapisProtocol_Fail(p,"Malformed or unsupported Lapis world/gameplay packet");
}

static cc_uint64 Now(void) { return Stopwatch_ElapsedMicroseconds(0, Stopwatch_Measure()) / 1000; }
static int Read(void* context, cc_uint8* data, int size) {
    cc_result res; cc_uint32 n;
    (void)context;
    res = Socket_Read(connection, data, (cc_uint32)size, &n);
    if (res == ReturnCode_SocketWouldBlock || res == ReturnCode_SocketInProgess) return 0;
    if (res) return -2;
    return n ? (int)n : -1;
}
static int Write(void* context, const cc_uint8* data, int size) {
    cc_result res; cc_uint32 n;
    (void)context;
    res = Socket_Write(connection, data, (cc_uint32)size, &n);
    if (res == ReturnCode_SocketWouldBlock || res == ReturnCode_SocketInProgess) return 0;
    if (res || !n) return -2;
    return (int)n;
}
void LapisBackend_Close(void) {
    if (connection != -1) Socket_Close(connection);
    connection = -1; connecting = false;
    LapisGui_Close();LapisMobs_Clear();LapisAudio_Free();ready=0;digging=0;usingItem=0;
    LapisProtocol_Init(&session.protocol, NULL, NULL);
    Server.Disconnected = true;
}
static void Disconnect(const char* reason) {
    const cc_string title = String_FromConst("LapisCube connection ended");
    cc_string message = String_FromReadonly(reason);
    /* Game_Disconnect resets the connection; copy its reason before reset. */
    char buffer[160]; cc_string copy = String_FromArray(buffer);
    String_Copy(&copy, &message);
    Game_Disconnect(&title, &copy);
}
static void Progress(void) {
    static const cc_string title = String_FromConst("LapisCube");
    static const cc_string config = String_FromConst("Synchronising Lapis registry identifiers...");
    static const cc_string play = String_FromConst("Loading the server world...");
    struct LapisProtocol* p = &session.protocol;
    if ((unsigned)p->state == lastStage) return;
    lastStage = (unsigned)p->state;
    if (p->state == LAPIS_CONFIG) LoadingScreen_Show(&title, &config);
    if (p->state == LAPIS_PLAY) {
        if(!ready)LoadingScreen_Show(&title, &play);
    }
}
static void PlayerTick(void) {
    struct LapisProtocol* p=&session.protocol;struct LocalPlayer* player=Entities.CurPlayer;
    struct Entity* e=&player->Base;cc_uint8 data[16];int i,n,s;
    player->Hacks.CanAnyHacks=false;player->Hacks.CanSpeed=false;player->Hacks.CanRespawn=false;
    player->Hacks.CanFly=(gameplay.abilities&4)!=0;player->Hacks.CanNoclip=p->gamemode==3;
    if(!player->Hacks.CanFly)player->Hacks.Flying=false;
    player->Hacks.Noclip=p->gamemode==3;player->Hacks.MaxHorSpeed=1.0f;
    player->Physics.JumpVel=.42f;player->Physics.ServerJumpVel=.42f;player->ReachDistance=4.5f;
    s=!Gui.InputGrab && KeyBind_IsPressed(BIND_HALF_SPEED);
    if(s!=sneak) { sneak=s;data[0]=(cc_uint8)(s?0x20:0);LapisProtocol_Queue(p,0x2A,data,1); }
    s=!Gui.InputGrab && !sneak && KeyBind_IsPressed(BIND_SPEED) && gameplay.food>6;
    if(s!=sprint) { sprint=s;n=LapisProtocol_EncodeVarInt(data,(cc_uint32)p->entityId);data[n++]=(cc_uint8)(s?1:2);data[n++]=0;LapisProtocol_Queue(p,0x29,data,n); }
    player->Hacks.BaseHorSpeed=sneak?.3f:sprint?1.3f:1.0f;
    if(gameplay.health<=0) { Vec3_Set(e->Velocity,0,0,0); }
    else LapisGameplay_Move(p,e->next.pos.x+originX,e->next.pos.y,e->next.pos.z+originZ,JavaYaw(e->Yaw),JavaPitch(e->Pitch),e->OnGround);
    Inventory.Offset=0;
    for(i=0;i<9;i++)Inventory.Table[i]=LapisBlocks_Item(gameplay.slots[0][36+i].item);
    if(selected!=Inventory.SelectedIndex) {
        selected=Inventory.SelectedIndex;gameplay.selected=selected;LapisGameplay_Select(p,selected);
    }
    if(digging && (Gui.InputGrab || gameplay.health<=0 || p->gamemode>=2 ||
        selected!=digSlot || gameplay.slots[0][36+selected].item!=digItem ||
        LapisWorld_Get(&world,digX,digY,digZ)!=digState ||
        !KeyBind_IsPressed(BIND_DELETE_BLOCK) || !Game_SelectedPos.valid ||
        Game_SelectedPos.pos.x+originX!=digX || Game_SelectedPos.pos.y!=digY || Game_SelectedPos.pos.z+originZ!=digZ)) {
        LapisGameplay_Dig(&gameplay,p,1,digX,digY,digZ,digFace);digging=0;
    }
    if(digging && Now()-digStart>=(cc_uint64)digDuration) {
        LapisGameplay_Dig(&gameplay,p,2,digX,digY,digZ,digFace);digging=0;
    }
    if(usingItem && (Gui.InputGrab || !KeyBind_IsPressed(BIND_PLACE_BLOCK))) {
        LapisGameplay_Dig(&gameplay,p,5,0,0,0,0);usingItem=0;
    }
}
static cc_bool Tick(struct ScheduledTask2* task) {
    struct LapisIO io; cc_bool writable; cc_result res;
    (void)task;
    if (Server.Disconnected) return true;
    if (connecting) {
        res = Socket_Poll(connection, 0, SOCKET_POLL_WRITE, &writable);
        if (res) { Disconnect("TCP connection failed"); return true; }
        if (!writable) {
            if (Now() - session.started > 15000) Disconnect("TCP connection timed out");
            return true;
        }
        /* Initial queued handshake write also detects a refused connection. */
        connecting = false;
    }
    io.read = Read; io.write = Write; io.context = NULL;
    if (!LapisSession_Pump(&session, &io, Now())) {
        Disconnect(session.protocol.error); return true;
    }
    Progress();
    if(!ready && session.protocol.teleports>=2 && world.decoded)LoadWorld(&session.protocol);
    if(ready && mapDirty)FillMap();
    if(ready) { PlayerTick();LapisMobs_Update(&entities,originX,originZ); }
    return true;
}
static void Begin(void) {
    static const cc_string title = String_FromConst("Connecting to Lapis Obsidian...");
    cc_sockaddr addresses[SOCKET_MAX_ADDRS]; int count;
    cc_result res; cc_uint8 uuid[16]; char host[256], username[16];
    LapisBackend_Close();
    World_NewMap();LapisSigns_Init(&signs);LapisWorld_Init(&world);LapisGameplay_Init(&gameplay);LapisBlocks_Init();
    LapisEntities_Init(&entities);LapisMobs_Init();TexturePack_ExtractCurrent(true);
    Blocks.Draw[255]=DRAW_GAS;Blocks.Collide[255]=COLLIDE_SOLID;Blocks.BlocksLight[255]=false;Block_DefineCustom(255,false);
    originX=originZ=0;mapDirty=0;selected=0;sneak=sprint=0;lastUse=0;
    if (Server.Address.length > 255 || Game_Username.length > 15 || !Game_Username.length) {
        Disconnect("Use a host of at most 255 bytes and a 1-15 character offline username"); return;
    }
    memcpy(host, Server.Address.buffer, Server.Address.length); host[Server.Address.length] = 0;
    memcpy(username, Game_Username.buffer, Game_Username.length); username[Game_Username.length] = 0;
    LapisSession_Init(&session, Now(), Packet, NULL);
    LapisIdentity_OfflineUUID(username, uuid);
    if (!LapisProtocol_Begin(&session.protocol, host, Server.Port, username, uuid, 0)) {
        Disconnect(session.protocol.error); return;
    }
    res = Socket_ParseAddress(&Server.Address, Server.Port, addresses, &count);
    if (res || !count) { Disconnect("Could not resolve Lapis address"); return; }
    res = Socket_Create(&connection, &addresses[0]);
    if (res) { Disconnect("Could not create TCP socket"); return; }
    res = Socket_SetNonBlocking(connection, true);
    if (res) { Disconnect("Could not enable nonblocking TCP"); return; }
    res = Socket_Connect(connection, addresses[0].data, addresses[0].size);
    if (res && res != ReturnCode_SocketInProgess && res != ReturnCode_SocketWouldBlock) {
        Disconnect("Could not connect to Lapis server"); return;
    }
    Server.Disconnected = false; connecting = true; lastStage = 0;
    LoadingScreen_Show(&title, &String_Empty);
}
/* Legacy optimistic block edits cannot become Lapis actions. Input hooks send
 * sequenced requests; only server block-update packets change the world. */
static void SendBlock(int x, int y, int z, BlockID old, BlockID now) {
    (void)x; (void)y; (void)z; (void)old; (void)now;
    Platform_LogConst("LapisCube: rejected legacy optimistic block edit");
}
static void SendChat(const cc_string* text) {
    cc_uint8 utf8[STRING_SIZE*3];int n;
    if(text->length>STRING_SIZE)return;
    n=String_EncodeUtf8(utf8,text);LapisGameplay_Chat(&session.protocol,(const char*)utf8,n);
}
static int TargetFace(void) {
    static const int map[6]={4,5,2,3,0,1};return Game_SelectedPos.closest<6?map[Game_SelectedPos.closest]:1;
}
struct LapisSign* LapisBackend_TargetSign(void) {
    if(!ready || !Game_SelectedPos.valid)return NULL;
    return LapisSigns_Find(&signs,Game_SelectedPos.pos.x+originX,Game_SelectedPos.pos.y,Game_SelectedPos.pos.z+originZ);
}
static float Cursor(float value) { return value<0?0:value>1?1:value; }
void LapisBackend_Dig(void) {
    int target;char block[STRING_SIZE];cc_string name,text=String_Init(block,0,STRING_SIZE-1);
    if(!ready || gameplay.health<=0)return;
    target=LapisMobs_Target(&entities);
    if(target>=0) { LapisGameplay_Attack(&session.protocol,entities.list[target].id);return; }
    if(!Game_SelectedPos.valid || digging)return;
    digX=Game_SelectedPos.pos.x+originX;digY=Game_SelectedPos.pos.y;digZ=Game_SelectedPos.pos.z+originZ;digFace=TargetFace();
    digState=LapisWorld_Get(&world,digX,digY,digZ);if(digState<0)return;
    name=Block_UNSAFE_GetName(LapisBlocks_State(digState));String_Copy(&text,&name);block[text.length]=0;
    digSlot=Inventory.SelectedIndex;digItem=gameplay.slots[0][36+digSlot].item;
    digDuration=LapisMining_Delay(block,LapisBlocks_ItemName(digItem),session.protocol.gamemode);
    if(digDuration<0)return;
    /* Selection must precede the action even between network ticks. */
    if(selected!=digSlot) { selected=digSlot;gameplay.selected=selected;LapisGameplay_Select(&session.protocol,selected); }
    if(!LapisGameplay_Dig(&gameplay,&session.protocol,0,digX,digY,digZ,digFace))return;
    digging=digDuration>0;digStart=Now();
}
float LapisBackend_DigProgress(void) {
    float value;if(!digging || digDuration<=0)return -1;
    value=(float)(Now()-digStart)/(float)digDuration;return value>1?1:value;
}
void LapisBackend_Use(void) {
    struct Entity* e=&Entities.CurPlayer->Base;
    /* Repeating Use every input tick restarts Lapis's eating timer. Hold one
       request until release; a second click starts the next interaction. */
    if(!ready || gameplay.health<=0 || usingItem || Now()-lastUse<250)return;
    lastUse=Now();
    usingItem=1;
    if(Game_SelectedPos.valid)
        LapisGameplay_UseOn(&gameplay,&session.protocol,Game_SelectedPos.pos.x+originX,Game_SelectedPos.pos.y,Game_SelectedPos.pos.z+originZ,TargetFace(),
            Cursor(Game_SelectedPos.intersect.x-Game_SelectedPos.pos.x),Cursor(Game_SelectedPos.intersect.y-Game_SelectedPos.pos.y),Cursor(Game_SelectedPos.intersect.z-Game_SelectedPos.pos.z));
    else LapisGameplay_Use(&gameplay,&session.protocol,JavaYaw(e->Yaw),JavaPitch(e->Pitch));
}
void LapisBackend_Drop(void) { if(ready)LapisGameplay_Dig(&gameplay,&session.protocol,4,0,0,0,0); }
static void SendData(const cc_uint8* data, cc_uint32 size) {
    (void)data; (void)size;
    Platform_LogConst("LapisCube: rejected legacy raw packet send");
}
void LapisBackend_Init(void) {
    int i;
    Server.BeginConnect = Begin; Server.Tick = Tick;
    Server.SendBlock = SendBlock; Server.SendChat = SendChat; Server.SendData = SendData;
    Server.IsSinglePlayer = false; Server.Disconnected = false;
    for (i = 0; i < BLOCK_COUNT; i++) { Blocks.CanPlace[i] = false; Blocks.CanDelete[i] = false; }
}
#else
struct LapisSigns* LapisBackend_Signs(void) { return NULL; }
struct LapisSign* LapisBackend_TargetSign(void) { return NULL; }
void LapisBackend_Close(void) { }
void LapisBackend_Init(void) { Server.Disconnected = true; }
struct LapisGameplay* LapisBackend_Gameplay(void) { return NULL; }
struct LapisProtocol* LapisBackend_Protocol(void) { return NULL; }
void LapisBackend_Dig(void) { }
float LapisBackend_DigProgress(void) { return -1; }
void LapisBackend_Use(void) { }
void LapisBackend_Drop(void) { }
#endif
