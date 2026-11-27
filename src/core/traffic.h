/* Per-origin request pacing. Times are monotonic-style milliseconds supplied by the host. */
#ifndef AR_TRAFFIC_H
#define AR_TRAFFIC_H
#include "timeutil.h"
typedef struct {
    ar_ms next, cooldown, window;
    unsigned long requests, errors, limited, recent, previous, cache_hits;
    unsigned long seed;
    int spacing_ms, failures, last_status;
} ArTraffic;
void traffic_init(ArTraffic *t, unsigned long seed);
ar_ms traffic_wait(const ArTraffic *t, ar_ms now);
void traffic_start(ArTraffic *t, ar_ms now);
void traffic_result(ArTraffic *t, ar_ms now, int status, int retry_seconds);
unsigned long traffic_rpm(const ArTraffic *t, ar_ms now);
/* Retry-After accepts delta-seconds or HTTP-date relative to response Date. */
int traffic_retry_after(const char *value, ar_ms date);
#endif
