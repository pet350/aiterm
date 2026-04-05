#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <time.h>
#include <stdlib.h>

#include "update.h"
#include "openai.h"
#include "gemini.h"  // <--- ADD THIS
#include "help.h"
#include "utils.h"

// --- Rate limiting ---
static time_t last_request_time = 0;

// --- Tee Mode ---
static int tee_enabled = 0;

#define TEE_BUFFER_SIZE 2048
static char tee_buffer[TEE_BUFFER_SIZE];
static size_t tee_index = 0;

//  a small struct to hold the data for the thread
typedef struct {
    AppContext *app;
    char *prompt;
    char *response_text;
    char *original_prompt;
} AIThreadData;

// --- Forward declarations ---
void flush_to_ai(AppContext *app);

// --- Helper: append text to GtkTextView ---
void append_to_view(GtkWidget *view, const char *prefix, const char *text) {
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(buffer, &end);

    if (prefix) gtk_text_buffer_insert(buffer, &end, prefix, -1);
    gtk_text_buffer_insert(buffer, &end, text, -1);
    gtk_text_buffer_insert(buffer, &end, "\n\n", -1);

    GtkTextMark *mark = gtk_text_buffer_get_insert(buffer);
    gtk_text_view_scroll_mark_onscreen(GTK_TEXT_VIEW(view), mark);
}

// --- VERY BASIC sanitizer (expand later!) ---
char* sanitize(const char *input) {
    char *clean = strdup(input);
    if (!clean) return NULL;

    // naive redactions (expand with regex later)
    char *p;

    if ((p = strstr(clean, "password"))) {
        memcpy(p, "[REDACTED]", 10);
	p[10] = '\0';
    }

    if ((p = strstr(clean, "token"))) {
        memcpy(p, "[REDACTED]", 10);
	p[10] = '\0';
    }

    return clean;
}

// --- Tee buffer processing ---
void process_for_ai(AppContext *app, const char *text)
{
    if (!tee_enabled || !text) return;

    size_t len = strlen(text);

    if (tee_index + len >= TEE_BUFFER_SIZE - 1) {
        printf("DEBUG: buffer full → flushing\n");
        flush_to_ai(app);
    }

    memcpy(tee_buffer + tee_index, text, len);
    tee_index += len;
    tee_buffer[tee_index] = '\0';

    printf("DEBUG: buffer now: [%s]\n", tee_buffer);

    // 🔥 Detect BOTH \n and \r
    if (strchr(tee_buffer, '\n') || strchr(tee_buffer, '\r')) {
        printf("DEBUG: newline detected → flushing\n");
        flush_to_ai(app);
    }
}

// --- Flush tee buffer to OpenAI ---
void flush_to_ai(AppContext *app)
{
    
    if (!tee_enabled || tee_index == 0) {
        printf("DEBUG: flush skipped (empty or disabled)\n");
        return;
    }

    tee_buffer[tee_index] = '\0';

    printf("DEBUG: Tee buffer contents before flush: [%s]\n", tee_buffer);
    printf("DEBUG: Tee buffer index: %zu\n", tee_index);

    char *clean = sanitize(tee_buffer);
    if (!clean) return;

    char prompt[4096];
    snprintf(prompt, sizeof(prompt),
        "You are a Linux assistant. Analyze this command/output:\n\n%s",
        clean
    );
    char *response = send_to_openai(app->api_key, prompt);
    printf("DEBUG: RAW API RESPONSE:\n%s\n", response ? response : "NULL");

    if (response) {
        printf("RAW API RESPONSE:\n%s\n", response);

        char *ai_text = extract_ai_text(response);

        if (ai_text) {
            append_to_view(app->gemini_view, "AI Tee: ", ai_text);
            free(ai_text);
        } else {
            append_to_view(app->gemini_view, "System: ", "Failed to parse AI response.");
        }
    free(response);
    free(clean);
	}
    // 🔥 ONLY clear buffer AFTER everything is done
    tee_index = 0;
    tee_buffer[0] = '\0';
}

// 2. The GUI Update (The function you asked about)
static gboolean update_gui_with_response(gpointer data) {
    AIResponseData *rd = (AIResponseData *)data;

    if (rd->response_text) {
        char *clean_text = extract_ai_text(rd->response_text);
        if (clean_text) {
            append_to_view(rd->app->gemini_view, "AI: ", clean_text);

            // This is where you save the exchange to history
            save_to_history(rd->original_prompt, clean_text);

            free(clean_text);
        }
        free(rd->response_text);
    }

    free(rd->original_prompt);
    gtk_label_set_text(GTK_LABEL(rd->app->status_label), "Ready");
    free(rd);
    return FALSE; // Tells GTK to stop calling this
}

