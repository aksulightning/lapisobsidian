#ifndef LAPIS_DURABLE_H
#define LAPIS_DURABLE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
/* Bounded regular-file reads; missing files are represented by *data == NULL. */
bool durable_read(const char *path, uint8_t **data, size_t *size, size_t limit);
bool durable_replace(const char *path, const void *data, size_t size);
bool durable_remove(const char *path);
bool durable_sync(const char *path);
uint32_t durable_crc(const void *data, size_t size);
void durable_put32(uint8_t *p, uint32_t v);
uint32_t durable_get32(const uint8_t *p);
void durable_put64(uint8_t *p, uint64_t v);
uint64_t durable_get64(const uint8_t *p);
#endif
