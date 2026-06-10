#ifndef RATELIMIT_H
#define RATELIMIT_H

#include <stdbool.h>
#include <time.h>
#include <pthread.h>

typedef struct {
    time_t last_request_time;
    int requests_per_minute;
    pthread_mutex_t lock;
} RateLimiter;

void ratelimit_init(RateLimiter *rl, int rpm);
bool ratelimit_check(RateLimiter *rl);
void ratelimit_wait_if_needed(RateLimiter *rl);

#endif
