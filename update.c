// update.c
// Part of the aiterm project
// C Program file containing functions for updating AI, Gemini, and Terminal
// By: Peter Talbott
// With assistance from Gemini and OpenAI
// May 2026

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

// Unified writing function for the AI Pane (Exactly 5 arguments)
void write_to_ai_pane(AppContext *app, const char *header, const char *body, const char *header_tag, const char *body_tag) {
    if (!app || !app->gemini_view) return;
    if (header) {
        append_ai_text(app, header, header_tag);
    }
    if (body) {
        append_ai_text(app, body, body_tag);
    }
    append_ai_text(app, "\n\n", NULL);
}

// Logic for process_for_ai (Tee Data Collection)
void process_for_ai(AppContext *app, const char *text, gboolean is_input) {
    if (!app->tee_enabled || !text) return;
    // app->sequence_id++; 
    if (is_input) {
        tee_handle_input(app, text); 
    } else {
        tee_handle_output(app, text); 
    }
}

// GUI update callback for AI responses
gboolean update_gui_with_response(gpointer data) {
    AIResponseData *rd = (AIResponseData *)data;
    if (!rd) return FALSE;

    if (rd->response_text) {
        if (strncmp(rd->response_text, "API ERROR:", 10) == 0) {
            // Display errors in Debian Red
            append_ai_text(rd->app, "System: ", "system_tag");
            append_ai_text(rd->app, rd->response_text, "body_tag");
        } else {
            // AI Response: Header in Green, Content in Yellow
            write_to_ai_pane(rd->app, "AI: ", rd->response_text, "ai_tag", "body_tag");
	}
    } else {
        write_to_ai_pane(rd->app, "System: ", "No response from provider.", "cmd_tag", "cmd_tag");
    }

    gtk_label_set_text(GTK_LABEL(rd->app->status_label), "Ready");

    rd->app->is_processing = FALSE;
    if (rd->response_text) free(rd->response_text);
    if (rd->original_prompt) free(rd->original_prompt);
    free(rd);
    return FALSE;
}

// Local command handler
static gboolean handle_local_command(const char *input, AppContext *app) {
    if (strcasecmp(input, "help") == 0) {
        write_to_ai_pane(app, "[ Help System ]\n", get_help_text(), "cmd_tag", "cmd_tag");
    } else if (strcasecmp(input, "features") == 0) {
        write_to_ai_pane(app, "[ Features ]\n", get_features_text(), "cmd_tag", "cmd_tag");
    } else if (strcasecmp(input, "hw") == 0) {
        write_to_ai_pane(app, "[ System Hardware ]\n", get_hw_stats(), "cmd_tag", "ai_tag");
    } else if (strcasecmp(input, "status") == 0) {
	display_status(app);
    } else if (strcasecmp(input, "clear") == 0) {
        GtkTextBuffer *buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(app->gemini_view));
        gtk_text_buffer_set_text(buf, "", -1);
    } else if (strcasecmp(input, "provider") == 0) {
        char info[512];
        snprintf(info, sizeof(info), "Provider: %s\nModel: %s", app->provider, app->model);
        write_to_ai_pane(app, "System: ", info, "cmd_tag", "cmd_tag");
    }
    // SEPARATE COMMAND: Tee Data Collection
    else if (strcasecmp(input, "tee on") == 0) {
        app->tee_enabled = TRUE;
        write_to_ai_pane(app, "System: ", "Tee Collection ENABLED", "cmd_tag", "cmd_tag");
    } else if (strcasecmp(input, "tee off") == 0) {
        app->tee_enabled = FALSE;
        // LOGIC: If Tee is off, Autoreply MUST be off
        app->autoreply_enabled = FALSE;
        write_to_ai_pane(app, "System: ", "Tee Collection & Auto-Reply DISABLED", "cmd_tag", "cmd_tag");
    }
    // SEPARATE COMMAND: Auto Reply Logic
    else if (strcasecmp(input, "autoreply on") == 0) {
        app->autoreply_enabled = TRUE;
        // LOGIC: If Autoreply is on, Tee MUST be on
        app->tee_enabled = TRUE;
        write_to_ai_pane(app, "System: ", "AI Auto-Reply & Tee Collection ENABLED", "cmd_tag", "cmd_tag");
    } else if (strcasecmp(input, "autoreply off") == 0) {
        app->autoreply_enabled = FALSE;
        write_to_ai_pane(app, "System: ", "AI Auto-Reply DISABLED", "cmd_tag", "cmd_tag");
    }
    else if (strcasecmp(input, "version") == 0) {
        write_to_ai_pane(app, "System: ", get_version_info(), "cmd_tag", "cmd_tag");
    } else if (strcasecmp(input, "history") == 0) {
        display_all_history(app);
    } else if (strcasecmp(input, "exit") == 0) {
        gtk_main_quit();
    } else {
        return FALSE; // Not a local command
    }
    return TRUE;      // Is a local command
}

void on_input_activate(GtkEntry *entry, gpointer data) {
    AppContext *app = (AppContext *)data;
    const char *input_text = gtk_entry_get_text(entry);
    if (strlen(input_text) == 0) return;

    if (handle_local_command(input_text, app)) {
        gtk_entry_set_text(entry, "");
        return;
    }

    // --- NEW: 0.7.5-beta:  Guard against double-processing ---
    if (app->is_processing) {
        update_status_label(app, "AI is already busy...");
        return;
    }

    // Rate Limiting
    static time_t last_request_time = 0;
    time_t now = time(NULL);
    if (now - last_request_time < 2) {
        write_to_ai_pane(app, "System: ", "Wait 2 seconds...", "cmd_tag", "cmd_tag");
        return;
    }
    last_request_time = now;

    gtk_label_set_text(GTK_LABEL(app->status_label), "AI is thinking...");
    write_to_ai_pane(app, "You: ", input_text, "user_tag", NULL);

    AIThreadData *td = g_malloc0(sizeof(AIThreadData));
    td->app = app;
    td->prompt = g_strdup(input_text);
    g_thread_new("ai_worker", (GThreadFunc)ai_thread_func, td);

    gtk_entry_set_text(entry, "");
}

void update_status_label(AppContext *app, const char *status) {
    if (app && app->status_label) {
        gtk_label_set_text(GTK_LABEL(app->status_label), status);
    }
}
