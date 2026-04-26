#include <string.h>
#include <stdlib.h>
#include "tee_handler.h"
#include "update.h"
#include "gemini.h"
#include "openai.h"
#include "utils.h"

static TeeContext tc;

static gboolean timer_callback(gpointer data); // Forward declaration

static gboolean update_tee_ui(gpointer data) {
    TeeResponseData *trd = (TeeResponseData *)data;
    char *ai_text = extract_ai_text(trd->response_text);
    if (ai_text) {
        append_to_view(trd->app->gemini_view, "AI Tee Analysis: ", ai_text);
        save_tee_to_history(trd->terminal_output, ai_text);
        free(ai_text);
    }

    // Inside tee_handler.c -> update_tee_ui()
    gtk_label_set_text(GTK_LABEL(trd->app->status_label), "Ready");

    free(trd->response_text);
    free(trd->terminal_output);
    free(trd);
    return FALSE;
}

void tee_flush_timed() {
    g_mutex_lock(&tc.mutex);
    char *local_cmd = g_strdup(tc.cmd_buffer);
    char *local_out = g_strdup(tc.out_buffer);
    
    // ATOMIC RESET: Clear globals so the terminal can keep writing
    g_free(tc.cmd_buffer);
    g_free(tc.out_buffer);
    tc.cmd_buffer = g_strdup("");
    tc.out_buffer = g_strdup("");
    g_mutex_unlock(&tc.mutex);

    // 1. VALIDATION: Only proceed if there is significant OUTPUT to analyze
    if (!tc.app || strlen(local_out) < 3) {
        g_free(local_cmd); g_free(local_out);
        return;
    }

    // 2. BUILD PROMPT: Use the local copies
    char *final_prompt = g_strdup_printf(
        "You are a Senior Linux Engineer assisting with aiterm %s development. "
        "Analyze this terminal snippet concisely. Ignore echoed commands. "
        "Focus on hardware IDs or errors.\n\n"
        "COMMAND: %s\n"
        "OUTPUT: %s",
        AITERM_VERSION, local_cmd, local_out
    );

    // Inside tee_handler.c -> tee_flush_timed()
    gtk_label_set_text(GTK_LABEL(tc.app->status_label), "AI is analyzing...");

    char *response = NULL;
    if (strcasecmp(tc.app->provider, "gemini") == 0) {
        response = send_to_gemini(tc.app->api_key, tc.app->model, final_prompt);
    } else {
        response = send_to_openai(tc.app->api_key, final_prompt);
    }

    if (response) {
        TeeResponseData *trd = g_malloc0(sizeof(TeeResponseData));
        trd->app = tc.app;
        trd->response_text = response;
        trd->terminal_output = g_strdup(local_out); 
        g_idle_add(update_tee_ui, trd);
    }

    g_free(local_cmd);
    g_free(local_out);
    g_free(final_prompt);
}

void tee_handler_init(AppContext *app) {
    tc.app = app;
    tc.cmd_buffer = g_strdup("");
    tc.out_buffer = g_strdup("");
    tc.timer_id = 0;
    g_mutex_init(&tc.mutex);
}

static gpointer tee_thread_func(gpointer data) {
    tee_flush_timed();
    return NULL;
}

static gboolean timer_callback(gpointer data) {
    g_thread_new("tee_thread", (GThreadFunc)tee_thread_func, NULL);
    tc.timer_id = 0;
    return FALSE;
}

void tee_handle_input(const char *text) {
    g_mutex_lock(&tc.mutex);
    char *old = tc.cmd_buffer;
    tc.cmd_buffer = g_strconcat(old, text, NULL);
    g_free(old);
    g_mutex_unlock(&tc.mutex);

    if (tc.timer_id) g_source_remove(tc.timer_id);
    tc.timer_id = g_timeout_add(2000, timer_callback, NULL);
}

void tee_handle_output(const char *text) {
    g_mutex_lock(&tc.mutex);
    char *old = tc.out_buffer;
    tc.out_buffer = g_strconcat(old, text, NULL);
    g_free(old);
    g_mutex_unlock(&tc.mutex);

    if (tc.timer_id) g_source_remove(tc.timer_id);
    tc.timer_id = g_timeout_add(2000, timer_callback, NULL);
}
