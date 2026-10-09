#include "LapisProtocol.h"
#include <string.h>
#define Byte LapisReader_Byte
#define Count LapisReader_Count
#define Skip LapisReader_Skip
#define Big LapisReader_Big
#define Double LapisReader_Double
#define Float LapisReader_Float
#define Done LapisReader_Done
#define Queue LapisProtocol_Queue

int Byte(struct LapisReader* r) {
    if (r->pos >= r->size) { r->failed = 1; return 0; }
    return r->data[r->pos++];
}

void LapisReader_Init(struct LapisReader* r, const cc_uint8* data, int size) {
    r->data = data; r->size = size; r->pos = 0; r->failed = size < 0;
}

cc_uint32 LapisReader_VarInt(struct LapisReader* r) {
    cc_uint32 value = 0;
    int i, b;
    for (i = 0; i < 5; i++) {
        b = Byte(r);
        if (r->failed || (i == 4 && (b & 0xF0))) { r->failed = 1; return 0; }
        value |= (cc_uint32)(b & 127) << (i * 7);
        if (!(b & 128)) return value;
    }
    r->failed = 1; return 0;
}

int Count(struct LapisReader* r, int max) {
    cc_uint32 n = LapisReader_VarInt(r);
    if (n > (cc_uint32)max) { r->failed = 1; return 0; }
    return (int)n;
}

int Skip(struct LapisReader* r, int count) {
    if (count < 0 || count > r->size - r->pos) { r->failed = 1; return 0; }
    r->pos += count; return 1;
}

int LapisReader_String(struct LapisReader* r, char* dst, int capacity) {
    int n;
    if (capacity <= 0) { r->failed = 1; return 0; }
    dst[0] = 0;
    n = Count(r, capacity - 1);
    if (r->failed || n > r->size - r->pos) { r->failed = 1; return 0; }
    if (memchr(r->data + r->pos, 0, (size_t)n)) { r->failed = 1; return 0; }
    memcpy(dst, r->data + r->pos, (size_t)n); dst[n] = 0;
    r->pos += n; return n;
}

cc_uint64 Big(struct LapisReader* r, int bytes) {
    cc_uint64 v = 0;
    while (bytes--) v = (v << 8) | (cc_uint64)Byte(r);
    return v;
}

double Double(struct LapisReader* r) {
    cc_uint64 bits = Big(r, 8); double v;
    memcpy(&v, &bits, 8);
    if (!(v >= -1.0e12 && v <= 1.0e12)) r->failed = 1;
    return v;
}

float Float(struct LapisReader* r) {
    cc_uint32 bits = (cc_uint32)Big(r, 4); float v;
    memcpy(&v, &bits, 4);
    if (!(v >= -1.0e12f && v <= 1.0e12f)) r->failed = 1;
    return v;
}

int Done(const struct LapisReader* r) { return !r->failed && r->pos == r->size; }

int LapisProtocol_EncodeVarInt(cc_uint8* dst, cc_uint32 value) {
    int n = 0;
    do {
        dst[n] = (cc_uint8)(value & 127); value >>= 7;
        if (value) dst[n] |= 128;
        n++;
    } while (value);
    return n;
}

int LapisProtocol_Fail(struct LapisProtocol* p, const char* reason) {
    if (p->state != LAPIS_FAILED) {
        strncpy(p->error, reason, sizeof(p->error) - 1);
        p->error[sizeof(p->error) - 1] = 0;
    }
    p->state = LAPIS_FAILED;
    p->outputSize = 0;
    return 0;
}

