#ifndef AI_RETRY_H
#define AI_RETRY_H

#include "gui.h"

/* Module Core API */
void ai_retry_init(AppContext *app);
gboolean ai_retry_is_transient_error(long http_status, const char *response_body);

char *ai_retry_execute_with_retry(AppContext *app,
                                  char *(*api_func)(const char *payload, long *out_http_code, void *user_data),
                                  const char *payload,
                                  void *user_data,
                                  long *final_http_code);

#endif // AI_RETRY_H

