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

// Return ansi green "On"
static char* ON_VAL(AppContext *app) {
    int len = 32;
    char *out = g_malloc(len);
    snprintf(out, len, "%sOn%s", app->ansi.green, app->ansi.yellow);
    return out;
}

// Return ansi red "Off"
static char* OFF_VAL(AppContext *app) {
    int len = 32;
    char *out = g_malloc(len);
    snprintf(out, len, "%sOff%s", app->ansi.red, app->ansi.yellow);
    return out;
}

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

    char *lt_pl = g_strdup(app->ansi.lt_purple);
    char *cy    = g_strdup(app->ansi.cyan);
    char *yl    = g_strdup(app->ansi.yellow);
    char *gr    = g_strdup(app->ansi.green);
    char *red   = g_strdup(app->ansi.red);
    char *nml   = g_strdup(app->ansi.normal);

    // Initialize configuration parameters using gui.h layout
    app->retry_config.is_enabled = TRUE;
    app->retry_config.max_retries = 3;
    app->retry_config.delay_sec = 2;

    // Sync configuration and reset telemetry counter in state
    app->retry_state.config = app->retry_config;
    app->retry_state.total_retries_executed = 0;

    DEBUG_PRINT("[%s DEBUG %s]: [%sMAIN%s] %sInitializing AI Retry Handler...%s\n",
	lt_pl, nml, cy, nml, yl, nml);
    DEBUG_PRINT("[%s DEBUG %s]: [%sLOADED%s] %sAI Retry Enabled [%s]\n", 
	lt_pl, nml, cy, nml, yl, app->retry_config.is_enabled ? ON_VAL(app) : OFF_VAL(app) );

    DEBUG_PRINT("[%s DEBUG %s]: [%sLOADED%s] %sAI Retry Max Attempts [%s%d%s]%s\n",
	lt_pl, nml, cy, nml, yl, gr, app->retry_config.max_retries, yl, nml);
    DEBUG_PRINT("[%s DEBUG %s]: [%sLOADED%s] %sAI Retry Delay [%s%d sec%s]%s\n",
	lt_pl, nml, cy, nml, yl, red, app->retry_config.delay_sec, yl, nml);
    DEBUG_PRINT("[%s DEBUG %s]: [%sMAIN%s] %sDone!%s AI Retry Handler Initialized%s\n",
	lt_pl, nml, cy, nml, gr, yl, nml);

    g_free(lt_pl);
    g_free(cy);
    g_free(yl);
    g_free(gr);
    g_free(red);
    g_free(nml);
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

    char *lt_pl = g_strdup(app->ansi.lt_purple);
    char *cy    = g_strdup(app->ansi.cyan);
    char *yl    = g_strdup(app->ansi.yellow);
    char *gr    = g_strdup(app->ansi.green);
    char *red   = g_strdup(app->ansi.red);
    char *nml   = g_strdup(app->ansi.normal);

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

            DEBUG_PRINT("[%s DEBUG %s]: [%sAI RETRY%s] %sQuota/Rate Limit hit %s(HTTP %ld)%s. Attempt %s%d/%d%s.\n", 
                        lt_pl, nml, cy, nml, yl, gr, http_code, yl, gr, attempt, max_attempts, nml);

            DEBUG_PRINT("[%s DEBUG %s]: [%sAI RETRY%s]%s Backing off for [%s%.2f%s] seconds before next retry...%s\n", 
                        lt_pl, nml, cy, nml, yl, red, sleep_sec, yl, nml);

            if (attempt < max_attempts) {
                // Sleep using GLib microsecond delay
                g_usleep((gulong)(sleep_sec * 1000000.0));
                continue;
            }
        }

        // If it's a non-retryable error (e.g. 400 Bad Request, 401 Unauthorized), break early
        if (http_code >= 400 && http_code < 500 && http_code != 429) {
            DEBUG_PRINT("[%s DEBUG %s]: [%sAI RETRY%s] %sNon-retryable HTTP error (%s%ld%s) encountered. %sAborting.%s\n", 
		lt_pl, nml, cy, nml, yl, gr, http_code, yl, red, nml);
            break;
        }

        // Standard exponential backoff for other transient failures (5xx, timeouts)
        if (attempt < max_attempts) {
            double sleep_sec = base_delay * attempt;
            DEBUG_PRINT("[%s DEBUG %s]: [%sAI RETRY%s] %sTransient error (%sHTTP %ld%s). Retrying in [%s%.2f%s] seconds...%s\n", 
		lt_pl, nml, cy, nml,yl, red, http_code, yl, red, sleep_sec, yl, nml);
            g_usleep((gulong)(sleep_sec * 1000000.0));
        }
    }

    if (out_http_code) *out_http_code = http_code;

    g_free(lt_pl);
    g_free(cy);
    g_free(yl);
    g_free(gr);
    g_free(red);
    g_free(nml);

    return raw_response;
}
