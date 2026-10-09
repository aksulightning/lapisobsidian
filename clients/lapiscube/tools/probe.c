/* Headless integration test driver, not a replacement game client. */
#define _POSIX_C_SOURCE 200809L
#include "LapisSession.h"
#include "LapisIdentity.h"
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
    (void)data; (void)context;
    if (state != LAPIS_PLAY || id == 0x2B || id == 0x41 || id == 0x26)
        printf("packet state=%d id=0x%02X bytes=%d next=%d\n", state, id, size, p->state);
    /* Test-only: allow server join completion after its final spawn teleport.
     * The graphical M1 adapter deliberately stays on its loading screen. */
    if (p->teleports >= 2 && p->chunks && !p->loaded) LapisProtocol_MarkLoaded(p);
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
    if (argc != 5 || (strcmp(argv[1], "status") && strcmp(argv[1], "login"))) {
        fprintf(stderr, "usage: %s status|login host port username\n", argv[0]); return 2;
    }
    parsed = strtol(argv[3], &end, 10);
    if (*end || parsed < 1 || parsed > 65535) return 2;
    port = (int)parsed; status = !strcmp(argv[1], "status");
    start = Now(); LapisSession_Init(&session, start, Packet, NULL);
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
        if ((status && p->statusPong) || (!status && p->loaded && p->teleports >= 2 &&
             p->keepalives && !p->outputSize)) { ok = 1; break; }
        poller.events = (short)(POLLIN | (p->outputSize ? POLLOUT : 0));
        poller.revents = 0;
        if (poll(&poller, 1, 50) < 0 && errno != EINTR) break;
    }
    if (status && p->statusPong) ok = 1;
    printf("result=%s state=%d registries=%d entries=%d tags=%d joined=%d loaded=%d chunks=%u teleports=%u keepalives=%u skipped=%u queued=%d\n",
           ok ? "PASS" : "FAIL", p->state, p->registryCount, p->entryCount, p->tagsReceived,
           p->joined, p->loaded, p->chunks, p->teleports, p->keepalives, p->skipped, p->outputSize);
    if (status) printf("status=%s\n", p->status);
    if (!ok) fprintf(stderr, "reason=%s\n", p->error[0] ? p->error : "integration deadline");
    close(fd); return ok ? 0 : 1;
}
