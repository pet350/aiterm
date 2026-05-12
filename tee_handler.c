#include <string.h>
#include <stdlib.h>
#include "tee_handler.h"
#include "update.h"
#include "gemini.h"
#include "openai.h"
#include "utils.h"

// Forward declaration
static gboolean update_tee_ui(gpointer data);

void tee_handler_init(AppContext *app) {
    if (!app) return;
    app->tee_accumulator = g_string_new(""); 
    g_mutex_init(&app->buffer_mutex); 
    DEBUG_PRINT("DEBUG: Tee Handler initialized.\n");
}

char* tee_extract_for_ai(AppContext *app) {
    if (!app || !app->tee_accumulator) return NULL;

    g_mutex_lock(&app->buffer_mutex);
    char *result = g_strdup(app->tee_accumulator->str);
    g_string_assign(app->tee_accumulator, ""); 
    g_mutex_unlock(&app->buffer_mutex);

    return result; 
}

void tee_flush_timed(AppContext *app) {
    if (!app) return;

    char *local_out = tee_extract_for_ai(app);
    
    if (!local_out || strlen(local_out) < 5) {
        if (local_out) g_free(local_out);
        return;
    }

    // SET FLAG: We are now busy V0.7.5-beta
    app->is_processing = TRUE;

    char *final_prompt = g_strdup_printf(
        "Analyze this terminal snippet concisely. Focus on hardware IDs, "
        "network configurations, or error messages.\n\n"
        "TERMINAL OUTPUT:\n%s", local_out
    );

    update_status_label(app, "AI is analyzing...");

    char *response = NULL;    
    if (app->provider && strcasecmp(app->provider, "gemini") == 0) {
        response = send_to_gemini(app, final_prompt);
    } else {
        response = send_to_openai(app->api_key, final_prompt);
    }
    
    if (response) {
        TeeResponseData *trd = g_malloc0(sizeof(TeeResponseData));
        trd->app = app;
        trd->response_text = response;
        trd->terminal_output = g_strdup(local_out); 
        g_idle_add(update_tee_ui, trd);
    }

    g_free(local_out);
    g_free(final_prompt);
}

// Consolidated GUI update callback
static gboolean update_tee_ui(gpointer data) {
    TeeResponseData *trd = (TeeResponseData *)data;
    if (!trd || !trd->app) return FALSE;

    char *ai_text = extract_ai_text(trd->response_text);
    
    if (ai_text) {
        // Correct 5-argument call:
        // app, header, body, header_tag (Green), body_tag (Yellow)
        write_to_ai_pane(trd->app, "AI (Auto-Reply): ", ai_text, "user_tag", "ai_tag");
        g_free(ai_text);
    } else {
        write_to_ai_pane(trd->app, "System: ", "Tee Analysis failed to return text.", "cmd_tag", "cmd_tag");
    }

    // Reset status label
    update_status_label(trd->app, "Ready");

    // --- NEW: Release the lock ---
    trd->app->is_processing = FALSE;

    // Clean up memory
    if (trd->response_text) g_free(trd->response_text);
    if (trd->terminal_output) g_free(trd->terminal_output);
    g_free(trd);

    return FALSE; // Remove from idle loop
}

void tee_handle_input(AppContext *app, const char *text) {
    if (!text || !app->tee_accumulator) return;
    g_mutex_lock(&app->buffer_mutex);
    g_string_append(app->tee_accumulator, text);
    g_mutex_unlock(&app->buffer_mutex);
}

void tee_handle_output(AppContext *app, const char *text) {
    if (!text || !app->tee_accumulator) return;

    g_mutex_lock(&app->buffer_mutex);
    g_string_append(app->tee_accumulator, text);

    // Truncate if buffer > 50KB to protect RAM
    if (app->tee_accumulator->len > 51200) {
        size_t to_remove = app->tee_accumulator->len - 51200;
        g_string_erase(app->tee_accumulator, 0, to_remove);
    }
    g_mutex_unlock(&app->buffer_mutex);
}
