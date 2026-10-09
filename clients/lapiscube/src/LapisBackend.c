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
#include <string.h>

cc_bool LapisBackend_Enabled;
#ifdef CC_BUILD_NETWORKING
static struct LapisSession session;
static cc_socket connection = -1;
static cc_bool connecting;
static unsigned lastStage;

static void Packet(struct LapisProtocol* p, int state, int id, const cc_uint8* data, int size, void* context) {
    int chunks;
    (void)data; (void)size; (void)context;
    if (state != LAPIS_PLAY) return;
    if (id == 0x2B) Platform_LogConst("LapisCube native: Play login accepted");
    if (id == 0x41 && p->teleports == 2) {
        chunks = (int)p->chunks;
        Platform_Log1("LapisCube native: spawn synchronized, received %i chunk packets", &chunks);
    }
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
    static const cc_string title = String_FromConst("LapisCube - protocol foundation");
    static const cc_string config = String_FromConst("Synchronising Lapis registry identifiers...");
    static const cc_string play = String_FromConst("Connected. World rendering is the next milestone.");
    struct LapisProtocol* p = &session.protocol;
    if ((unsigned)p->state == lastStage) return;
    lastStage = (unsigned)p->state;
    if (p->state == LAPIS_CONFIG) LoadingScreen_Show(&title, &config);
    if (p->state == LAPIS_PLAY) {
        LoadingScreen_Show(&title, &play);
        Platform_LogConst("LapisCube: configuration complete, entered Play (world rendering pending)");
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
    Progress(); return true;
}
static void Begin(void) {
    static const cc_string title = String_FromConst("Connecting to Lapis Obsidian...");
    cc_sockaddr addresses[SOCKET_MAX_ADDRS]; int count;
    cc_result res; cc_uint8 uuid[16]; char host[256], username[16];
    LapisBackend_Close();
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
/* Gameplay actions are unavailable until their server-authoritative modules exist.
 * No Classic packet bytes are allowed onto the Lapis socket. */
static void SendBlock(int x, int y, int z, BlockID old, BlockID now) {
    (void)x; (void)y; (void)z; (void)old; (void)now;
    Chat_AddRaw("&eLapis block interaction is not implemented in milestone 1.");
}
static void SendChat(const cc_string* text) {
    (void)text; Chat_AddRaw("&eLapis chat is not implemented in milestone 1.");
}
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
void LapisBackend_Close(void) { }
void LapisBackend_Init(void) { Server.Disconnected = true; }
#endif
