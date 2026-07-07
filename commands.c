// part of aiterm project
// commands.c
// local command handler / registry / wrappers
// By: Peter Talbott
// Assisted by: Gemini
// June 2026

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <gtk/gtk.h>
#include <unistd.h>

#include "commands.h"
#include "ratelimit.h"
#include "gui.h"
#include "utils.h"
#include "update.h"
#include "help.h"
#include "gemini.h"
#include "session_manager.h"
#include "session_manager_gui.h"
#include "crypto.h"
#include "noisefilter.h"
#include "status.h"
#include "config.h"

static CommandRegistry registry[] = {
    {"auto all", "Toggle (on/off): Tee, AutoReply, & AutoExecute", cmd_toggle_auto_all},
    {"autoreply", "Toggle real-time prompt analysis (on/off)", cmd_toggle_autoreply},
    {"autoexe", "Toggle execution of AI payloads (on/off)", cmd_toggle_autoexe},
    {"clear", "Clear the contents of the AI pane", handle_clear_wrapper},
    {"extended help", "Display extended help message", handle_extended_help},
    {"features", "Display new aiterm features", handle_features_wrapper},
    {"help", "Display this dynamic help menu", handle_help_wrapper},
    {"history", "Display mysql history", handle_history_wrapper},
    {"hw", "Display various hardware info", handle_hw_wrapper},
    {"list models", "Display all models from current provider", handle_list_models_wrapper},
    {"load config", "Loads configurations from aiterm.conf", handle_load_config_wrapper},
    {"noise filter", "Toggle noise filter (on/off)", cmd_toggle_noise_filter},
    {"noise add", "Add a pattern to the noise filter list", cmd_noise_add},
    {"noise delete", "Remove a pattern from the noise filter (TBA)", cmd_noise_delete},
    {"noise list", "List all active noise filter patterns (TBA)", cmd_noise_list},
    {"save config", "Saves configuration to aiterm.conf", handle_save_config_wrapper},
    {"session default", "Sets current or specified UUID as default", cmd_session_default},
    {"session delete", "Deletes a session from the database", cmd_session_delete},
    {"session description", "Sets a desctiption to current session",  cmd_session_description},
    {"session list", "Lists sessions stored in database", cmd_session_list},
    {"session load", "Loads a previous session from database", cmd_session_load},
    {"session manager", "Opens the session manager window", cmd_session_manager_wrapper},
    {"session new", "Starts a new AI session", cmd_session_new},
    {"session no default", "Sets no session tto default", cmd_session_no_default},
    {"session read from global", "Toggle reading from global (on/off)", cmd_session_read_from_gloal_toggle},
    {"session show", "shows the current session uuid and description", cmd_session_show},
    {"session write to global", "Toggle writing to global (on/off)", cmd_session_write_to_global_toggle},
    {"smart cache", "Smart Cache Toggle (on/off)", cmd_toggle_smart_cache},
    {"provider", "Display AI provider [OpenAI/Gemini]", handle_provider_wrapper},
    {"ratelimit", "Toggle Raate Limiting (on/off)", cmd_toggle_ratelimit},
    {"reset db", "Reset database connection", cmd_reset_db_connect},
    {"reset state", "Reeset current AI state back to ready", handle_reset_state_wrapper},
    {"rpm", "Set ratelimit Requests Per Minute", cmd_set_rpm},
    {"status", "Display operational metrics", handle_status_wrapper},
    {"tee", "Toggle immediate terminal capturing (on/off)", cmd_toggle_tee},
    {"version", "Display running aiterm version, build ID, and build time", handle_version_wrapper},
    {NULL, NULL, NULL} // Sentinel
};

gboolean execute_command(AppContext *app, const char *input) {
    // Trim optional leading slash
    const char *cmd = (input[0] == '/') ? input + 1 : input;
    for (int i = 0; registry[i].name != NULL; i++) {
        // Use strncasecmp for case-insensitive command matching
        if (strncasecmp(cmd, registry[i].name, strlen(registry[i].name)) == 0) {
            // Found it! Trigger the handler
            registry[i].handler(app, cmd + strlen(registry[i].name));
            return TRUE; // Command handled, signal AI thread to skip
        }
    }
    return FALSE; // No command found, pass to AI
}

