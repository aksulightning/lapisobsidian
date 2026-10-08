#ifndef H_WEB_CLIENT
#define H_WEB_CLIENT
#include <stddef.h>
#include <stdint.h>
#include <unistd.h>

/* Entire transport and embedded assets are absent unless compiled in. */
#if defined(LAPIS_OBSIDIAN_WEB_CLIENT) && LAPIS_OBSIDIAN_WEB_CLIENT == 1
void web_client_reset(int fd, int64_t now);
void web_client_forget(int fd);
/* -1: close; 0: HTTP/upgrade pending; 1: game transport ready. */
int web_client_poll(int fd, int64_t now);
int web_client_active(int fd);
ssize_t web_client_receive(int fd, void *buffer, size_t size, int flags);
ssize_t web_client_send(int fd, const void *buffer, size_t size);
#endif
#endif
