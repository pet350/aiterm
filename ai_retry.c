
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gtk/gtk.h>

#include "gui.h"
#include "ai_retry.h"
#include "commands.h"
#include "utils.h"
#include "update.h"
#include "gemini.h"


void ai_retry_init(AppContext *app) {
    if (!app) return;

    // Initialize configuration parameters using gui.h layout
    app->retry_config.is_enabled = TRUE;
    app->retry_config.max_retries = 3;
    app->retry_config.delay_sec = 2;

    // Sync configuration and reset telemetry counter in state
    app->retry_state.config = app->retry_config;
    app->retry_state.total_retries_executed = 0;

    DEBUG_PRINT("[DEBUG]: [MAIN] Initializing AI Retry Handler...");
    DEBUG_PRINT("[DEBUG]: [LOADED] AI Retry Enabled [%s]", app->retry_config.is_enabled ? "ON" : "OFF");
    DEBUG_PRINT("[DEBUG]: [LOADED] AI Retry Max Attempts [%d]", app->retry_config.max_retries);
    DEBUG_PRINT("[DEBUG]: [LOADED] AI Retry Delay [%d sec]", app->retry_config.delay_sec);
    DEBUG_PRINT("[DEBUG]: [MAIN] Done! AI Retry Handler Initialized");
}

gboolean ai_retry_is_transient_error(long http_status, const char *response_body) {
    // Standard HTTP transient failure codes
    if (http_status == 429 || http_status == 503 || http_status == 500 || http_status == 502) {
        return TRUE;
    }

    if (response_body != NULL) {
        // High-demand and transient payload error strings
        if (strstr(response_body, "high demand") != NULL ||
            strstr(response_body, "RESOURCE_EXHAUSTED") != NULL ||
            strstr(response_body, "Spikes in demand are usually temporary") != NULL ||
            strstr(response_body, "please try again") != NULL ||
            strstr(response_body, "UNAVAILABLE") != NULL) {
            return TRUE;
        }
    }

    return FALSE;
}

char *ai_retry_execute_with_retry(AppContext *app,
                                  char *(*api_func)(const char *payload, long *out_http_code, void *user_data),
                                  const char *payload,
                                  void *user_data,
                                  long *final_http_code) {
    if (!app || !api_func) return NULL;

    // Bypass loop if retry feature is toggled off
    if (!app->retry_config.is_enabled) {
        return api_func(payload, final_http_code, user_data);
    }

    int attempts = 0;
    int max_retries = (app->retry_config.max_retries > 0) ? app->retry_config.max_retries : 1;
    int delay_sec = (app->retry_config.delay_sec > 0) ? app->retry_config.delay_sec : 2;

    char *response = NULL;
    long http_code = 0;

    while (attempts < max_retries) {
        attempts++;
        response = api_func(payload, &http_code, user_data);

        if (final_http_code) {
            *final_http_code = http_code;
        }

        // Return immediately if successful or if it's a permanent (non-transient) failure
        if (response != NULL && !ai_retry_is_transient_error(http_code, response)) {
            return response;
        }

        // Catch transient errors and perform silent backoff
        if (ai_retry_is_transient_error(http_code, response) && attempts < max_retries) {
            app->retry_state.total_retries_executed++;
            DEBUG_PRINT("[DEBUG]: [AI Retry] High demand / transient error detected (HTTP %ld). Attempt %d/%d failed. Retrying in %d seconds...",
                    http_code, attempts, max_retries, delay_sec);

            if (response) {
                free(response);
                response = NULL;
            }

            g_usleep((gulong)delay_sec * 1000000);
        } else {
            break;
        }
    }

    if (attempts >= max_retries && ai_retry_is_transient_error(http_code, response)) {
        g_warning("[DEBUG]: [AI Retry] Exhausted all %d retries for high-demand API error.", max_retries);
    }

    return response;
}