void display_dynamic_help(AppContext *app) {
    // 1. First, find the longest command name
    int max_len = 0;
    int current_len=0;
    for (int i = 0; registry[i].name != NULL; i++) {
        int len = strlen(registry[i].name);
        if (len > max_len) max_len = len;
    }

    // 2. Build the format string dynamically (e.g., "%-25s")
    //char format[32];
    //snprintf(format, sizeof(format), "%%-%ds : %%s\n", max_len + 2);

    GString *help = g_string_new("--- Available Commands ---\n");
    for (int i = 0; registry[i].name != NULL; i++) {
        current_len = strlen(registry[i].name);
        g_string_append_printf(help, registry[i].name);
       while(current_len < max_len+2) {
           current_len++;
           g_string_append_printf(help, ".");
       }
       g_string_append_printf(help, ": %s\n", registry[i].description);
       current_len=0;
    }

    write_to_ai_pane(app, "Help: ", help->str, "ai_tag", "body_tag");
    g_string_free(help, TRUE);
}

gboolean ui_display_list(gpointer data) {
    SessionListResult *res = (SessionListResult *)data;
    GList *iter = res->entries;
    while (iter != NULL) {
        SessionEntry *e = (SessionEntry *)iter->data;
        char *line = g_strdup_printf("%s : %s\n", e->uuid, e->description);
        write_to_ai_pane(global_app, "System: ", line, "ai_tag", "body_tag");
        g_free(line);
        // Clean up individual entry
        g_free(e->uuid);
        g_free(e->description);
        g_free(e);
        iter = iter->next;
    }
    g_list_free(res->entries);
    g_free(res);
    return FALSE; // Remove idle source
}

gboolean ui_display_show(gpointer data) {
    SessionShowResult *res = (SessionShowResult *)data;

    // 1. Prepare the content string
    char *desc = (res->description && strlen(res->description) > 0 && strcmp(res->description, "NULL") != 0) 
                 ? res->description
                 : "[No description set]";

    char *msg = g_strdup_printf("Session UUID: %s\nDescription: %s\n", res->uuid, desc);

    // 2. Render to the AI pane using the standard wrapper
    // This applies the "System: " prefix consistently
    write_to_ai_pane(global_app, "System: ", msg, "ai_tag", "body_tag");

    // 3. Cleanup
    g_free(msg);
    g_free(res->uuid);
    g_free(res->description);
    g_free(res);

    return FALSE; // Remove idle source
}


void cmd_reset_db_connect(AppContext *app, const char *args) {
    write_to_ai_pane_wrapper(app, "Attempting to reset Global Database connection.");
    if (app->global_db_conn) {
        mysql_close(app->global_db_conn);
    }

    pthread_mutex_init(&app->db_mutex, NULL);
    pthread_t db_init_thread;
    DEBUG_PRINT("[DEBUG]: [MAIN] Spawning asynchronous DB initialization thread...\n");
    if (pthread_create(&db_init_thread, NULL, init_db_thread_worker, app) == 0) {
        pthread_detach(db_init_thread); // Allow thread to clean itself up on exit
    } else {
        fprintf(stderr, "Error: Failed to spawn database initialization thread.\n");
        return;
    }
    if (app->global_db_conn) {
        write_to_ai_pane_wrapper(app, "Asynchronous DB thread re-established!");
    }
}

// command helper wrappers
void handle_help_wrapper(AppContext *app, const char *args) {
    write_to_ai_pane(app, "System: ", "==== Available Functions ====", "system_tag", "body_tag");
    display_dynamic_help(app);
}

void handle_status_wrapper(AppContext *app, const char *args) {
    write_to_ai_pane(app, "System: ", "==== System status ====", "system_tag", "body_tag");
    display_status(app);
}

void handle_features_wrapper(AppContext *app, const char *args) {
    write_to_ai_pane(app, "System: ", "==== System features ====", "system_tag", "body_tag");
   // 1. If get_features() prints to stdout, we need to redirect it or capture it.
    // Better: Refactor get_features() to return a GString or char* instead of printing.

    char *feature_text = g_strdup(get_features_text()); // You should create this!

    if (feature_text) {
        write_to_ai_pane(app, "Features: ", feature_text, "system_tag", "body_tag");
        g_free(feature_text);
    } else {
        write_to_ai_pane(app, "Error: ", "Could not retrieve features.", "error_tag", "body_tag");
    }
}

