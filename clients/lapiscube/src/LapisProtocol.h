#ifndef LC_PROTOCOL_H
#define LC_PROTOCOL_H
#include "Core.h"
CC_BEGIN_HEADER

/* LapisCube protocol 772 foundation. Original code, BSD-3-Clause; see LICENSE. */
#define LAPIS_PROTOCOL_VERSION 772
#define LAPIS_MAX_PACKET (512 * 1024)
#define LAPIS_MAX_OUTPUT 32768
#define LAPIS_MAX_REGISTRIES 32
#define LAPIS_MAX_ENTRIES 2048
#define LAPIS_NAMES_SIZE 65536

enum LapisState { LAPIS_OFFLINE, LAPIS_STATUS, LAPIS_LOGIN, LAPIS_CONFIG, LAPIS_PLAY, LAPIS_FAILED };
struct LapisReader { const cc_uint8* data; int size, pos, failed; };
struct LapisRegistry { int name, first, count; };
struct LapisProtocol;
typedef void (*LapisPacketHandler)(struct LapisProtocol*, int state, int id,
                                 const cc_uint8* data, int size, void* context);

/* Caller owns this fixed-size object. No parser allocations or hidden sockets. */
struct LapisProtocol {
    enum LapisState state;
    char error[160], status[2048], username[16], dimension[128];
    cc_uint8 uuid[16];
    cc_uint8 frame[LAPIS_MAX_PACKET], output[LAPIS_MAX_OUTPUT];
    int frameSize, frameUsed, lengthBytes, outputSize;
    cc_uint32 lengthValue;
    struct LapisRegistry registries[LAPIS_MAX_REGISTRIES];
    int entries[LAPIS_MAX_ENTRIES], registryCount, entryCount, namesUsed;
    char names[LAPIS_NAMES_SIZE];
    int knownPacks, tagsReceived, featureCount, joined, loaded, statusPong;
    int entityId, gamemode, viewDistance, dimensionType;
    double x, y, z;
    float yaw, pitch;
    unsigned packets, skipped, chunks, teleports, keepalives;
    LapisPacketHandler handler;
    void* context;
};

void LapisReader_Init(struct LapisReader* r, const cc_uint8* data, int size);
cc_uint32 LapisReader_VarInt(struct LapisReader* r);
int LapisReader_String(struct LapisReader* r, char* dst, int capacity);
int LapisProtocol_EncodeVarInt(cc_uint8* dst, cc_uint32 value);
void LapisProtocol_Init(struct LapisProtocol* p, LapisPacketHandler handler, void* context);
int LapisProtocol_Begin(struct LapisProtocol* p, const char* host, int port,
                        const char* username, const cc_uint8 uuid[16], int status);
int LapisProtocol_Feed(struct LapisProtocol* p, const cc_uint8* data, int size);
void LapisProtocol_ConsumeOutput(struct LapisProtocol* p, int size);
int LapisProtocol_Fail(struct LapisProtocol* p, const char* reason);
int LapisProtocol_EndInput(struct LapisProtocol* p);
int LapisProtocol_MarkLoaded(struct LapisProtocol* p);
/* Returns received registry entry in wire ID order, or NULL. */
const char* LapisProtocol_RegistryEntry(const struct LapisProtocol* p,
                                      const char* registry, int id);
CC_END_HEADER
#endif
