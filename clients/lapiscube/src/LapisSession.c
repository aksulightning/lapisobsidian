#include "LapisSession.h"

void LapisSession_Init(struct LapisSession* s, cc_uint64 now,
                       LapisPacketHandler handler, void* context) {
    LapisProtocol_Init(&s->protocol, handler, context);
    s->started = s->lastRead = now; s->frameStarted = 0; s->previousPackets = 0;
}

static int Flush(struct LapisProtocol* p, struct LapisIO* io) {
    int n, budget = 32768;
    while (p->outputSize && budget > 0) {
        n = io->write(io->context, p->output, p->outputSize);
        if (n < 0 || n > p->outputSize) return LapisProtocol_Fail(p, "TCP write failed");
        if (!n) break;
        budget -= n; LapisProtocol_ConsumeOutput(p, n);
    }
    return 1;
}

int LapisSession_Pump(struct LapisSession* s, struct LapisIO* io, cc_uint64 now) {
    struct LapisProtocol* p = &s->protocol;
    cc_uint8 data[16384]; int n, budget = 256 * 1024;
    if (p->state == LAPIS_FAILED || p->state == LAPIS_OFFLINE) return 0;
    if ((!p->joined && now - s->started > 15000) || now - s->lastRead > 30000)
        return LapisProtocol_Fail(p, "Server connection timed out");
    if (s->frameStarted && now - s->frameStarted > 15000)
        return LapisProtocol_Fail(p, "Incomplete packet timed out");
    if (!Flush(p, io)) return 0;
    while (budget > 0) {
        n = io->read(io->context, data, sizeof(data));
        if (n == -1) return LapisProtocol_EndInput(p);
        if (n < 0 || n > (int)sizeof(data)) return LapisProtocol_Fail(p, "TCP read failed");
        if (!n) break;
        s->lastRead = now; budget -= n;
        if (!LapisProtocol_Feed(p, data, n)) return 0;
        if (p->packets != s->previousPackets || !p->lengthBytes) s->frameStarted = 0;
        if (p->lengthBytes && !s->frameStarted) s->frameStarted = now ? now : 1;
        s->previousPackets = p->packets;
        if (!Flush(p, io)) return 0;
    }
    return Flush(p, io);
}
