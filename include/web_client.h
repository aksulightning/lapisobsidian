#ifndef H_WEB_CLIENT
#define H_WEB_CLIENT
#include <stddef.h>
#include <stdint.h>
#include <unistd.h>

/* Entire transport and embedded assets are absent unless compiled in. */
#if defined(LAPIS_OBSIDIAN_WEB_CLIENT) && (LAPIS_OBSIDIAN_WEB_CLIENT == 1 || LAPIS_OBSIDIAN_WEB_CLIENT == 2)
/* Open a nonblocking IPv4 HTTP/WebSocket listener; -1 on bind/setup failure. */
int web_client_listen(const char *address, uint16_t port);
/* Register only sockets accepted by the web listener. */
void web_client_reset(int fd, int64_t now);
void web_client_forget(int fd);
/* -1: close; 0: HTTP/upgrade pending; 1: game transport ready (or native fd). */
int web_client_poll(int fd, int64_t now);
int web_client_active(int fd);
/* Bounded nonblocking output pump during expensive terrain generation.
 * Does not read input or re-enter packet handling; native sockets are a no-op. */
int web_client_flush(int fd, int64_t now);
ssize_t web_client_receive(int fd, void *buffer, size_t size, int flags);
ssize_t web_client_send(int fd, const void *buffer, size_t size);
#endif
#endif
