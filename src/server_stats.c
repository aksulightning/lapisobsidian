#include <stdio.h>
#include <string.h>
#include "globals.h"
#include "server_stats.h"

#define SAMPLE_COUNT 60
static int64_t intervals[SAMPLE_COUNT];
static unsigned next, count;

void server_stats_reset (void) {
  memset(intervals,0,sizeof(intervals)); next = count = 0;
}
void server_stats_record_tick (int64_t elapsed_us) {
  if (elapsed_us <= 0) return;
  intervals[next] = elapsed_us;
  next = (next+1)%SAMPLE_COUNT;
  if (count < SAMPLE_COUNT) count++;
}
void server_stats_format (char *output, size_t capacity) {
  double target = 1000000.0/TIME_BETWEEN_TICKS;
  if (!count) {
    snprintf(output,capacity,"TPS: warming up (target %.2f).",target);
    return;
  }
  double elapsed = 0;
  for (unsigned i = 0; i < count; i++) elapsed += (double)intervals[i];
  double tps = count*1000000.0/elapsed;
  if (tps > target) tps = target;
  snprintf(output,capacity,"TPS: %.2f / %.2f (last %u ticks, %.1fs).",
           tps,target,count,elapsed/1000000.0);
}
