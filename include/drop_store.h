#ifndef LAPIS_DROP_STORE_H
#define LAPIS_DROP_STORE_H
#include <stdbool.h>
#include <stddef.h>
#include "globals.h"
#ifndef LAPIS_ENABLE_EXPERIMENTAL_DROPSYSTEM
#define LAPIS_ENABLE_EXPERIMENTAL_DROPSYSTEM 0
#endif
#if LAPIS_ENABLE_EXPERIMENTAL_DROPSYSTEM != 0 && LAPIS_ENABLE_EXPERIMENTAL_DROPSYSTEM != 1
#error LAPIS_ENABLE_EXPERIMENTAL_DROPSYSTEM must be 0 or 1
#endif
#if LAPIS_ENABLE_EXPERIMENTAL_DROPSYSTEM && (defined(ESP_PLATFORM) || !defined(SYNC_WORLD_TO_DISK))
#error Experimental drops require a desktop filesystem or the standalone IDBFS build
#endif
/* Recover before reading world/player/sidecar files. root is "." or a Plate directory. */
bool drop_store_recover(const char *root, bool shared_players);
bool drop_store_begin(void);
bool drop_store_commit(void);
/* -1: buffer failed; 0: no transaction; 1: queued until durable commit. */
int drop_store_buffer(int fd, const void *data, size_t size);
bool drop_store_active(void);
#endif
