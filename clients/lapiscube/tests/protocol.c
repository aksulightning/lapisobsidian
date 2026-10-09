#include "LapisSession.h"
#include "LapisIdentity.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#ifdef LAPIS_SANDBOX_SANITIZERS
/* Restricted test runners may not expose /proc. No production code is affected. */
const char* __asan_default_options(void) { return "detect_leaks=0"; }
const char* __ubsan_default_options(void) { return "halt_on_error=1"; }
#endif

static struct LapisProtocol p;
static struct LapisSession session;
static cc_uint8 packet[LAPIS_MAX_PACKET + 8], body[LAPIS_MAX_PACKET];
static unsigned checks;
#define CHECK(x) do { checks++; if (!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); assert(x); } } while (0)

static int Str(cc_uint8* data, const char* text) {
    int n = (int)strlen(text), prefix = LapisProtocol_EncodeVarInt(data, (cc_uint32)n);
    memcpy(data + prefix, text, (size_t)n); return n + prefix;
}
static int Frame(int id, const cc_uint8* data, int size) {
    int n = LapisProtocol_EncodeVarInt(packet, (cc_uint32)(size + 1));
    packet[n++] = (cc_uint8)id;
    if (size) memcpy(packet + n, data, (size_t)size);
    return n + size;
}
static int Feed(int id, const cc_uint8* data, int size) {
    int n = Frame(id, data, size);
    return LapisProtocol_Feed(&p, packet, n);
}
static void Drain(void) { LapisProtocol_ConsumeOutput(&p, p.outputSize); }
static void Start(int status) {
    cc_uint8 uuid[16];
    LapisIdentity_OfflineUUID("LapisCubeTest", uuid);
    LapisProtocol_Init(&p, NULL, NULL);
    CHECK(LapisProtocol_Begin(&p, "127.0.0.1", 25565, "LapisCubeTest", uuid, status));
}
static void Login(void) {
    int n;
    Start(0); Drain();
    memcpy(body, p.uuid, 16); n = 16 + Str(body + 16, p.username); body[n++] = 0;
    CHECK(Feed(2, body, n)); CHECK(p.state == LAPIS_CONFIG); Drain();
}
static void Known(void) {
    int n = 1;
    body[0] = 1; n += Str(body + n, "minecraft"); n += Str(body + n, "core"); n += Str(body + n, "1.21.8");
    CHECK(Feed(0x0E, body, n)); CHECK(p.knownPacks); Drain();
}
static int Registry(const char* name, const char* entry) {
    int n = Str(body, name); body[n++] = 1;
    n += Str(body + n, entry); body[n++] = 0;
    return Feed(7, body, n);
}
static void Configuration(void) {
    static const char* const names[] = {"cat_variant", "chicken_variant", "cow_variant", "frog_variant",
        "painting_variant", "pig_variant", "wolf_sound_variant", "wolf_variant", "damage_type", "worldgen/biome"};
    int i;
    Login(); Known();
    for (i = 0; i < 10; i++) CHECK(Registry(names[i], i == 9 ? "plains" : "test"));
    CHECK(Registry("minecraft:dimension_type", "minecraft:overworld"));
    body[0] = 0; CHECK(Feed(0x0D, body, 1)); CHECK(p.tagsReceived);
    CHECK(Feed(3, NULL, 0)); CHECK(p.state == LAPIS_PLAY); Drain();
}
static void Join(void) {
    int n;
    Configuration();
    memset(body, 0, sizeof(body)); body[3] = 42; body[5] = 1;
    n = 6 + Str(body + 6, "minecraft:overworld");
    body[n++] = 10; body[n++] = 2; body[n++] = 2;
    body[n++] = 0; body[n++] = 1; body[n++] = 0; body[n++] = 0;
    n += Str(body + n, "minecraft:overworld");
    n += 8; body[n++] = 0; body[n++] = 255; n += 3;
    body[n++] = 0; body[n++] = 63; body[n++] = 0;
    CHECK(Feed(0x2B, body, n)); CHECK(p.joined && p.entityId == 42); Drain();
}
static void VarInts(void) {
    static const cc_uint32 values[] = {0, 1, 127, 128, 255, 772, 16383, 16384, 2147483647u, 2147483648u, 4294967295u};
    struct LapisReader r; cc_uint8 encoded[6]; int i, n;
    for (i = 0; i < (int)(sizeof(values) / sizeof(values[0])); i++) {
        n = LapisProtocol_EncodeVarInt(encoded, values[i]);
        LapisReader_Init(&r, encoded, n); CHECK(LapisReader_VarInt(&r) == values[i]); CHECK(!r.failed && r.pos == n);
        LapisReader_Init(&r, encoded, n - 1); LapisReader_VarInt(&r); CHECK(r.failed);
    }
    memset(encoded, 128, sizeof(encoded));
    LapisReader_Init(&r, encoded, 6); LapisReader_VarInt(&r); CHECK(r.failed);
    encoded[4] = 16; LapisReader_Init(&r, encoded, 5); LapisReader_VarInt(&r); CHECK(r.failed);
}
static void Identity(void) {
    static const cc_uint8 expected[16] = {0x56,0x27,0xdd,0x98,0xe6,0xbe,0x3c,0x21,0xb8,0xa8,0xe9,0x23,0x44,0x18,0x36,0x41};
    cc_uint8 uuid[16];
    LapisIdentity_OfflineUUID("Steve", uuid); CHECK(!memcmp(uuid, expected, 16));
}
static void Framing(void) {
    static const cc_uint8 ping[] = {9, 1, 0x4C,0x61,0x70,0x69,0x73,0x43,0x75,0x62};
    int n, i, split, length;
    n = Str(body, "{\"protocol\":772}"); length = Frame(0, body, n);
    /* Every boundary, including empty first/last fragments. */
    for (split = 0; split <= length; split++) {
        Start(1); Drain();
        CHECK(LapisProtocol_Feed(&p, packet, split));
        CHECK(LapisProtocol_Feed(&p, packet + split, length - split));
        CHECK(!strcmp(p.status, "{\"protocol\":772}")); CHECK(p.outputSize == 10);
    }
    Start(1); Drain();
    for (i = 0; i < length; i++) CHECK(LapisProtocol_Feed(&p, packet + i, 1));
    CHECK(p.packets == 1);
    Start(1); Drain(); memcpy(packet + length, ping, sizeof(ping));
    CHECK(LapisProtocol_Feed(&p, packet, length + (int)sizeof(ping))); CHECK(p.statusPong);
    CHECK(LapisProtocol_EndInput(&p));
    Start(1); packet[0] = 0; CHECK(!LapisProtocol_Feed(&p, packet, 1));
    Start(1); packet[0] = 128; packet[1] = 128; packet[2] = 128;
    CHECK(!LapisProtocol_Feed(&p, packet, 3));
    Start(1); n = LapisProtocol_EncodeVarInt(packet, LAPIS_MAX_PACKET + 1);
    CHECK(!LapisProtocol_Feed(&p, packet, n));
    Start(1); packet[0] = 128; CHECK(LapisProtocol_Feed(&p, packet, 1)); CHECK(!LapisProtocol_EndInput(&p));
    Start(1); packet[0] = 10; packet[1] = 0;
    CHECK(LapisProtocol_Feed(&p, packet, 2)); CHECK(!LapisProtocol_EndInput(&p));
    Start(1); packet[0] = 6; memset(packet + 1, 255, 5); packet[6] = 1;
    CHECK(!LapisProtocol_Feed(&p, packet, 7));
    Start(1); body[0] = 255; body[1] = 255; body[2] = 255; body[3] = 255; body[4] = 15;
    CHECK(!Feed(0, body, 5));
    Start(1); CHECK(!LapisProtocol_Feed(&p, NULL, -1)); CHECK(!LapisProtocol_Feed(&p, packet, 1));
}
static void States(void) {
    int n; cc_uint8 uuid[16] = {0};
    LapisProtocol_Init(&p, NULL, NULL);
    CHECK(!LapisProtocol_Begin(&p, "localhost", 0, "x", uuid, 0));
    LapisProtocol_Init(&p, NULL, NULL);
    CHECK(!LapisProtocol_Begin(&p, "localhost", 25565, "sixteencharsxxxx", uuid, 0));
    LapisProtocol_Init(&p, NULL, NULL);
    CHECK(!LapisProtocol_Begin(&p, "localhost", 25565, "bad name", uuid, 0));
    Start(0); CHECK(!Feed(3, NULL, 0)); CHECK(strstr(p.error, "Compression") != NULL);
    Start(0); CHECK(!Feed(1, NULL, 0));
    Login(); CHECK(!Feed(3, NULL, 0)); /* no known packs/registries */
    Login(); body[0] = 0; CHECK(!Feed(0x0E, body, 1));
    Login(); Known(); CHECK(!Feed(0x0E, NULL, 0));
    Login(); Known(); CHECK(Registry("damage_type", "fall")); CHECK(!Registry("minecraft:damage_type", "fall"));
    Login(); Known(); n = Str(body, "dimension_type"); body[n++] = 1; n += Str(body + n, "overworld"); body[n++] = 1;
    CHECK(!Feed(7, body, n)); /* unsupported NBT must not be silently ignored */
    Login(); Known(); n = Str(body, "damage_type"); body[n++] = 2;
    n += Str(body + n, "fall"); body[n++] = 0; n += Str(body + n, "fall"); body[n++] = 0;
    CHECK(!Feed(7, body, n));
    Configuration(); CHECK(p.registryCount == 11);
    CHECK(!strcmp(LapisProtocol_RegistryEntry(&p, "dimension_type", 0), "minecraft:overworld"));
    CHECK(!LapisProtocol_RegistryEntry(&p, "dimension_type", 1));
    CHECK(!LapisProtocol_RegistryEntry(&p, "missing", 0));
    CHECK(!Feed(0x26, uuid, 8)); /* Play keepalive before Join */
}
static void Play(void) {
    int n; cc_uint8 keepalive[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    Join(); CHECK(Feed(0x26, keepalive, 8)); CHECK(p.keepalives == 1);
    CHECK(p.outputSize == 10 && p.output[1] == 0x1B && !memcmp(p.output + 2, keepalive, 8)); Drain();
    n = LapisProtocol_EncodeVarInt(body, 4294967295u); memset(body + n, 0, 60);
    /* x=8.5, y=80, z=8.5 big-endian IEEE754, all velocity/angles/flags zero */
    body[n] = 0x40; body[n+1] = 0x21;
    body[n+8] = 0x40; body[n+9] = 0x54;
    body[n+16] = 0x40; body[n+17] = 0x21;
    CHECK(Feed(0x41, body, n + 60)); CHECK(p.x == 8.5 && p.y == 80 && p.z == 8.5);
    CHECK(p.outputSize == 7 && p.output[1] == 0 && p.output[6] == 15); Drain();
    CHECK(!LapisProtocol_MarkLoaded(&p));
    CHECK(Feed(0x27, keepalive, 8)); CHECK(p.chunks == 1);
    CHECK(LapisProtocol_MarkLoaded(&p)); CHECK(!LapisProtocol_MarkLoaded(&p)); Drain();
    /* largest admitted frame, unknown play packet: bounded skip */
    memset(body, 0, sizeof(body)); n = Frame(0x7F, body, LAPIS_MAX_PACKET - 1);
    CHECK(LapisProtocol_Feed(&p, packet, n));
    CHECK(!Feed(0x26, keepalive, 7));
    Join(); n = LapisProtocol_EncodeVarInt(body, 1); memset(body + n, 0, 60);
    body[n] = 0x7F; body[n+1] = 0xF8; CHECK(!Feed(0x41, body, n + 60)); /* NaN */
    Join(); n = LapisProtocol_EncodeVarInt(body, 1); memset(body + n, 0, 60); body[n+59] = 1;
    CHECK(!Feed(0x41, body, n + 60)); /* unsupported relative flags */
    Join(); CHECK(!Feed(0x41, keepalive, 8));
    Start(0); CHECK(p.registryCount == 0 && p.teleports == 0 && !p.joined); /* reconnect reset */
}

static cc_uint8 wire[65536], incoming[32];
static int wireUsed, inputUsed, inputSize, blocked;
static int FakeRead(void* ctx, cc_uint8* data, int size) {
    (void)ctx; (void)size;
    if (inputUsed == inputSize) return 0;
    *data = incoming[inputUsed++]; return 1;
}
static int FakeWrite(void* ctx, const cc_uint8* data, int size) {
    int n = size > 3 ? 3 : size; (void)ctx;
    if (blocked) return 0;
    CHECK(wireUsed + n <= (int)sizeof(wire)); memcpy(wire + wireUsed, data, (size_t)n); wireUsed += n;
    return n;
}
static void Sessions(void) {
    struct LapisIO io; cc_uint8 expected[512]; int n;
    io.read = FakeRead; io.write = FakeWrite; io.context = NULL;
    wireUsed = inputUsed = inputSize = 0; blocked = 1;
    LapisSession_Init(&session, 1, NULL, NULL);
    CHECK(LapisProtocol_Begin(&session.protocol, "localhost", 25565, NULL, NULL, 1));
    n = session.protocol.outputSize; memcpy(expected, session.protocol.output, (size_t)n);
    CHECK(LapisSession_Pump(&session, &io, 2)); CHECK(!wireUsed && session.protocol.outputSize == n);
    blocked = 0; CHECK(LapisSession_Pump(&session, &io, 3));
    CHECK(wireUsed == n && !memcmp(wire, expected, (size_t)n) && !session.protocol.outputSize);
    incoming[0] = 127; inputSize = 1;
    CHECK(LapisSession_Pump(&session, &io, 4));
    /* Isolate frame deadline from the login deadline. */
    session.protocol.joined = 1; session.lastRead = 16000;
    CHECK(!LapisSession_Pump(&session, &io, 16005)); CHECK(strstr(session.protocol.error, "Incomplete") != NULL);
    inputSize = inputUsed = 0;
    LapisSession_Init(&session, 1, NULL, NULL);
    CHECK(LapisProtocol_Begin(&session.protocol, "localhost", 25565, NULL, NULL, 1));
    CHECK(!LapisSession_Pump(&session, &io, 15002)); CHECK(strstr(session.protocol.error, "timed out") != NULL);
    Join();
    /* Peer floods requests without consuming responses: fail safely at queue cap. */
    memset(body, 0, 8);
    while (p.state != LAPIS_FAILED) Feed(0x26, body, 8);
    CHECK(strstr(p.error, "queue full") != NULL); CHECK(!p.outputSize);
    Start(1); LapisProtocol_ConsumeOutput(&p, p.outputSize + 1); CHECK(p.state == LAPIS_FAILED);
}

static void FuzzFraming(void) {
    cc_uint32 random = 0x12345678u; cc_uint8 data[64]; int i, j;
    /* Deterministic adversarial bytes under ASan/UBSan; no network required. */
    for (i = 0; i < 2000; i++) {
        LapisProtocol_Init(&p, NULL, NULL); p.state = LAPIS_LOGIN;
        for (j = 0; j < (int)sizeof(data); j++) {
            random ^= random << 13; random ^= random >> 17; random ^= random << 5;
            data[j] = (cc_uint8)random;
        }
        LapisProtocol_Feed(&p, data, sizeof(data));
        LapisProtocol_EndInput(&p);
        CHECK(p.frameUsed <= LAPIS_MAX_PACKET && p.outputSize <= LAPIS_MAX_OUTPUT);
    }
}
int main(void) {
    VarInts(); Identity(); Framing(); States(); Play(); Sessions(); FuzzFraming();
    printf("protocol: %u assertions passed (framing, identity, states, registries, sync, backpressure, deadlines, fuzz)\n", checks);
    return 0;
}
