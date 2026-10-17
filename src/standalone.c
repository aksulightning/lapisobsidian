#ifdef LAPIS_SINGLEPLAYER
#include <errno.h>
#include <string.h>
#include "standalone.h"
#include "webclient.h"
#include "globals.h"
#include <emscripten.h>
#include "tools.h"
#include "serialize.h"
#include "items.h"
#include "doors.h"
#include "signs.h"
#include "farming.h"
#include "circuits.h"

/* One in-memory stream. No TCP listener, WebSocket, proxy or second client. */
#define LOCAL_FD 30000
#define CAPACITY (256u * 1024u)
static unsigned char incoming[CAPACITY], outgoing[CAPACITY];
static size_t received, written;
static int connected, accepted, stopped, save_requested;
static unsigned iterations;
static int64_t last_save;

static void flush(void) {
    if (!written) return;
    EM_ASM({
        var data = HEAPU8.slice($0, $0 + $1);
        Module.pendingBytes += data.length;
        postMessage({type:'data', data:data.buffer}, [data.buffer]);
    }, outgoing, written);
    written = 0;
}
EMSCRIPTEN_KEEPALIVE int standalone_input(const void *data, unsigned size) {
    if (!connected || size > CAPACITY-received) return 0;
    memcpy(incoming+received, data, size); received += size; return 1;
}
EMSCRIPTEN_KEEPALIVE void standalone_save(void) { save_requested = 1; }
EMSCRIPTEN_KEEPALIVE void standalone_stop(void) { stopped = 1; }
int webclient_start(void) {
    connected = 1; last_save = get_program_time();
    EM_ASM({ postMessage({type:'ready'}); }); return 1;
}
int webclient_accept(void) {
    if (!connected || accepted) return -1;
    accepted = 1; return LOCAL_FD;
}
int webclient_has(int fd) { return fd == LOCAL_FD; }
int webclient_recv(int fd, void *data, size_t size, int peek) {
    if (fd != LOCAL_FD || !connected) return 0;
    if (!received) { errno = EAGAIN; return -1; }
    if (size > received) size = received;
    memcpy(data, incoming, size);
    if (!peek) { received -= size; memmove(incoming, incoming+size, received); }
    return (int)size;
}
int webclient_send(int fd, const void *data, size_t size) {
    const unsigned char *bytes = data;
    if (fd != LOCAL_FD || !connected || size > 0x7fffffff) return -1;
    size_t remaining = size;
    while (remaining) {
        size_t n = CAPACITY-written;
        if (n > remaining) n = remaining;
        memcpy(outgoing+written, bytes, n); written += n; bytes += n; remaining -= n;
        if (written == CAPACITY) flush();
    }
    return (int)size;
}
int webclient_disconnect(int fd) {
    if (fd != LOCAL_FD) return 0;
    flush(); connected = 0; stopped = 1; return 1;
}
void webclient_poll(int64_t now) { (void)now; }
void webclient_stop(void) { flush(); connected = 0; }
int standalone_yield(void) {
    /* Drain several complete protocol frames per browser turn, then let worker
       messages and IndexedDB callbacks run. No Asyncify suspension in handlers. */
    if (++iterations % 64 && !stopped) return 0;
    flush();
    emscripten_sleep(1);
    while (EM_ASM_INT({ return Module.saving || Module.pendingBytes > 1048576; }) && !stopped)
        emscripten_sleep(1);
    if (stopped) return 1;
    if (save_requested || get_program_time()-last_save >= 15000000) {
        writeBlockChangesToDisk(0,block_changes_count-1); writePlayerDataToDisk();
        int ok = items_save() && doors_save() && signs_save() && farming_save() && circuits_save();
        last_save = get_program_time(); save_requested = 0;
        EM_ASM({ Module.saveWorld(!!$0, false); }, ok);
        while (EM_ASM_INT({ return Module.saving; })) emscripten_sleep(1);
    }
    return 0;
}
#endif