void handle_clear_wrapper(AppContext *app, const char *args) {

    GtkTextBuffer *buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(app->gemini_view));
    gtk_text_buffer_set_text(buf, "", -1);
}

void handle_provider_wrapper(AppContext *app, const char *args) {
    write_to_ai_pane(app, "System: ", "==== System provider ====", "system_tag", "body_tag");
    char info[512];
    snprintf(info, sizeof(info), "Provider: %s\nModel: %s", app->provider, app->model);
    write_to_ai_pane(app, "System: ", info, "cmd_tag", "cmd_tag");
}

void handle_list_models_wrapper(AppContext *app, const char *args) {
    write_to_ai_pane(app, "System: ", gemini_list_models(app), "system_tag", "ai_tag");
    g_idle_add((GSourceFunc)scroll_ai_pane_to_bottom, app);
}

void handle_reset_state_wrapper(AppContext *app, const char *args) {
    write_to_ai_pane(app, "AI State:", "Reset processing state", "system_tag", "ai_tag");
    if (app->is_processing) {
        app->is_processing = FALSE;
        DEBUG_PRINT("[DEBUG]: RESET_STATE: cleared is_processing flag\n");
    }
    if (app->ai_busy) {
        app->ai_busy = FALSE;
        DEBUG_PRINT("[DEBUG]: RESET_STATE: cleared ai_busy flag\n");
    }
}

void handle_hw_wrapper(AppContext *app, const char *args) {
    write_to_ai_pane(app, "[ System Hardware ]\n", get_hw_stats(), "cmd_tag", "ai_tag");
}

void handle_history_wrapper(AppContext *app, const char *args) {
    display_all_history(app);
}

void handle_version_wrapper(AppContext *app, const char *args) {
    write_to_ai_pane(app, "System: ", get_version_info(), "cmd_tag", "cmd_tag");
}

void handle_load_config_wrapper(AppContext *app, const char *args) {
    write_to_ai_pane(app, "System: ", "Loaded settings from configuration file", "system_tag", "ai_tag");
    load_config(app);
    init_provider_config(app);
}

void handle_save_config_wrapper(AppContext *app, const char *args) {
    write_to_ai_pane(app, "System: ", "Saved settings to configuration file", "system_tag", "ai_tag");
    save_config(app);
}

void cmd_session_new(AppContext *app, const char *args) {
    SessionThreadData *std = g_malloc0(sizeof(SessionThreadData));
    std->app = app;
    std->type = CMD_SESSION_NEW;
    std->arg = g_strdup(args ? args : "No description"); // Use the user's description
    g_thread_new("session_new_worker", (GThreadFunc)session_db_worker, std);
}

void cmd_session_list(AppContext *app, const char *args) {
    SessionThreadData *std = g_malloc0(sizeof(SessionThreadData));
    std->app = app;
    std->type = CMD_SESSION_LIST;
    // No args needed for list
    g_thread_new("session_list_worker", (GThreadFunc)session_db_worker, std);
}

void cmd_session_show(AppContext *app, const char *args) {
    SessionThreadData *std = g_malloc0(sizeof(SessionThreadData));
    std->app = app;
    std->type = CMD_SESSION_SHOW;
    // We pass args as NULL since show doesn't require input,
    // but we ensure the struct pointer is safe.
    std->arg = NULL;

    g_thread_new("session_show_worker", (GThreadFunc)session_db_worker, std);
}

void cmd_session_description(AppContext *app, const char *args) {
    const char *ptr = args;
    if (ptr && *ptr == ' ') {
        ptr++;
    }

    if (!ptr || strlen(ptr) == 0) {
        write_to_ai_pane_wrapper(app, ": Error: Provide a description.");
        return;
    }
    // std->arg needs to be the text provided after "session description"
    SessionThreadData *std = g_malloc0(sizeof(SessionThreadData));
    std->app = app;
    std->type = CMD_SESSION_DESCRIPTION;
    std->arg = g_strdup(ptr);
    g_thread_new("session_desc_worker", (GThreadFunc)session_db_worker, std);
}

void cmd_session_load(AppContext *app, const char *args) {
    const char *ptr = args;
    if (ptr && *ptr == ' ') {
        ptr++;
    }

    if (!ptr || strlen(ptr) < 36) {
        write_to_ai_pane_wrapper(app, ": Error: Invalid UUID.");
        return;
    }

    SessionThreadData *std = g_malloc0(sizeof(SessionThreadData));
    std->app = app;
    std->type = CMD_SESSION_LOAD;
    std->arg = g_strdup(ptr); // The UUID to load
    g_thread_new("session_load_worker", (GThreadFunc)session_db_worker, std);
}

