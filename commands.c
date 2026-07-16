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
#include "gemini_cache.h"
#include "session_manager.h"
#include "session_manager_gui.h"
#include "history_manager_gui.h"
#include "policy_manager_gui.h"
#include "noise_filter_manager_gui.h"
#include "crypto.h"
#include "noisefilter.h"
#include "status.h"
#include "config.h"
#include "menu.h"

extern const char* HIGHLIGHT_STRING;

// Parse command line options and handle early-exit CLI queries
void parse_command_line_options(AppContext *app, int argc, char *argv[]) {
    char *env_key = getenv("AITERM_MASTER_KEY");

    if (env_key) {
        app->security.master_key = strdup(env_key);
    }

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--debug") == 0) {
            app->sys.debug_mode = TRUE;
            app->sys.debug_mode_override = TRUE;
        } else if (strcmp(argv[i], "--version") == 0) {
            print_version();
            exit(0);
        } else if (strcmp(argv[i], "--list-models") == 0) {
            load_config(app);
            if (!app->security.master_key) {
                printf("Error: no master key found!\n");
                exit(1);
            }
            char *models = gemini_list_models(app);
            printf("Gemini Model List:\n%s\n", models);
            if (app->security.master_key) {
                // Overwrite memory with zeros before freeing
                size_t len = strlen(app->security.master_key);
                memset(app->security.master_key, 0, len);
                free(app->security.master_key);
            }
            free_provider_config(&app->provider_config);
            g_free(app);
            exit(0);
        } else if (strcmp(argv[i], "--provider") == 0) {
            load_config(app);
            char info[512];
            snprintf(info, sizeof(info), "Provider: %s\nModel: %s", app->provider_config.provider, app->aiterm_runtime.model);
            printf("%s\n", info);
            exit(0);
        } else if (strcmp(argv[i], "--features") == 0) {
            printf("%s\n", get_features_text());
            exit(0);
        } else if (strcmp(argv[i], "--help") == 0) {
            printf("%s\n%s\n", get_cmd_help(), HIGHLIGHT_STRING);
            exit(0);
        } else if (strncmp(argv[i], "--master=", 9) == 0) {
            app->security.master_key = strdup(argv[i] + 9);
        } else if (strncmp(argv[i], "--crypt-pw=", 11) == 0) {
            if (!app->security.master_key) {
                fprintf(stderr, "Error: You must provide a master key (via --master or AITERM_MASTER_KEY) before encrypting.\n");
        	exit(1);
    	    }
            char *plaintext = argv[i] + 11;
            char *encrypted = crypt_to_hex(plaintext, app->security.master_key);
            if (encrypted) {
        	printf("Encrypted string: %s\n", encrypted);
        	free(encrypted);
            } else {
        	fprintf(stderr, "Error: Encryption failed.\n");
            }
            exit(0);
       }
    }
    if (!app->security.master_key) {
	char *pwd = getpass("Enter Master Encryption Key: ");
	if (pwd) app->security.master_key = strdup(pwd);
    }
}

