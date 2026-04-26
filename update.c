// Update.c
// Features multi threaded tee buffer

#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <time.h>
#include <stdlib.h>
#include <gtk/gtk.h>

#include "update.h"
#include "openai.h"
#include "gemini.h"
#include "help.h"
#include "utils.h"
#include "tee_handler.h"

// --- Rate limiting ---
static time_t last_request_time = 0;

// --- Tee Mode ---
static int tee_enabled = 0;

#define TEE_BUFFER_SIZE 4096
static char tee_buffer[TEE_BUFFER_SIZE];
static size_t tee_index = 0;

// Struct for background thread
typedef struct {
    AppContext *app;
    char *prompt;
    char *original_prompt;
} AIThreadData;

// --- Helper: append text to GtkTextView ---
void append_to_view(GtkWidget *view, const char *prefix, const char *text) {
    if (!view || !text) return;
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
    GtkTextIter end;

    // 1. Get the current end of the buffer
    gtk_text_buffer_get_end_iter(buffer, &end);

    // 2. Insert the new text
    if (prefix) gtk_text_buffer_insert(buffer, &end, prefix, -1);
    gtk_text_buffer_insert(buffer, &end, text, -1);
    gtk_text_buffer_insert(buffer, &end, "\n\n", -1);

    // 3. MOVE THE CURSOR: Force the 'insert' mark to the new end
    gtk_text_buffer_get_end_iter(buffer, &end);
    gtk_text_buffer_place_cursor(buffer, &end);

    GtkTextMark *mark = gtk_text_buffer_get_insert(buffer);
    gtk_text_view_scroll_mark_onscreen(GTK_TEXT_VIEW(view), mark);
}

// --- Basic sanitizer ---
char* sanitize(const char *input) {
    if (!input) return NULL;
    char *clean = strdup(input);
    char *p;
    if ((p = strstr(clean, "password"))) memcpy(p, "[REDACTED]", 10);
    if ((p = strstr(clean, "token"))) memcpy(p, "[REDACTED]", 10);
    return clean;
}

// --- Flush Tee Buffer to AI ---
void flush_to_ai(AppContext *app) {
    if (!tee_enabled || tee_index == 0) return;

    tee_buffer[tee_index] = '\0';
    char *clean_output = sanitize(tee_buffer);

    // Construct the actual prompt for the AI
    char final_prompt[5120];
    snprintf(final_prompt, sizeof(final_prompt),
        "You are a Linux assistant. Analyze this command/output:\n\n%s",
        clean_output);

    char *response = NULL;
    if (app->provider && strcasecmp(app->provider, "gemini") == 0) {
        DEBUG_PRINT("DEBUG: Tee flushing to Gemini...\n");
        response = send_to_gemini(app->api_key, app->model, final_prompt);
    } else {
        DEBUG_PRINT("DEBUG: Tee flushing to OpenAI...\n");
        response = send_to_openai(app->api_key, final_prompt);
    }

    if (response) {
        char *ai_text = extract_ai_text(response);
        if (ai_text) {
            append_to_view(app->gemini_view, "AI Tee: ", ai_text);
	    save_tee_to_history(clean_output, ai_text); 
            free(ai_text);
        }
        free(response);
    }

    free(clean_output);
    tee_index = 0;
    tee_buffer[0] = '\0';
}

void process_for_ai(AppContext *app, const char *text, gboolean is_input) {
    if (!tee_enabled || !text) return;

    if (is_input) {
        tee_handle_input(text);
    } else {
        tee_handle_output(text);
    }
}

// GUI update callback
static gboolean update_gui_with_response(gpointer data) {
    AIResponseData *rd = (AIResponseData *)data;
    if (rd->response_text) {
        char *clean_text = extract_ai_text(rd->response_text);
        if (clean_text) {
            append_to_view(rd->app->gemini_view, "AI: ", clean_text);
            save_to_history(rd->original_prompt, clean_text);
            free(clean_text);
        }
        free(rd->response_text);
    }
    free(rd->original_prompt);
    gtk_label_set_text(GTK_LABEL(rd->app->status_label), "Ready");
    free(rd);
    return FALSE;
}