void cmd_session_delete(AppContext *app, const char *args) {
    // 1. Skip the leading space
    const char *ptr = args;
    if (ptr && *ptr == ' ') {
        ptr++;
    }

    // 2. Validate that a UUID was provided (36 characters)
    if (!ptr || strlen(ptr) < 36) {
        write_to_ai_pane_wrapper(app, ": Error: You must provide a valid UUID to delete.");
        return;
    }

    // 3. Prevent deleting the currently active session
    if (strcmp(ptr, app->session.session_uuid) == 0) {
        write_to_ai_pane_wrapper(app, ": Error: Cannot delete the currently active session.");
        return;
    }

    // 4. Create the worker data
    SessionThreadData *std = g_malloc0(sizeof(SessionThreadData));
    std->app = app;
    std->type = CMD_SESSION_DELETE;
    std->arg = g_strdup(ptr); // Copy the UUID

    g_thread_new("session_delete_worker", (GThreadFunc)session_db_worker, std);
}

void cmd_session_default(AppContext *app, const char *args) {
    const char *ptr = args;
    // Skip leading space if it exists
    if (ptr && *ptr == ' ') {
        ptr++;
    }

    SessionThreadData *std = g_malloc0(sizeof(SessionThreadData));
    std->app = app;
    std->type = CMD_SESSION_DEFAULT;

    // Case 1: No args, use current session
    if (!ptr || strlen(ptr) == 0) {
        std->arg = g_strdup(app->session.session_uuid);
    }
    // Case 2: Provide specific UUID (36 chars)
    else if (strlen(ptr) >= 36) {
        std->arg = g_strdup(ptr);
    }
    else {
        write_to_ai_pane_wrapper(app, ": Error, invalid UUID.");
        g_free(std);
        return;
    }

    g_thread_new("session_default_worker", (GThreadFunc)session_db_worker, std);
}

void cmd_session_no_default(AppContext *app, const char *args) {
    SessionThreadData *std = g_malloc0(sizeof(SessionThreadData));
    std->app = app;
    std->type = CMD_SESSION_NO_DEFAULT;
    // No arg needed
    g_thread_new("session_no_default_worker", (GThreadFunc)session_db_worker, std);
}

void cmd_session_manager_wrapper(AppContext *app, const char *args) {
    open_session_manager_window(app);
}

void cmd_set_rpm(AppContext *app, const char *args) {
    const char *ptr = args;

    if (ptr && *ptr == ' ') {
        ptr++;
    }

    if (!ptr || strlen(ptr) == 0) {
        write_to_ai_pane_wrapper(app,": Required parameter missing: ON or OFF");
        return;
    }
    app->limiter.requests_per_minute = atoi(ptr);
    DEBUG_PRINT("[DEBUG]: Ratelimit: Requests Per Minute set %s\n", ptr);

    ratelimit_init(&app->limiter, app->limiter.requests_per_minute);

}

// ================= Start of Toggle ON / OFF functions  ======================

void cmd_toggle_auto_all(AppContext *app, const char *args) {
    const char *ptr = args;
    gboolean state;

    if (ptr && *ptr == ' ') {
        ptr++;
    }

    if (!ptr || strlen(ptr) == 0) {
        write_to_ai_pane_wrapper(app,": Required parameter missing: ON or OFF");
        return;
    }

    if (strcmp(ptr, "on") == 0) {
	state = TRUE;
    } else if (strcmp(ptr, "off") == 0) {
        state = FALSE;
    } else {
        GString *msg = g_string_new(": Unknown parameter parsed: ");
        g_string_append_printf(msg, "%s ", ptr);
        write_to_ai_pane_wrapper(app, msg->str);
        return;
    }
    //gboolean state = (strstr(ptr, "on") != NULL);
    app->tee_enabled = state;
    app->autoreply_enabled = state;
    app->auto_execute_enabled = state;
    write_to_ai_pane_wrapper(app, app->tee_enabled ?          ": Tee Collection Enabled" : ": Tee Collection Disabled");
    write_to_ai_pane_wrapper(app, app->autoreply_enabled ?    ": Auto Reply Enabled"     : ": Auto Reply Disabled");
    write_to_ai_pane_wrapper(app, app->auto_execute_enabled ? ": Auto Execute Enabled"   : ": Auto Execute Disabled");
}

