#ifndef LAPIS_WEBCLIENT_H
#define LAPIS_WEBCLIENT_H
/* Optional transport only. Java packet validation and game rules remain shared. */
#if defined(LAPIS_ENABLE_WEBCLIENT) && LAPIS_ENABLE_WEBCLIENT == 1
#include <stddef.h>
#include <stdint.h>
int webclient_start(void);
void webclient_poll(int64_t now);
int webclient_accept(void);
int webclient_has(int fd);
int webclient_recv(int fd, void *data, size_t size, int peek);
int webclient_send(int fd, const void *data, size_t size);
int webclient_disconnect(int fd);
void webclient_stop(void);
#endif
#endif