int Queue(struct LapisProtocol* p, int id, const cc_uint8* data, int size) {
    cc_uint8 header[10]; int n, m;
    if (p->state == LAPIS_FAILED || size < 0 || size > LAPIS_MAX_OUTPUT - 10) return 0;
    m = LapisProtocol_EncodeVarInt(header + 5, (cc_uint32)id);
    n = LapisProtocol_EncodeVarInt(header, (cc_uint32)(m + size));
    if (p->outputSize > LAPIS_MAX_OUTPUT - n - m - size)
        return LapisProtocol_Fail(p, "Outbound queue full (peer is not reading)");
    memcpy(p->output + p->outputSize, header, (size_t)n); p->outputSize += n;
    memcpy(p->output + p->outputSize, header + 5, (size_t)m); p->outputSize += m;
    if (size) memcpy(p->output + p->outputSize, data, (size_t)size);
    p->outputSize += size; return 1;
}

void LapisProtocol_ConsumeOutput(struct LapisProtocol* p, int size) {
    if (size < 0 || size > p->outputSize) { LapisProtocol_Fail(p, "Invalid output consumption"); return; }
    p->outputSize -= size;
    memmove(p->output, p->output + size, (size_t)p->outputSize);
}

void LapisProtocol_Init(struct LapisProtocol* p, LapisPacketHandler handler, void* context) {
    memset(p, 0, sizeof(*p));
    p->handler = handler; p->context = context;
}

static int PutString(cc_uint8* dst, const char* str) {
    int n = (int)strlen(str), prefix = LapisProtocol_EncodeVarInt(dst, (cc_uint32)n);
    memcpy(dst + prefix, str, (size_t)n); return prefix + n;
}

int LapisProtocol_Begin(struct LapisProtocol* p, const char* host, int port,
                        const char* username, const cc_uint8 uuid[16], int status) {
    cc_uint8 data[512]; int n, i;
    if (p->state != LAPIS_OFFLINE) return LapisProtocol_Fail(p, "Connection already started");
    if (!host || !host[0] || strlen(host) > 255 || port < 1 || port > 65535)
        return LapisProtocol_Fail(p, "Invalid host or port");
    if (!status) {
        if (!username || !uuid || !username[0] || strlen(username) > 15)
            return LapisProtocol_Fail(p, "Lapis usernames must be 1-15 ASCII letters, digits or underscores");
        for (i = 0; username[i]; i++) {
            char c = username[i];
            if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                  (c >= '0' && c <= '9') || c == '_'))
                return LapisProtocol_Fail(p, "Invalid offline username");
        }
        strcpy(p->username, username); memcpy(p->uuid, uuid, 16);
    }
    p->state = status ? LAPIS_STATUS : LAPIS_LOGIN;
    n = LapisProtocol_EncodeVarInt(data, LAPIS_PROTOCOL_VERSION);
    n += PutString(data + n, host);
    data[n++] = (cc_uint8)(port >> 8); data[n++] = (cc_uint8)port;
    data[n++] = (cc_uint8)(status ? 1 : 2);
    if (!Queue(p, 0, data, n)) return 0;
    if (status) return Queue(p, 0, NULL, 0);
    n = PutString(data, username); memcpy(data + n, uuid, 16);
    return Queue(p, 0, data, n + 16);
}

static int SameKey(const char* a, const char* b) {
    if (!strncmp(a, "minecraft:", 10)) a += 10;
    if (!strncmp(b, "minecraft:", 10)) b += 10;
    return !strcmp(a, b);
}

const char* LapisProtocol_RegistryEntry(const struct LapisProtocol* p, const char* registry, int id) {
    int i;
    for (i = 0; i < p->registryCount; i++) {
        const struct LapisRegistry* reg = &p->registries[i];
        if (SameKey(p->names + reg->name, registry)) {
            if (id < 0 || id >= reg->count) return NULL;
            return p->names + p->entries[reg->first + id];
        }
    }
    return NULL;
}

static int Name(struct LapisProtocol* p, struct LapisReader* r) {
    char key[256]; int n, i, offset = p->namesUsed;
    n = LapisReader_String(r, key, sizeof(key));
    if (!n || r->failed || n + 1 > LAPIS_NAMES_SIZE - offset) { r->failed = 1; return 0; }
    for (i = 0; i < n; i++) {
        char c = key[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
              c == '_' || c == '-' || c == ':' || c == '/' || c == '.')) r->failed = 1;
    }
    memcpy(p->names + offset, key, (size_t)n + 1); p->namesUsed += n + 1;
    return offset;
}

