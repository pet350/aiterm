// part of aiterm project
// ai-retry.c
// Functions for resending payload when 
//  AI reports certain errors
// Added 0.9.6-omega
// By: Peter Talbott
// Assisted by: Gemini
// August 2026

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gtk/gtk.h>
#include <stdbool.h>
#include <glib.h>

#include "gui.h"
#include "ai_retry.h"
#include "commands.h"
#include "utils.h"
#include "update.h"
#include "gemini.h"

// Inspects the raw HTTP body or status code for rate limit/quota signatures
gboolean is_quota_or_ratelimit_error(long http_code, const char *raw_response) {
    // 1. Check HTTP Status Code
    if (http_code == 429) {
        return true;
    }

    // 2. Scan JSON body for Gemini Quota & Rate Limit signatures
    if (raw_response != NULL) {
        if (strstr(raw_response, "RESOURCE_EXHAUSTED") != NULL ||
            strstr(raw_response, "Quota exceeded") != NULL ||
            strstr(raw_response, "exceeded your current quota") != NULL ||
            strstr(raw_response, "Please retry in") != NULL) {
            return true;
        }
    }

    return false;
}

// Extracts retry delay from "Please retry in X.XXXXXs" string in raw_response
double extract_recommended_delay(const char *raw_response, double default_delay_sec) {
    if (!raw_response) return default_delay_sec;

    const char *p = strstr(raw_response, "Please retry in ");
    if (p) {
        p += strlen("Please retry in ");
        double parsed_seconds = 0.0;
        if (sscanf(p, "%lf", &parsed_seconds) == 1 && parsed_seconds > 0.0) {
            // Add a 0.25s safety margin to guarantee the window resets on Gemini's side
            return parsed_seconds + 0.25;
        }
    }

    return default_delay_sec;
}

void ai_retry_init(AppContext *app) {
    if (!app) return;

    // Initialize configuration parameters using gui.h layout
    app->retry_config.is_enabled = TRUE;
    app->retry_config.max_retries = 3;
    app->retry_config.delay_sec = 2;

    // Sync configuration and reset telemetry counter in state
    app->retry_state.config = app->retry_config;
    app->retry_state.total_retries_executed = 0;

    DEBUG_PRINT("[DEBUG]: [MAIN] Initializing AI Retry Handler...\n");
    DEBUG_PRINT("[DEBUG]: [LOADED] AI Retry Enabled [%s]\n", app->retry_config.is_enabled ? "ON" : "OFF");
    DEBUG_PRINT("[DEBUG]: [LOADED] AI Retry Max Attempts [%d]\n", app->retry_config.max_retries);
    DEBUG_PRINT("[DEBUG]: [LOADED] AI Retry Delay [%d sec]\n", app->retry_config.delay_sec);
    DEBUG_PRINT("[DEBUG]: [MAIN] Done! AI Retry Handler Initialized\n");
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

char* ai_retry_execute_with_retry(AppContext *app, 
                                 AiHttpAttemptFunc attempt_func, 
                                 const char *payload, 
                                 void *user_data, 
                                 long *out_http_code) {
    int max_attempts = app->retry_config.max_retries ? app->retry_config.max_retries : 3;
    double base_delay = app->retry_config.delay_sec ? app->retry_config.delay_sec : 2.0;

    char *raw_response = NULL;
    long http_code = 0;

    for (int attempt = 1; attempt <= max_attempts; attempt++) {
        if (raw_response) {
            free(raw_response);
            raw_response = NULL;
        }

        // Execute single HTTP request attempt
        raw_response = attempt_func(payload, &http_code, user_data);

        // Success condition (HTTP 200 and no embedded error body)
        if (http_code == 200 && !is_quota_or_ratelimit_error(http_code, raw_response)) {
            if (out_http_code) *out_http_code = http_code;
            return raw_response;
        }

        // Check if we hit quota limits or rate limits
        if (is_quota_or_ratelimit_error(http_code, raw_response)) {
            // Determine sleep duration (use API's recommended retry time if available)
            double sleep_sec = extract_recommended_delay(raw_response, base_delay * attempt);

            DEBUG_PRINT("[DEBUG]: [AI RETRY] Quota/Rate Limit hit (HTTP %ld). Attempt %d/%d.\n", 
                        http_code, attempt, max_attempts);
            DEBUG_PRINT("[DEBUG]: [AI RETRY] Backing off for %.2f seconds before next retry...\n", 
                        sleep_sec);

            if (attempt < max_attempts) {
                // Sleep using GLib microsecond delay
                g_usleep((gulong)(sleep_sec * 1000000.0));
                continue;
            }
        }

        // If it's a non-retryable error (e.g. 400 Bad Request, 401 Unauthorized), break early
        if (http_code >= 400 && http_code < 500 && http_code != 429) {
            DEBUG_PRINT("[DEBUG]: [AI RETRY] Non-retryable HTTP error (%ld) encountered. Aborting.\n", http_code);
            break;
        }

        // Standard exponential backoff for other transient failures (5xx, timeouts)
        if (attempt < max_attempts) {
            double sleep_sec = base_delay * attempt;
            DEBUG_PRINT("[DEBUG]: [AI RETRY] Transient error (HTTP %ld). Retrying in %.2f seconds...\n", http_code, sleep_sec);
            g_usleep((gulong)(sleep_sec * 1000000.0));
        }
    }

    if (out_http_code) *out_http_code = http_code;
    return raw_response;
}
