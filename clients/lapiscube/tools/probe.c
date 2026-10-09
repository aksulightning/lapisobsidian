/* Headless integration test driver, not a replacement game client. */
#define _POSIX_C_SOURCE 200809L
#include "LapisSession.h"
#include "LapisIdentity.h"
#include "LapisWorld.h"
#include "LapisGameplay.h"
#include "LapisEntities.h"
#include "LapisEffects.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <netdb.h>

static struct LapisSession session;
static struct LapisWorld world;
static struct LapisGameplay gameplay;
static struct LapisEntities entities;
static int exercise, stage, tx,ty,tz,slot;
static cc_uint64 actionTime;
static cc_uint64 Now(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (cc_uint64)t.tv_sec * 1000 + (cc_uint64)t.tv_nsec / 1000000;
}
static int Read(void* context, cc_uint8* data, int capacity) {
    int fd = *(int*)context;
    ssize_t n = recv(fd, data, (size_t)capacity, 0);
    if (!n) return -1;
    if (n < 0) return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR ? 0 : -2;
    return (int)n;
}
static int Write(void* context, const cc_uint8* data, int size) {
    int fd = *(int*)context;
    ssize_t n = send(fd, data, (size_t)size, MSG_NOSIGNAL);
    if (n < 0) return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR ? 0 : -2;
    return n ? (int)n : -2;
}
static void Packet(struct LapisProtocol* p, int state, int id, const cc_uint8* data, int size, void* context) {
    struct LapisReader r;struct LapisSoundEvent sound;int x,y,z,value;
    (void)context;
    if (state != LAPIS_PLAY || id == 0x2B || id == 0x41 || id == 0x26)
        printf("packet state=%d id=0x%02X bytes=%d next=%d\n", state, id, size, p->state);
    if(state==LAPIS_PLAY) {
        LapisReader_Init(&r,data,size);
        if(id==0x27 && !LapisWorld_Chunk(&world,data,size))LapisProtocol_Fail(p,"Chunk decoding failed");
        if(id==0x57) {
            x=(cc_int32)LapisReader_VarInt(&r);z=(cc_int32)LapisReader_VarInt(&r);
            if(!LapisReader_Done(&r) || LapisWorld_Center(&world,x,z)<0)LapisProtocol_Fail(p,"Center failed");
        }
        if(id==0x08) {
            LapisWorld_Position(LapisReader_Big(&r,8),&x,&y,&z);value=LapisReader_Count(&r,65535);
            if(!LapisReader_Done(&r) || !LapisWorld_Block(&world,x,y,z,value))LapisProtocol_Fail(p,"Update failed");
        }
        if(!LapisGameplay_Packet(&gameplay,p,id,data,size))LapisProtocol_Fail(p,"Gameplay decode failed");
        if(!LapisEntities_Packet(&entities,id,data,size))LapisProtocol_Fail(p,"Entity decode failed");
        if(id==0x6E && !LapisEffects_Sound(&sound,data,size))LapisProtocol_Fail(p,"Sound decode failed");
    }
    /* Test-only headless join uses the same decoders as the native client. */
    if (p->teleports >= 2 && p->chunks && !p->loaded) LapisProtocol_MarkLoaded(p);
}
static int FindStack(void) {
    int i;
    for(i=36;i<45;i++)if(gameplay.slots[0][i].count)return i;
    return -1;
}
static void Exercise(struct LapisProtocol* p) {
    int candidate;
    if(!p->loaded || p->state!=LAPIS_PLAY)return;
    if(stage==0) {
        tx=(int)p->x;ty=(int)p->y-1;tz=(int)p->z;
        if(LapisWorld_Get(&world,tx,ty,tz)<=0) { LapisProtocol_Fail(p,"Exercise needs solid adjacent spawn terrain");return; }
        printf("exercise: mining (%d,%d,%d) state=%d\n",tx,ty,tz,LapisWorld_Get(&world,tx,ty,tz));
        LapisGameplay_Move(p,p->x,p->y,p->z,0,0,1);
        LapisGameplay_Dig(&gameplay,p,0,tx,ty,tz,1);stage=1;actionTime=Now();
    } else if(stage==1 && Now()-actionTime>=700) {
        LapisGameplay_Dig(&gameplay,p,2,tx,ty,tz,1);stage=2;
    } else if(stage==2 && LapisWorld_Get(&world,tx,ty,tz)==0 && (candidate=FindStack())>=0) {
        slot=candidate;printf("exercise: authoritative block removal and pickup item=%d count=%d slot=%d\n",gameplay.slots[0][slot].item,gameplay.slots[0][slot].count,slot);
        LapisGameplay_Click(&gameplay,p,slot,0,0);stage=3;
    } else if(stage==3 && gameplay.cursor.count && !gameplay.slots[0][slot].count) {
        LapisGameplay_Click(&gameplay,p,44,0,0);stage=4;
    } else if(stage==4 && !gameplay.cursor.count && gameplay.slots[0][44].count) {
        puts("exercise: server-owned cursor transfer passed");LapisGameplay_Close(&gameplay,p);
        LapisGameplay_Select(p,8);LapisGameplay_Dig(&gameplay,p,4,0,0,0,0);stage=5;actionTime=Now();
    } else if(stage==5 && !gameplay.slots[0][44].count) {
        puts("exercise: item drop acknowledged by inventory");stage=6;
    } else if(stage==6 && (candidate=FindStack())>=0) {
        slot=candidate;puts("exercise: dropped item picked up again");
        LapisGameplay_Select(p,slot-36);LapisGameplay_UseOn(&gameplay,p,tx,ty-1,tz,1,.5f,1,.5f);stage=7;
    } else if(stage==7 && LapisWorld_Get(&world,tx,ty,tz)>0 && !gameplay.slots[0][slot].count) {
        puts("exercise: placement and inventory consumption passed");
        LapisGameplay_Chat(p,"LapisCube integration",21);stage=8;
    } else if(stage==8 && strstr(gameplay.message,"LapisCube integration")) {
        puts("exercise: unsigned chat round trip passed");stage=9;
    }
}
static int Connect(const char* host, const char* port) {
    struct addrinfo hints, *list, *a; int fd = -1, err, flags; socklen_t len;
    struct pollfd poller;
    memset(&hints, 0, sizeof(hints)); hints.ai_socktype = SOCK_STREAM; hints.ai_family = AF_UNSPEC;
    err = getaddrinfo(host, port, &hints, &list);
    if (err) { fprintf(stderr, "DNS: %s\n", gai_strerror(err)); return -1; }
    for (a = list; a; a = a->ai_next) {
        fd = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        if (fd < 0) continue;
        flags = fcntl(fd, F_GETFL, 0);
        if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) { close(fd); fd = -1; continue; }
        err = connect(fd, a->ai_addr, a->ai_addrlen);
        if (!err) break;
        if (errno == EINPROGRESS) {
            poller.fd = fd; poller.events = POLLOUT; poller.revents = 0;
            if (poll(&poller, 1, 5000) > 0) {
                len = sizeof(err);
                if (!getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) && !err) break;
            }
        }
        close(fd); fd = -1;
    }
    freeaddrinfo(list); return fd;
}
int main(int argc, char** argv) {
    int fd, status, ok = 0, port; cc_uint8 uuid[16]; cc_uint64 start;
    struct LapisIO io; struct pollfd poller; struct LapisProtocol* p = &session.protocol;
    char* end; long parsed;
    if (argc != 5 || (strcmp(argv[1], "status") && strcmp(argv[1], "login") && strcmp(argv[1],"exercise"))) {
        fprintf(stderr, "usage: %s status|login|exercise host port username\n", argv[0]); return 2;
    }
    parsed = strtol(argv[3], &end, 10);
    if (*end || parsed < 1 || parsed > 65535) return 2;
    port = (int)parsed; status = !strcmp(argv[1], "status");
    exercise=!strcmp(argv[1],"exercise");
    start = Now(); LapisSession_Init(&session, start, Packet, NULL);
    LapisWorld_Init(&world);LapisGameplay_Init(&gameplay);LapisEntities_Init(&entities);
    LapisIdentity_OfflineUUID(argv[4], uuid);
    if (!LapisProtocol_Begin(p, argv[2], port, argv[4], uuid, status)) {
        fprintf(stderr, "%s\n", p->error); return 2;
    }
    fd = Connect(argv[2], argv[3]);
    if (fd < 0) { fputs("TCP connection failed\n", stderr); return 1; }
    io.read = Read; io.write = Write; io.context = &fd;
    poller.fd = fd;
    while (Now() - start < 45000) {
        if (!LapisSession_Pump(&session, &io, Now())) break;
        if(exercise)Exercise(p);
        if ((status && p->statusPong) || (!status && p->loaded && p->teleports >= 2 &&
             p->keepalives && !p->outputSize && (!exercise || stage==9))) { ok = 1; break; }
        poller.events = (short)(POLLIN | (p->outputSize ? POLLOUT : 0));
        poller.revents = 0;
        if (poll(&poller, 1, 50) < 0 && errno != EINTR) break;
    }
    if (status && p->statusPong) ok = 1;
    printf("result=%s state=%d registries=%d entries=%d tags=%d joined=%d loaded=%d chunks=%u teleports=%u keepalives=%u delegated=%u queued=%d\n",
           ok ? "PASS" : "FAIL", p->state, p->registryCount, p->entryCount, p->tagsReceived,
           p->joined, p->loaded, p->chunks, p->teleports, p->keepalives, p->skipped, p->outputSize);
    if (status) printf("status=%s\n", p->status);
    else printf("decoded=%u block_updates=%u gameplay_updates=%u health=%.0f food=%d exercise_stage=%d\n",world.decoded,world.changes,gameplay.revision,gameplay.health,gameplay.food,stage);
    if (!ok) fprintf(stderr, "reason=%s\n", p->error[0] ? p->error : "integration deadline");
    close(fd); return ok ? 0 : 1;
}