// This runs in the background
gpointer ai_thread_func(gpointer data) {
    AIThreadData *td = (AIThreadData *)data;
    char *response = NULL;

    printf("DEBUG: Current Provider: [%s]\n", td->app->provider);

    if (strcmp(td->app->provider, "gemini") == 0) {
        response = send_to_gemini(td->app->api_key, td->prompt);
    } else {
        response = send_to_openai(td->app->api_key, td->prompt);
    }

    // Prepare the data for the GUI thread
    AIResponseData *rd = malloc(sizeof(AIResponseData));
    rd->app = td->app;
    rd->response_text = response;

    // Schedule the GUI update
    g_idle_add((GSourceFunc)update_gui_with_response, rd);

    // Clean up the input data
    free(td->prompt);
    free(td);
    return NULL;
}

// --- Main input handler ---
void on_input_activate(GtkEntry *entry, gpointer data) {
    AppContext *app = (AppContext *)data;
    const char *input_text = gtk_entry_get_text(entry);

    if (strlen(input_text) == 0) return;

    // --- LOCAL COMMANDS ---

    if (strcasecmp(input_text, "help") == 0) {
        append_to_view(app->gemini_view, "System: ", get_help_text());
        gtk_entry_set_text(entry, "");
        return;
    }

    if (strcasecmp(input_text, "clear") == 0) {
        GtkTextBuffer *buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(app->gemini_view));
        gtk_text_buffer_set_text(buf, "", -1);
        gtk_entry_set_text(entry, "");
        return;
    }

    if (strcasecmp(input_text, "hw") == 0) {
        char *hw = get_hw_stats();
        append_to_view(app->gemini_view, "System: ", hw);
        free(hw);
        gtk_entry_set_text(entry, "");
        return;
    }

    if (strcasecmp(input_text, "version") == 0) {
        append_to_view(app->gemini_view, "System: ", get_version_info());
        gtk_entry_set_text(entry, "");
        return;
    }

    // --- TEE CONTROL ---

    if (strcasecmp(input_text, "tee on") == 0) {
        tee_enabled = 1;
        append_to_view(app->gemini_view, "System: ", "AI Tee ENABLED");
        gtk_entry_set_text(entry, "");
        return;
    }

    if (strcasecmp(input_text, "tee off") == 0) {
        tee_enabled = 0;
        append_to_view(app->gemini_view, "System: ", "AI Tee DISABLED");
        gtk_entry_set_text(entry, "");
        return;
    }

    if (strcasecmp(input_text, "tee flush") == 0) {
        flush_to_ai(app);
        append_to_view(app->gemini_view, "System: ", "Tee buffer flushed");
        gtk_entry_set_text(entry, "");
        return;
    }

    // --- RATE LIMIT ---
    time_t now = time(NULL);
    if (now - last_request_time < 10) {
        append_to_view(app->gemini_view, "System: ", "Cooldown active. Wait a few seconds...");
        return;
    }
    last_request_time = now;


     // 1. Give the user immediate feedback
        gtk_label_set_text(GTK_LABEL(app->status_label), "AI is thinking...");

        // 2. Clean the prompt (Strip the root@UbuntuMini stuff)
        char *cleaned_input = strip_prompt(input_text);

        // 3. Setup the thread data
        AIThreadData *td = malloc(sizeof(AIThreadData));
        td->app = app;
        td->prompt = cleaned_input; 

        // 4. Fire the background thread
        g_thread_new("ai_thread", (GThreadFunc)ai_thread_func, td);

        // 5. Clear the entry box so the user can keep typing commands
        gtk_entry_set_text(entry, "");

    // --- NORMAL AI QUERY ---
    gtk_label_set_text(GTK_LABEL(app->status_label), "Thinking...");
    append_to_view(app->gemini_view, "You: ", input_text); 
    if (tee_enabled) {
       process_for_ai(app, input_text);
    }

    // Call the API
    char *response = send_to_openai(app->api_key, input_text);

    if (response) {
        // Use your utility function to parse the JSON response
        char *ai_text = extract_ai_text(response);

        if (ai_text) {
            append_to_view(app->gemini_view, "AI: ", ai_text);
            free(ai_text);
        } else {
            // Check if the raw response contains an error message
            if (strstr(response, "\"error\"")) {
                append_to_view(app->gemini_view, "API Error: ", response);
            } else {
                append_to_view(app->gemini_view, "System: ", "Failed to parse response.");
            }
        }
        free(response);
    } else {
        append_to_view(app->gemini_view, "Error: ", "Failed to get response.");
    }

    gtk_label_set_text(GTK_LABEL(app->status_label), "Ready");
    gtk_entry_set_text(entry, "");
}
