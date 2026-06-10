/* ratelimit.c                                  */
/* Part of project: aiterm                      */
/* C Program file to hale AI ratelimiting       */
/* By: Peter Talbott                            */
/* With assistance from Gemini and OpenAI       */
/* April, May 2026                              */


#include "ratelimit.h"
#include <unistd.h>
#include <stdio.h>
#include "utils.h"
#include "update.h"
#include "tee_handler.h"

void ratelimit_init(RateLimiter *rl, int rpm) {
    rl->last_request_time = 0;
    rl->requests_per_minute = rpm;
    pthread_mutex_init(&rl->lock, NULL);
}

bool ratelimit_check(RateLimiter *rl) {
    pthread_mutex_lock(&rl->lock);
    DEBUG_PRINT("[DEBUG] RATELIMIT_CHECK: Locked mutex");
    gint64 now = g_get_monotonic_time(); // Microseconds, monotonic
    gint64 interval = (60 * 1000000) / rl->requests_per_minute;

    if (now - rl->last_request_time < interval) {
        pthread_mutex_unlock(&rl->lock);
        return false;
    }
    rl->last_request_time = now;
    pthread_mutex_unlock(&rl->lock);
    DEBUG_PRINT("[DEBUG] RATELIMIT_CHECK: Unlocked mutex");
    return true;
}

void ratelimit_wait_if_needed(RateLimiter *rl) {
    // You can call this before making an API request
    while (!ratelimit_check(rl)) {
        sleep(1); // Wait 1 second and try checking again
    }
}