void cmd_session_read_from_gloal_toggle(AppContext *app, const char *args) {
    const char *ptr = args;
    gboolean state = FALSE;

    if (ptr && *ptr == ' ') {
        ptr++;
    }

    if (!ptr || strlen(ptr) == 0) {
        write_to_ai_pane_wrapper(app,": Required parameter missing: ON or OFF");
        return;
    }

    if (strcmp(ptr, "on") == 0) {
        state = TRUE;
    } else if (strcmp(ptr, "off") == 0) {
        state = FALSE;
    } else {
        GString *msg = g_string_new(": Unknown parameter parsed: ");
        g_string_append_printf(msg, "%s ", ptr);
        write_to_ai_pane_wrapper(app, msg->str);
        return;
    }

    app->session.read_from_global = state;
    write_to_ai_pane_wrapper(app, state ? ": Reading from GLOBAL history." : ": Reading STRICT history.");
}

void cmd_session_write_to_global_toggle(AppContext *app, const char *args) {
    const char *ptr = args;
    gboolean state = FALSE;

    if (ptr && *ptr == ' ') {
        ptr++;
    }

    if (!ptr || strlen(ptr) == 0) {
        write_to_ai_pane_wrapper(app,": Required parameter missing: ON or OFF");
        return;
    }

    if (strcmp(ptr, "on") == 0) {
        state = TRUE;
    } else if (strcmp(ptr, "off") == 0) {
        state = FALSE;
    } else {
        GString *msg = g_string_new(": Unknown parameter parsed: ");
        g_string_append_printf(msg, "%s ", ptr);
        write_to_ai_pane_wrapper(app, msg->str);
        return;
    }

    app->session.write_to_global = state;
    write_to_ai_pane_wrapper(app, state ? ": Writing to GLOBAL session." : ": Writing to STRICT session.");
}

void cmd_toggle_tee(AppContext *app, const char *args) {
    const char *ptr = args;
    gboolean state = FALSE;

    if (ptr && *ptr == ' ') {
        ptr++;
    }

    if (!ptr || strlen(ptr) == 0) {
        write_to_ai_pane_wrapper(app,": Required parameter missing: ON or OFF");
        return;
    }

    if (strcmp(ptr, "on") == 0) {
        state = TRUE;
    } else if (strcmp(ptr, "off") == 0) {
        state = FALSE;
    } else {
        GString *msg = g_string_new(": Unknown parameter parsed: ");
        g_string_append_printf(msg, "%s ", ptr);
        write_to_ai_pane_wrapper(app, msg->str);
        return;
    }

    app->tee_enabled = state;
    write_to_ai_pane_wrapper(app, state ? ": Tee Collection Enabled" : ": Tee Collection Disabled");
}

void cmd_toggle_autoreply(AppContext *app, const char *args) {
    const char *ptr = args;
    gboolean state = FALSE;

    if (ptr && *ptr == ' ') {
        ptr++;
    }

    if (!ptr || strlen(ptr) == 0) {
        write_to_ai_pane_wrapper(app,": Required parameter missing: ON or OFF");
        return;
    }

    if (strcmp(ptr, "on") == 0) {
        state = TRUE;
    } else if (strcmp(ptr, "off") == 0) {
        state = FALSE;
    } else {
        GString *msg = g_string_new(": Unknown parameter parsed: ");
        g_string_append_printf(msg, "%s ", ptr);
        write_to_ai_pane_wrapper(app, msg->str);
        return;
    }

    app->autoreply_enabled = state;
    write_to_ai_pane_wrapper(app, state ? ": Auto Reply Enabled" : ": Auto Reply Disabled");
}

void cmd_toggle_autoexe(AppContext *app, const char *args) {
    const char *ptr = args;
    gboolean state = FALSE;

    if (ptr && *ptr == ' ') {
        ptr++;
    }

    if (!ptr || strlen(ptr) == 0) {
        write_to_ai_pane_wrapper(app,": Required parameter missing: ON or OFF");
        return;
    }

    if (strcmp(ptr, "on") == 0) {
        state = TRUE;
    } else if (strcmp(ptr, "off") == 0) {
        state = FALSE;
    } else {
        GString *msg = g_string_new(": Unknown parameter parsed: ");
        g_string_append_printf(msg, "%s ", ptr);
        write_to_ai_pane_wrapper(app, msg->str);
        return;
    }

    app->auto_execute_enabled = state;
    write_to_ai_pane_wrapper(app, state ? ": Auto Execute Enabled" : ": Auto Execute Disabled");
}