static int Registry(struct LapisProtocol* p, struct LapisReader* r) {
    struct LapisRegistry* reg;
    int i, j, key, count;
    if (p->registryCount == LAPIS_MAX_REGISTRIES) return 0;
    key = Name(p, r); count = Count(r, LAPIS_MAX_ENTRIES - p->entryCount);
    for (i = 0; i < p->registryCount; i++)
        if (SameKey(p->names + key, p->names + p->registries[i].name)) return 0;
    reg = &p->registries[p->registryCount];
    reg->name = key; reg->first = p->entryCount; reg->count = count;
    for (i = 0; i < count && !r->failed; i++) {
        key = Name(p, r);
        for (j = reg->first; j < p->entryCount; j++)
            if (SameKey(p->names + key, p->names + p->entries[j])) return 0;
        p->entries[p->entryCount++] = key;
        /* Authoritative Lapis snapshot only sends identifiers, never NBT. */
        if (Byte(r) != 0) return 0;
    }
    if (!Done(r)) return 0;
    p->registryCount++; return 1;
}

static int Tags(struct LapisReader* r) {
    char name[256]; int i, j, k, registries, tags, ids;
    registries = Count(r, 32);
    for (i = 0; i < registries && !r->failed; i++) {
        LapisReader_String(r, name, sizeof(name)); tags = Count(r, 4096);
        for (j = 0; j < tags && !r->failed; j++) {
            LapisReader_String(r, name, sizeof(name)); ids = Count(r, 65536);
            for (k = 0; k < ids && !r->failed; k++) Count(r, 1000000);
        }
    }
    return Done(r);
}

static int RequiredRegistries(struct LapisProtocol* p) {
    static const char* const required[] = { "cat_variant", "chicken_variant", "cow_variant",
        "frog_variant", "painting_variant", "pig_variant", "wolf_sound_variant", "wolf_variant",
        "damage_type", "worldgen/biome", "dimension_type" };
    int i;
    const char* dim;
    for (i = 0; i < (int)(sizeof(required) / sizeof(required[0])); i++)
        if (!LapisProtocol_RegistryEntry(p, required[i], 0)) return 0;
    dim = LapisProtocol_RegistryEntry(p, "dimension_type", 0);
    return dim && SameKey(dim, "overworld") && p->knownPacks && p->tagsReceived;
}

static int Login(struct LapisProtocol* p, int id, struct LapisReader* r) {
    cc_uint8 data[64]; char username[64]; int n;
    if (id == 1) return LapisProtocol_Fail(p, "Online-mode encryption is outside the Lapis offline profile");
    if (id == 3) return LapisProtocol_Fail(p, "Compression is not sent by the supported Lapis revision");
    if (id == 0) return LapisProtocol_Fail(p, "Server rejected login (disconnect packet)");
    if (id != 2 || r->size < 16) return 0;
    if (memcmp(r->data, p->uuid, 16)) return 0;
    Skip(r, 16); LapisReader_String(r, username, sizeof(username));
    if (strcmp(username, p->username) || Count(r, 0) != 0 || !Done(r)) return 0;
    if (!Queue(p, 3, NULL, 0)) return 0;
    p->state = LAPIS_CONFIG;
    n = PutString(data, "en_US");
    data[n++] = 4; /* view distance */
    data[n++] = 0; data[n++] = 1; /* chat mode, colors */
    data[n++] = 0; data[n++] = 1; /* no skin layers, right hand */
    data[n++] = 0; data[n++] = 0; data[n++] = 0; /* filter, listing, particles */
    return Queue(p, 0, data, n);
}

