#ifndef LC_SESSION_H
#define LC_SESSION_H
#include "LapisProtocol.h"
CC_BEGIN_HEADER
/* I/O returns bytes transferred, 0 for would-block, -1 EOF, -2 error.
 * Both operations MUST be nonblocking; ownership/closing stays with caller. */
struct LapisIO {
    int (*read)(void* context, cc_uint8* data, int capacity);
    int (*write)(void* context, const cc_uint8* data, int size);
    void* context;
};
struct LapisSession {
    struct LapisProtocol protocol;
    cc_uint64 started, lastRead, frameStarted;
    unsigned previousPackets;
};
void LapisSession_Init(struct LapisSession* s, cc_uint64 now,
                       LapisPacketHandler handler, void* context);
int LapisSession_Pump(struct LapisSession* s, struct LapisIO* io, cc_uint64 now);
CC_END_HEADER
#endif
