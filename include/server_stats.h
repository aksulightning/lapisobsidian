#ifndef H_SERVER_STATS
#define H_SERVER_STATS
#include <stddef.h>
#include <stdint.h>

/* Process-wide tick intervals, not per-world server_ticks. */
void server_stats_reset (void);
void server_stats_record_tick (int64_t elapsed_us);
void server_stats_format (char *output, size_t capacity);
#endif