static int Config(struct LapisProtocol* p, int id, struct LapisReader* r) {
    char a[256], b[256], c[256]; cc_uint8 data[96]; int n, i, count;
    if (id == 2) return LapisProtocol_Fail(p, "Server disconnected during configuration");
    if (id == 0x0E) {
        if (p->knownPacks || Count(r, 1) != 1) return 0;
        LapisReader_String(r, a, sizeof(a)); LapisReader_String(r, b, sizeof(b));
        LapisReader_String(r, c, sizeof(c));
        if (!Done(r) || strcmp(a, "minecraft") || strcmp(b, "core") || strcmp(c, "1.21.8"))
            return LapisProtocol_Fail(p, "Unsupported known-pack profile (requires minecraft/core/1.21.8)");
        data[0] = 1; n = 1 + PutString(data + 1, a);
        n += PutString(data + n, b); n += PutString(data + n, c);
        p->knownPacks = 1; return Queue(p, 7, data, n);
    }
    if (id == 7) return p->knownPacks && Registry(p, r);
    if (id == 0x0D) {
        if (!Tags(r)) return 0;
        p->tagsReceived = 1; return 1;
    }
    if (id == 0x0C) {
        count = Count(r, 64);
        for (i = 0; i < count && !r->failed; i++) LapisReader_String(r, a, sizeof(a));
        p->featureCount = count; return Done(r);
    }
    if (id == 1) { /* plugin payload: validate channel, do not execute or download anything */
        LapisReader_String(r, a, sizeof(a)); return !r->failed;
    }
    if (id == 4) return r->size == 8 && Queue(p, 4, r->data, 8);
    if (id == 5) return r->size == 4 && Queue(p, 5, r->data, 4);
    if (id == 3) {
        if (!Done(r) || !RequiredRegistries(p)) return 0;
        if (!Queue(p, 3, NULL, 0)) return 0;
        p->state = LAPIS_PLAY; return 1;
    }
    return LapisProtocol_Fail(p, "Unexpected configuration packet for the pinned Lapis profile");
}

static int PlayLogin(struct LapisProtocol* p, struct LapisReader* r) {
    char key[128]; int i, count;
    if (p->joined) return 0;
    p->entityId = (int)Big(r, 4); Byte(r);
    count = Count(r, 128);
    if (!count) return 0;
    for (i = 0; i < count && !r->failed; i++) LapisReader_String(r, key, sizeof(key));
    Count(r, 1000000); p->viewDistance = Count(r, 64); Count(r, 64);
    Byte(r); Byte(r); Byte(r);
    p->dimensionType = Count(r, 31);
    if (p->dimensionType != 0) return 0;
    LapisReader_String(r, p->dimension, sizeof(p->dimension)); Skip(r, 8);
    p->gamemode = Byte(r); Byte(r); Byte(r); Byte(r);
    if (Byte(r)) { LapisReader_String(r, key, sizeof(key)); Skip(r, 8); }
    Count(r, 1000000); Count(r, 384); Byte(r);
    if (!Done(r) || p->gamemode > 3) return 0;
    p->joined = 1; return 1;
}

static int Play(struct LapisProtocol* p, int id, struct LapisReader* r) {
    cc_uint8 data[5]; cc_uint32 teleport; double x, y, z; float yaw, pitch; int n;
    if (id == 0x1C) return LapisProtocol_Fail(p, "Server disconnected during play");
    if (id == 0x2B) return PlayLogin(p, r);
    if (!p->joined) return 0;
    if (id == 0x26) {
        if (r->size != 8) return 0;
        p->keepalives++; return Queue(p, 0x1B, r->data, 8);
    }
    if (id == 0x41) {
        teleport = LapisReader_VarInt(r);
        x = Double(r); y = Double(r); z = Double(r);
        Double(r); Double(r); Double(r);
        yaw = Float(r); pitch = Float(r);
        /* Lapis only emits absolute teleports. Reject unsupported flags explicitly. */
        if (Big(r, 4) != 0 || !Done(r)) return 0;
        p->x = x; p->y = y; p->z = z; p->yaw = yaw; p->pitch = pitch;
        p->teleports++;
        n = LapisProtocol_EncodeVarInt(data, teleport);
        return Queue(p, 0, data, n);
    }
    /* Gameplay modules own these bounded payloads via the packet callback.
       skipped counts packets delegated by the connection core, not missing features. */
    if (id == 0x27) p->chunks++;
    p->skipped++; return 1;
}