static CommandRegistry registry[] = {
    {"auto all", "Toggle (on/off): Tee, AutoReply, & AutoExecute", cmd_toggle_auto_all},
    {"autoreply", "Toggle real-time prompt analysis (on/off)", cmd_toggle_autoreply},
    {"autoexe", "Toggle execution of AI payloads (on/off)", cmd_toggle_autoexe},
    {"clear", "Clear the contents of the AI pane", handle_clear_wrapper},
    {"close history manager", "Closes the history manager window", cmd_close_history_manager_wrapper},
    {"close noise manager", "Closes the noise filter manager window", cmd_close_noise_manager_wrapper},
    {"close policy manager", "Closes the policy manager window", cmd_close_policy_manager_wrapper},
    {"close session manager", "Closes the session manager window", cmd_close_session_manager_wrapper},
    {"command line help", "display all command line options", cmd_show_command_line_help_wrapper},
    {"debug", "Toggle debug mode out stderr (on/off)", cmd_toggle_debug},
    {"extended help", "Display extended help message", handle_extended_help},
    {"features", "Display new aiterm features", handle_features_wrapper},
    {"help", "Display this dynamic help menu", handle_help_wrapper},
    {"history", "Display mysql history", handle_history_wrapper},
    {"hw", "Display various hardware info", handle_hw_wrapper},
    {"invalidate cache", "Force a reload of smart cache", cmd_invalidate_cache},
    {"list models", "Display all models from current provider", handle_list_models_wrapper},
    {"load config", "Loads configurations from aiterm.conf", handle_load_config_wrapper},
    {"noise filter", "Toggle noise filter (on/off)", cmd_toggle_noise_filter},
    {"noise add", "Add a pattern to the noise filter list", cmd_noise_add},
    {"noise delete", "Remove a pattern from the noise filter (TBA)", cmd_noise_delete},
    {"noise list", "List all active noise filter patterns (TBA)", cmd_noise_list},
    {"noise reload", "Reload noise filters from database", cmd_noisefilter_reload_wrapper},
    {"open history manager", "Open History Manager window", cmd_history_manager_wrapper},
    {"open noise manager", "Opens Noise Filter Manager window", cmd_noisefilter_manager_wrapper},
    {"open policy manager", "Open Policy Manager window", cmd_policy_manager_wrapper},
    {"open session manager", "Opens the session manager window", cmd_session_manager_wrapper},
    {"save config", "Saves configuration to aiterm.conf", handle_save_config_wrapper},
    {"session default", "Sets current or specified UUID as default", cmd_session_default},
    {"session delete", "Deletes a session from the database", cmd_session_delete},
    {"session description", "Sets a desctiption to current session",  cmd_session_description},
    {"session list", "Lists sessions stored in database", cmd_session_list},
    {"session load", "Loads a previous session from database", cmd_session_load},
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
    {"xml tagging", "Toggle XML Tagging of AI payloads (on/off)", cmd_toggle_xml_tagging},
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

void cmd_invalidate_cache(AppContext *app, const char *args) {
    if (!app->sys.smart_cache_enabled) {
        write_to_ai_pane(app, "System: ", "Smart cache not enabled", "ai_tag", "cmd_tag");
        return;
    }

    // Force smartcace to be reloaded
    gemini_cache_invalidate(app);
    if (!app->gemini_cache.id) {
        write_to_ai_pane(app, "System: ", "Forced cache reload", "ai_tag", "cmd_tag");
    } else {
        write_to_ai_pane(app, "System: ", "Failed to force cache reload", "ai_tag", "cmd_tag");
    }
}

void cmd_show_command_line_help_wrapper(AppContext *app, const char *args) {
    write_to_ai_pane(app, "AITerm Command Line Help: ", get_cmd_help(), "ai_tag", "cmd_tag");
}

void cmd_reset_db_connect(AppContext *app, const char *args) {
    write_to_ai_pane_wrapper(app, "Attempting to reset Global Database connection.");
    if (app->database.global_db_conn) {
        mysql_close(app->database.global_db_conn);
    }

    pthread_mutex_init(&app->access.db_mutex, NULL);
    pthread_t db_init_thread;
    DEBUG_PRINT("[DEBUG]: [MAIN] Spawning asynchronous DB initialization thread...\n");
    if (pthread_create(&db_init_thread, NULL, init_db_thread_worker, app) == 0) {
        pthread_detach(db_init_thread); // Allow thread to clean itself up on exit
    } else {
        fprintf(stderr, "Error: Failed to spawn database initialization thread.\n");
        return;
    }
    if (app->database.global_db_conn) {
        write_to_ai_pane_wrapper(app, "Asynchronous DB thread re-established!");
    }
    gemini_cache_invalidate(app);
}

void cmd_close_policy_manager_wrapper(AppContext *app, const char *args) {
    if(app->manager.policy != NULL) {
        write_to_ai_pane(app, "System: ", "Closing Policy Manager window", "ai_tag", "cmd_tag");
        close_policy_manager(app);
    } else {
        write_to_ai_pane(app, "System: ", "Policy Manager window is not open", "ai_tag", "cmd_tag");
    }
}

void cmd_close_history_manager_wrapper(AppContext *app, const char *args) {
    if(app->manager.history != NULL) {
        write_to_ai_pane(app, "System: ", "Closing History Manager window", "ai_tag", "cmd_tag");
        close_history_manager(app);
    } else {
        write_to_ai_pane(app, "System: ", "history Manager window is not open", "ai_tag", "cmd_tag");
    }
}

void cmd_close_session_manager_wrapper(AppContext *app, const char *args) {
    if(app->manager.session != NULL) {
        write_to_ai_pane(app, "System: ", "Closing Session Manager window", "ai_tag", "cmd_tag");
        close_session_manager(app);
    } else {
        write_to_ai_pane(app, "System: ", "Session Manager window is not open", "ai_tag", "cmd_tag");
    }
}

void cmd_close_noise_manager_wrapper(AppContext *app, const char *args) {
    if(app->manager.noise != NULL) {
        write_to_ai_pane(app, "System: ", "Closing noise Manager window", "ai_tag", "cmd_tag");
        close_noise_manager(app);
    } else {
        write_to_ai_pane(app, "System: ", "noise Manager window is not open", "ai_tag", "cmd_tag");
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

    GtkTextBuffer *buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(app->gui.gemini_view));
    gtk_text_buffer_set_text(buf, "", -1);
}

void handle_provider_wrapper(AppContext *app, const char *args) {
    write_to_ai_pane(app, "System: ", "==== System provider ====", "system_tag", "body_tag");
    char info[512];
    snprintf(info, sizeof(info), "Provider: %s\nModel: %s", app->provider_config.provider, app->aiterm_runtime.model);
    write_to_ai_pane(app, "System: ", info, "cmd_tag", "cmd_tag");
}

void handle_list_models_wrapper(AppContext *app, const char *args) {
    write_to_ai_pane(app, "System: ", gemini_list_models(app), "system_tag", "ai_tag");
    g_idle_add((GSourceFunc)scroll_ai_pane_to_bottom, app);
}

void handle_reset_state_wrapper(AppContext *app, const char *args) {
    write_to_ai_pane(app, "AI State:", "Reset processing state", "system_tag", "ai_tag");
    if (app->sys.is_processing) {
        app->sys.is_processing = FALSE;
        DEBUG_PRINT("[DEBUG]: RESET_STATE: cleared is_processing flag\n");
    }
    if (app->sys.ai_busy) {
        app->sys.ai_busy = FALSE;
        DEBUG_PRINT("[DEBUG]: RESET_STATE: cleared ai_busy flag\n");
    }
    gemini_cache_invalidate(app);
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
    gemini_cache_invalidate(app);
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
    gemini_cache_invalidate(app);
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
    gemini_cache_invalidate(app);
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
    write_to_ai_pane(app, "System: ", "No default session set", "ai_tag", "cmd_tag");
}

void cmd_session_manager_wrapper(AppContext *app, const char *args) {
    DEBUG_PRINT("[DEBUG]: [Commands]: Opening Session Manager Window\n");
    write_to_ai_pane(app, "System: ", "Opening session manager window", "ai_tag", "cmd_tag");
    open_session_manager_window(app);
}

void cmd_history_manager_wrapper(AppContext *app, const char *args) {
    DEBUG_PRINT("[DEBUG]: [Commands]: Opening History Manager Window\n");
    write_to_ai_pane(app, "System: ", "Opening history manager window", "ai_tag", "cmd_tag");
    open_history_manager_window(app);
}

void cmd_noisefilter_manager_wrapper(AppContext *app, const char *args) {
    DEBUG_PRINT("[DEBUG]: [Commands]: Opening Noise Filter Manager Window\n");
    noise_filter_load_from_db(app);
    write_to_ai_pane(app, "System: ", "Opening noise filter manager window", "ai_tag", "cmd_tag");
    open_noise_filter_manager_window(app);
}

void cmd_policy_manager_wrapper(AppContext *app, const char *args) {
    DEBUG_PRINT("[DEBUG]: [Commands]: Opening Policy Manager Window\n");
    write_to_ai_pane(app, "System: ", "Opening policy manager window", "ai_tag", "cmd_tag");
    open_policy_manager_window(app);
}

void cmd_set_rpm(AppContext *app, const char *args) {
    const char *ptr = args;

    // 1. Skip any initial spaces after the command name (e.g., "/rpm 6" or "/rpm =6")
    while (ptr && *ptr == ' ') {
        ptr++;
    }

    // 2. If an equals sign is present, skip past it (e.g., "/rpm=6" or "/rpm =6")
    if (ptr && *ptr == '=') {
        ptr++;
    }

    // 3. Skip any spaces after the equals sign just in case (e.g., "/rpm = 6")
    while (ptr && *ptr == ' ') {
        ptr++;
    }

    // 4. Validation guard: Ensure we actually have characters left to parse
    if (!ptr || strlen(ptr) == 0 || !isdigit((unsigned char)*ptr)) {
        write_to_ai_pane_wrapper(app, ": Error: Required parameter missing or invalid. Provide a numeric RPM value.");
        return;
    }

    // 5. Safely convert the sanitized string to an integer
    app->limiter.requests_per_minute = atoi(ptr);
    DEBUG_PRINT("[DEBUG]: Ratelimit: Requests Per Minute set to %d (Parsed from raw input)\n", 
                app->limiter.requests_per_minute);

    // Re-initialize the rate limiter state with the newly validated RPM
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
    app->sys.tee_enabled = state;
    app->sys.autoreply_enabled = state;
    app->sys.auto_execute_enabled = state;
    write_to_ai_pane_wrapper(app, app->sys.tee_enabled ?          ": Tee Collection Enabled" : ": Tee Collection Disabled");
    write_to_ai_pane_wrapper(app, app->sys.autoreply_enabled ?    ": Auto Reply Enabled"     : ": Auto Reply Disabled");
    write_to_ai_pane_wrapper(app, app->sys.auto_execute_enabled ? ": Auto Execute Enabled"   : ": Auto Execute Disabled");
    sync_toggle_ui_elements(app);
}

void cmd_toggle_debug(AppContext *app, const char *args) {
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
    app->sys.debug_mode = state;
    write_to_ai_pane_wrapper(app, app->sys.debug_mode ? ": Debug Mode Enabled" : ": Debug Mode Disabled");
    sync_toggle_ui_elements(app);
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
    sync_toggle_ui_elements(app);
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
    sync_toggle_ui_elements(app);
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

    app->sys.tee_enabled = state;
    write_to_ai_pane_wrapper(app, state ? ": Tee Collection Enabled" : ": Tee Collection Disabled");
    sync_toggle_ui_elements(app);
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

    app->sys.autoreply_enabled = state;
    write_to_ai_pane_wrapper(app, state ? ": Auto Reply Enabled" : ": Auto Reply Disabled");
    sync_toggle_ui_elements(app);
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

    app->sys.auto_execute_enabled = state;
    write_to_ai_pane_wrapper(app, state ? ": Auto Execute Enabled" : ": Auto Execute Disabled");
    sync_toggle_ui_elements(app);
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

    app->sys.ratelimit_enabled = state;
    write_to_ai_pane_wrapper(app, state ? ": Rate Limiting Enabled." : ": Rate Limiting Disabled.");
    sync_toggle_ui_elements(app);
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

    app->sys.smart_cache_enabled = state;
    write_to_ai_pane_wrapper(app, state ? ": Smart Cache Enabled." : ": Smart Cache Disabled.");
    sync_toggle_ui_elements(app);
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

    app->sys.noise_filter_enabled = state;
    write_to_ai_pane_wrapper(app, state ? ": Noise Block Enabled." : ": Noise Block Disabled.");
    sync_toggle_ui_elements(app);
    noise_filter_load_from_db(app);
}

void cmd_toggle_xml_tagging(AppContext *app, const char *args) {
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

    app->xml.tagging_enabled = state;
    write_to_ai_pane_wrapper(app, state ? ": XML Payload Tagging Enabled." : ": XML Payload Tagging Disabled.");
    sync_toggle_ui_elements(app);
    noise_filter_load_from_db(app);
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
    noise_filter_load_from_db(app);
    g_free(msg);
}

void cmd_noise_list(AppContext *app, const char *args) {
    // We ignore 'args' because listing doesn't require extra parameters
    noise_filter_list(app);
}

void cmd_noise_delete(AppContext *app, const char *args) {
    // Placeholder stub until announced
    write_to_ai_pane_wrapper(app, ": System: 'noise delete' function is to be announced as of yet.");
    noise_filter_load_from_db(app);
}

void cmd_noisefilter_reload_wrapper(AppContext *app, const char *args) {
    noise_filter_load_from_db(app);
    write_to_ai_pane(app, "System: ", "Reloaded noise filters from database into running cache", "ai_tag", "cmd_tag");
}