// Background thread
gpointer ai_thread_func(gpointer data) {
    AIThreadData *td = (AIThreadData *)data;
    if (!td) return NULL;

    char *response = NULL;
    char *key = td->app->api_key ? strdup(td->app->api_key) : NULL;
    char *prov = td->app->provider ? strdup(td->app->provider) : strdup("openai");
    char *model = td->app->model ? strdup(td->app->model) : NULL;

    if (key) {
        if (strcasecmp(prov, "gemini") == 0) {
            response = send_to_gemini(key, model, td->prompt);
        } else {
            response = send_to_openai(key, td->prompt);
        }
    } else {
        response = strdup("{\"error\": {\"message\": \"No API Key found\"}}");
    }

    AIResponseData *rd = g_malloc0(sizeof(AIResponseData));
    rd->app = td->app;
    rd->response_text = response;
    rd->original_prompt = strdup(td->prompt);

    g_idle_add((GSourceFunc)update_gui_with_response, rd);

    if (key) free(key);
    if (model) free(model);
    free(prov);
    free(td->prompt);
    free(td);
    return NULL;
}

// Local command handler
static gboolean handle_local_command(const char *input, AppContext *app) {
    if (strcasecmp(input, "help") == 0) {
        append_to_view(app->gemini_view, "System: ", get_help_text());
    } else if (strcasecmp(input, "clear") == 0) {
        GtkTextBuffer *buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(app->gemini_view));
        gtk_text_buffer_set_text(buf, "", -1);
    } else if (strcasecmp(input, "provider") == 0) {
        char info[512];
        snprintf(info, sizeof(info), "DEBUG INFO:\nProvider: %s\nModel: %s\nKey Length: %ld",
             app->provider ? app->provider : "NULL",
             app->model ? app->model : "DEFAULT",
             app->api_key ? (long)strlen(app->api_key) : -1);
        append_to_view(app->gemini_view, "System: ", info);
    } else if (strcasecmp(input, "tee on") == 0) {
        tee_enabled = 1;
        append_to_view(app->gemini_view, "System: ", "AI Tee ENABLED");
    } else if (strcasecmp(input, "tee off") == 0) {
        tee_enabled = 0;
        append_to_view(app->gemini_view, "System: ", "AI Tee DISABLED");
    } else if (strcasecmp(input, "version") == 0) {
        append_to_view(app->gemini_view, "System: ", get_version_info());
    } else if (strcasecmp(input, "History") == 0) {
        // We can reuse the logic from load_history_to_api but print it to the view
        display_all_history(app);
        return TRUE;
    } else if (strcasecmp(input, "exit") == 0) {
        gtk_main_quit();
        return TRUE;
    } else {
        return FALSE;
    }
    return TRUE;
}

void on_input_activate(GtkEntry *entry, gpointer data) {
    AppContext *app = (AppContext *)data;
    const char *input_text = gtk_entry_get_text(entry);
    if (strlen(input_text) == 0) return;

    if (handle_local_command(input_text, app)) {
        gtk_entry_set_text(entry, "");
        return;
    }

    time_t now = time(NULL);
    if (now - last_request_time < 2) {
        append_to_view(app->gemini_view, "System: ", "Slow down...");
        return;
    }
    last_request_time = now;

    gtk_label_set_text(GTK_LABEL(app->status_label), "AI is thinking...");
    append_to_view(app->gemini_view, "You: ", input_text);

    AIThreadData *td = g_malloc0(sizeof(AIThreadData));
    td->app = app;
    td->prompt = strdup(input_text);

    g_thread_new("ai_thread", (GThreadFunc)ai_thread_func, td);
    gtk_entry_set_text(entry, "");
}

// End of update.c
// By: Peter Talbott
// With assistance from Gemini and OpenAI
// 2026
