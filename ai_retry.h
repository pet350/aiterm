#ifndef AI_RETRY_H
#define AI_RETRY_H

#include "gui.h"

// Function pointer signature for individual HTTP request attempts
typedef char* (*AiHttpAttemptFunc)(const char *payload, long *out_http_code, void *user_data);

/* Module Core API */
void ai_retry_init(AppContext *app);
gboolean ai_retry_is_transient_error(long http_status, const char *response_body);
gboolean is_quota_or_ratelimit_error(long http_code, const char *raw_response);

char* ai_retry_execute_with_retry(AppContext *app, 
                                 AiHttpAttemptFunc attempt_func, 
                                 const char *payload, 
                                 void *user_data, 
                                 long *out_http_code);

double extract_recommended_delay(const char *raw_response, double default_delay_sec);

#endif // AI_RETRY_H

