// Part of project: aiterm
// ratelimit.h
// C Program header file for rate limiting AI functions
// By: Peter Talbott
// With assistance from Gemini and OpenAI
// April, May 2026

#ifndef RATELIMIT_H
#define RATELIMIT_H

#include <stdbool.h>
#include <time.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <gtk/gtk.h>

typedef struct {
    gint64 last_request_time;
    int requests_per_minute;
    pthread_mutex_t lock;
} RateLimiter;

void ratelimit_init(RateLimiter *rl, int rpm);
bool ratelimit_check(RateLimiter *rl);
void ratelimit_wait_if_needed(RateLimiter *rl);

#endif