static int Dispatch(struct LapisProtocol* p) {
    struct LapisReader r; cc_uint32 id; int state = p->state, ok = 0, offset;
    static const cc_uint8 ping[8] = {0x4C, 0x61, 0x70, 0x69, 0x73, 0x43, 0x75, 0x62};
    LapisReader_Init(&r, p->frame, p->frameSize);
    id = LapisReader_VarInt(&r); offset = r.pos;
    if (r.failed || id > 255) return LapisProtocol_Fail(p, "Invalid packet ID");
    LapisReader_Init(&r, p->frame + offset, p->frameSize - offset);
    if (state == LAPIS_STATUS) {
        if (id == 0 && !p->status[0]) {
            LapisReader_String(&r, p->status, sizeof(p->status));
            ok = Done(&r) && Queue(p, 1, ping, 8);
        } else if (id == 1 && p->status[0] && r.size == 8 && !memcmp(r.data, ping, 8)) {
            p->statusPong = 1; ok = 1;
        }
    } else if (state == LAPIS_LOGIN) ok = Login(p, (int)id, &r);
    else if (state == LAPIS_CONFIG) ok = Config(p, (int)id, &r);
    else if (state == LAPIS_PLAY) ok = Play(p, (int)id, &r);
    if (!ok) return LapisProtocol_Fail(p, "Malformed or out-of-order packet in Lapis connection");
    p->packets++;
    if (p->handler) p->handler(p, state, (int)id, r.data, r.size, p->context);
    return p->state != LAPIS_FAILED;
}

int LapisProtocol_Feed(struct LapisProtocol* p, const cc_uint8* data, int size) {
    int b, n;
    if (p->state == LAPIS_FAILED) return 0;
    if (size < 0 || (size && !data)) return LapisProtocol_Fail(p, "Invalid input buffer");
    while (size > 0) {
        if (!p->frameSize) {
            b = *data++; size--;
            if (p->lengthBytes == 3) return LapisProtocol_Fail(p, "Packet length exceeds three bytes");
            p->lengthValue |= (cc_uint32)(b & 127) << (p->lengthBytes++ * 7);
            if (b & 128) {
                if (p->lengthBytes == 3) return LapisProtocol_Fail(p, "Overlong packet length");
                continue;
            }
            if (!p->lengthValue || p->lengthValue > LAPIS_MAX_PACKET)
                return LapisProtocol_Fail(p, "Packet length outside 1..524288 bytes");
            p->frameSize = (int)p->lengthValue;
        }
        n = p->frameSize - p->frameUsed;
        if (n > size) n = size;
        memcpy(p->frame + p->frameUsed, data, (size_t)n);
        p->frameUsed += n; data += n; size -= n;
        if (p->frameUsed != p->frameSize) continue;
        if (!Dispatch(p)) return 0;
        p->frameSize = p->frameUsed = p->lengthBytes = 0; p->lengthValue = 0;
    }
    return 1;
}

int LapisProtocol_EndInput(struct LapisProtocol* p) {
    if (p->frameUsed || p->lengthBytes) return LapisProtocol_Fail(p, "Connection closed inside a packet");
    if (p->state == LAPIS_STATUS && p->statusPong) { p->state = LAPIS_OFFLINE; return 1; }
    return LapisProtocol_Fail(p, "Connection closed by peer");
}

int LapisProtocol_MarkLoaded(struct LapisProtocol* p) {
    if (p->state != LAPIS_PLAY || !p->joined || !p->teleports || !p->chunks || p->loaded) return 0;
    if (!Queue(p, 0x2B, NULL, 0)) return 0;
    p->loaded = 1; return 1;
}
