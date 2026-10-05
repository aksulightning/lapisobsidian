#ifndef LAPISCLIENT_H
#define LAPISCLIENT_H
#include <stddef.h>
#include <stdint.h>
#ifdef LAPISCLIENT
int lc_start(void);
void lc_poll(void);
int lc_is(int fd);
int lc_json(int fd, const char *format, ...);
int lc_text(int fd, const char *type, const char *key, const char *text,
            size_t n);
int lc_chunk(int fd, int x, int z);
int lc_position(int fd, double x, double y, double z, float yaw, float pitch);
#else
static inline int lc_noop(void) { return 0; }
#define lc_start() 1
#define lc_poll() ((void)0)
#define lc_is(fd) 0
#define lc_json(...) lc_noop()
#define lc_text(...) lc_noop()
#define lc_chunk(...) lc_noop()
#define lc_position(...) lc_noop()
#endif
#endif