void cmd_toggle_ratelimit(AppContext *app, const char *args) {
    const char *ptr = args;
    gboolean state = FALSE;

    if (ptr && *ptr == ' ') {
        ptr++;
    }

    if (!ptr || strlen(ptr) == 0) {
        write_to_ai_pane_wrapper(app,": Required parameter missing: ON or OFF");
        return;
    }

    if (strcmp(ptr, "on") == 0) {
        state = TRUE;
    } else if (strcmp(ptr, "off") == 0) {
        state = FALSE;
    } else {
        GString *msg = g_string_new(": Unknown parameter parsed: ");
        g_string_append_printf(msg, "%s ", ptr);
        write_to_ai_pane_wrapper(app, msg->str);
        return;
    }

    app->ratelimit_enabled = state;
    write_to_ai_pane_wrapper(app, state ? ": Rate Limiting Enabled." : ": Rate Limiting Disabled.");
}

void cmd_toggle_smart_cache(AppContext *app, const char *args) {
    const char *ptr = args;
    gboolean state = FALSE;

    if (ptr && *ptr == ' ') {
        ptr++;
    }

    if (!ptr || strlen(ptr) == 0) {
        write_to_ai_pane_wrapper(app,": Required parameter missing: ON or OFF");
        return;
    }

    if (strcmp(ptr, "on") == 0) {
        state = TRUE;
    } else if (strcmp(ptr, "off") == 0) {
        state = FALSE;
    } else {
        GString *msg = g_string_new(": Unknown parameter parsed: ");
        g_string_append_printf(msg, "%s ", ptr);
        write_to_ai_pane_wrapper(app, msg->str);
        return;
    }

    app->smart_cache_enabled = state;
    write_to_ai_pane_wrapper(app, state ? ": Smart Cache Enabled." : ": Smart Cache Disabled.");
}

void cmd_toggle_noise_filter(AppContext *app, const char *args) {
    const char *ptr = args;
    gboolean state = FALSE;

    if (ptr && *ptr == ' ') {
        ptr++;
    }

    if (!ptr || strlen(ptr) == 0) {
        write_to_ai_pane_wrapper(app,": required parameter missing: ON or OFF");
        return;
    }

    if (strcmp(ptr, "on") == 0) {
        state = TRUE;
    } else if (strcmp(ptr, "off") == 0) {
        state = FALSE;
    } else {
        GString *msg = g_string_new(": Unknown parameter parsed: ");
        g_string_append_printf(msg, "%s ", ptr);
        write_to_ai_pane_wrapper(app, msg->str);
        return;
    }

    app->noise_filter_enabled = state;
    write_to_ai_pane_wrapper(app, state ? ": Noise Block Enabled." : ": Noise Block Disabled.");
}

// ================= End of Toggle ON / OFF functions  ======================

void handle_extended_help(AppContext *app, const char *args) {
    write_to_ai_pane(app, "[ Extended Help System ]\n", get_help_text(), "cmd_tag", "cmd_tag");
}

void cmd_noise_add(AppContext *app, const char *args) {
    const char *ptr = args;

    // Skip the leading separator space if it exists
    if (ptr && *ptr == ' ') {
        ptr++;
    }

    if (!ptr || strlen(ptr) == 0) {
        write_to_ai_pane_wrapper(app, ": Error: You must provide a text pattern to filter.");
        return;
    }

    // Invoke our database/cache entry function
    noise_filter_add(app, ptr);

    char *msg = g_strdup_printf(": Success: Pattern '%s' added to noise filters.", ptr);
    write_to_ai_pane_wrapper(app, msg);
    g_free(msg);
}

void cmd_noise_list(AppContext *app, const char *args) {
    // We ignore 'args' because listing doesn't require extra parameters
    noise_filter_list(app);
}

void cmd_noise_delete(AppContext *app, const char *args) {
    // Placeholder stub until announced
    write_to_ai_pane_wrapper(app, ": System: 'noise delete' function is to be announced as of yet.");
}
