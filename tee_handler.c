// part of aiterm project
// tee_handler.c
// Logic for capturing terminal streams and backgrounding AI analysis
// By: Peter Talbott
// Assisted by: Gemini
// May 2026

#include <string.h>
#include <stdlib.h>
#include "tee_handler.h"
#include "update.h"
#include "gemini.h"
#include "openai.h"
#include "utils.h"

// --- FORWARD DECLARATIONS (Private callbacks for 0.8.3) ---
static gboolean update_tee_ui(gpointer data);
static gpointer tee_ai_thread_func(gpointer data);

void tee_handler_init(AppContext *app) {
    if (!app) return;
    app->tee_accumulator = g_string_new("");
    g_mutex_init(&app->buffer_mutex);
    DEBUG_PRINT("DEBUG: Tee Handler initialized.\n");
}

/* 
 * ATOMIC SNAPSHOT (The 0.8.2 fix):
 * Grabs text and clears the buffer in one locked operation
 * to prevent splicing and duplication bugs.
 */
char* tee_extract_for_ai(AppContext *app) {
    if (!app || !app->tee_accumulator) return NULL;
    char *snapshot = NULL;

    g_mutex_lock(&app->buffer_mutex);
    if (app->tee_accumulator->len > 5) {
        // Snapshot the current buffer
        snapshot = g_strdup(app->tee_accumulator->str);
        // Atomic Clear: Wipe accumulator while still locked
        g_string_assign(app->tee_accumulator, "");
    }
    g_mutex_unlock(&app->buffer_mutex);

    return snapshot;
}

/*
 * THREADED FLUSH (The 0.8.3 fix):
 * This returns INSTANTLY to the UI thread, spawning a background
 * worker to handle the network latency of the AI API.
 */
void tee_flush_timed(AppContext *app) {
    if (!app || app->is_processing) return;

    // Grab the text snapshot safely
    char *local_out = tee_extract_for_ai(app);
    if (!local_out) return;

    // Set the "Busy" flag immediately to prevent overlapping requests
    app->is_processing = TRUE;
    update_status_label(app, "AI is analyzing (Background)...");

    // Package data for the background thread
    TeeResponseData *trd = g_malloc0(sizeof(TeeResponseData));
    trd->app = app;
    trd->terminal_output = local_out; // Hand off memory ownership to thread

    // START BACKGROUND THREAD: This is what stops the terminal from hanging!
    g_thread_new("tee_background_worker", (GThreadFunc)tee_ai_thread_func, trd);
}

/*
 * BACKGROUND WORKER:
 * This runs on a separate CPU thread. It can "hang" waiting for
 * the internet/API without affecting the terminal UI responsiveness.
 */
static gpointer tee_ai_thread_func(gpointer data) {
    TeeResponseData *trd = (TeeResponseData*)data;
    AppContext *app = trd->app;

    char *final_prompt = g_strdup_printf(
        "Analyze this terminal snippet concisely. Focus on hardware IDs, "
        "network configurations, or error messages.\n\n"
        "TERMINAL OUTPUT:\n%s", trd->terminal_output
    );

    char *response = NULL;
    if (app->provider && strcasecmp(app->provider, "gemini") == 0) {
        response = send_to_gemini(app, final_prompt);
    } else {
        response = send_to_openai(app->api_key, final_prompt);
    }

    if (response) {
        trd->response_text = response;
        // Signal the UI thread to display results safely
        g_idle_add(update_tee_ui, trd);
    } else {
        // RESET FLAG on failure so the app doesn't stay locked forever
        app->is_processing = FALSE;
        update_status_label(app, "Ready");
        g_free(trd->terminal_output);
        g_free(trd);
    }

    g_free(final_prompt);
    return NULL;
}

/*
 * GUI UPDATE CALLBACK:
 * Safely runs on the Main UI Thread to update GTK widgets.
 */
static gboolean update_tee_ui(gpointer data) {
    TeeResponseData *trd = (TeeResponseData *)data;
    if (!trd || !trd->app) return FALSE;

    char *ai_text = extract_ai_text(trd->response_text);

    if (ai_text) {
        // Display in AI Pane
        write_to_ai_pane(trd->app, "AI (Auto-Reply): ", ai_text, "user_tag", "ai_tag");

        // SAVE TO DATABASE: Ensure automated insights are in the 100-msg history
        save_tee_to_history(trd->terminal_output, ai_text);

        g_free(ai_text);
    } else {
        write_to_ai_pane(trd->app, "System: ", "Tee Analysis failed to return text.", "cmd_tag", "cmd_tag");
    }

    // --- CRITICAL: Reset processing flag so next timer tick can trigger ---
    update_status_label(trd->app, "Ready");
    trd->app->is_processing = FALSE;

    // Final memory cleanup
    if (trd->response_text) g_free(trd->response_text);
    if (trd->terminal_output) g_free(trd->terminal_output);
    g_free(trd);

    return FALSE;
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

    // Delta Upgrade: If AI is already busy, ignore heavy stream chatter
    // to protect context integrity and memory.
    if (app->is_processing && app->tee_accumulator->len > 51200) {
        g_mutex_unlock(&app->buffer_mutex);
        return;
    }

    g_string_append(app->tee_accumulator, text);

    // Hard limit safety: 64KB max buffer per flush
    if (app->tee_accumulator->len > 65536) {
        char *first_newline = strchr(app->tee_accumulator->str, '\n');
        if (first_newline) {
            size_t offset = first_newline - app->tee_accumulator->str + 1;
            g_string_erase(app->tee_accumulator, 0, offset);
        } else {
            g_string_erase(app->tee_accumulator, 0, 8192);
        }
    }

    g_mutex_unlock(&app->buffer_mutex);
}
