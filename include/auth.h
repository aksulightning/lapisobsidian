#ifndef LAPIS_AUTH_H
#define LAPIS_AUTH_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <unistd.h>
#ifndef LAPIS_ENABLE_EXPERIMENTAL_AUTH
#define LAPIS_ENABLE_EXPERIMENTAL_AUTH 0
#endif
#if LAPIS_ENABLE_EXPERIMENTAL_AUTH != 0 && LAPIS_ENABLE_EXPERIMENTAL_AUTH != 1
#error LAPIS_ENABLE_EXPERIMENTAL_AUTH must be 0 or 1
#endif
#if LAPIS_ENABLE_EXPERIMENTAL_AUTH && (defined(LAPIS_SINGLEPLAYER) || defined(ESP_PLATFORM))
#error Experimental authentication requires the native multiplayer server
#endif
bool auth_init(void);
void auth_shutdown(void);
bool auth_accept(int fd, int64_t now);
void auth_forget(int fd);
void auth_poll(void);
/* -1 disconnect, 0 pending, 1 continue packet processing. */
int auth_progress(int fd, int64_t now);
bool auth_login_packet(int fd, int packet_id);
bool auth_encrypted(int fd);
bool auth_decrypt(int fd, uint8_t *bytes, size_t size);
ssize_t auth_send(int fd, const void *data, size_t size, ssize_t (*sink)(int,const void *,ssize_t));
bool auth_profile(const char *json, size_t size, const char *claimed, uint8_t uuid[16], char name[17]);
bool auth_name_valid(const char *name, size_t size);
#ifdef LAPIS_AUTH_TESTING
void auth_test_response(int fd, long status, const char *body);
const char *auth_test_url(int fd);
#endif
#endif
