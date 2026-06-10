// part of aiterm project
// commands.c
// local command handler / registry / wrappers
// By: Peter Talbott
// Assisted by: Gemini
// June 2026

#include <stdio.h>
#include <string.h>
#include "commands.h"
#include "gui.h"
#include "utils.h"
#include "update.h"
#include "help.h"
#include "gemini.h"

static CommandRegistry registry[] = {
    {"auto on", "Turn on all three: System Tee, Auto Reply, and Auto Execute", handle_auto_on_wrapper},
    {"auto off", "Turn off all three: System Tee, Auto Reply, and Auto Execute", handle_auto_off_wrapper},
    {"autoreply on", "Toggle real-time prompt analysis ON", handle_auto_reply_on_wrapper},
    {"autoreply off", "Toggle real-time prompt analysis OFF", handle_auto_reply_off_wrapper},
    {"auto execute on", "Toggle direct execution of AI payloads ON", handle_auto_execute_on_wrapper},
    {"auto execute off", "Toggle direct execution of AI payloads OFF", handle_auto_execute_off_wrapper},
    {"clear", "Clear the contents of the AI pane", handle_clear_wrapper},
    {"features", "Display new aiterm features", handle_features_wrapper},
    {"help", "Display this dynamic help menu", handle_help_wrapper},
    {"history", "Display mysql history", handle_history_wrapper},
    {"hw", "Display various hardware info", handle_hw_wrapper},
    {"list models", "Display all models from current provider", handle_list_models_wrapper},
    {"load config", "Loads configurations from aiterm.conf", handle_load_config_wrapper},
    {"save config", "Saves configuration to aiterm.conf", handle_save_config_wrapper},
    {"provider", "Display AI provider [OpenAI/Gemini]", handle_provider_wrapper},
    {"reset state", "Reeset current AI state back to ready", handle_reset_state_wrapper},
    {"status", "Display operational metrics", handle_status_wrapper},
    {"tee on", " Toggle immediate terminal capturing ON", handle_tee_on_wrapper},
    {"tee off", "Toggle immediate terminal capturing OFF", handle_tee_off_wrapper},
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
    GString *help = g_string_new("--- Available Commands ---\n");
    for (int i = 0; registry[i].name != NULL; i++) {
        g_string_append_printf(help, "%-15s : %s\n", registry[i].name, registry[i].description);
    }
    write_to_ai_pane(app, "Help: ", help->str, "system_tag", "body_tag");
    g_string_free(help, TRUE);
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
    g_free(features_text);
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
    app->is_processing = FALSE;
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
}

void handle_save_config_wrapper(AppContext *app, const char *args) {
    write_to_ai_pane(app, "System: ", "Saved settings to configuration file", "system_tag", "ai_tag");
    save_config(app);
}

void handle_tee_on_wrapper(AppContext *app, const char *args) {
    write_to_ai_pane(app, "System: ", "Tee Collection ENABLED", "system_tag", "ai_tag");
    app->tee_enabled = TRUE;
}

void handle_tee_off_wrapper(AppContext *app, const char *args) {
    write_to_ai_pane(app, "System: ", "Tee Collection DISABLED", "system_tag", "ai_tag");
    app->tee_enabled = FALSE;
}

void handle_auto_reply_on_wrapper(AppContext *app, const char *args) {
    write_to_ai_pane(app, "System: ", "AutoReply ENABLED", "system_tag", "ai_tag");
    app->autoreply_enabled = TRUE;
}

void handle_auto_reply_off_wrapper(AppContext *app, const char *args) {
    write_to_ai_pane(app, "System: ", "AutoReply DISABLED", "system_tag", "ai_tag");
    app->autoreply_enabled = FALSE;
}

void handle_auto_execute_on_wrapper(AppContext *app, const char *args) {
    write_to_ai_pane(app, "System: ", "Tee Auto Execute ENABLED", "system_tag", "ai_tag");
    app->auto_execute_enabled = TRUE;
}

void handle_auto_execute_off_wrapper(AppContext *app, const char *args) {
    write_to_ai_pane(app, "System: ", "Tee Auto Execute DISABLED", "system_tag", "ai_tag");
    app->auto_execute_enabled = FALSE;
}

void handle_auto_on_wrapper(AppContext *app, const char *args) {
    handle_tee_on_wrapper(app, NULL);
    handle_auto_reply_on_wrapper(app, NULL);
    handle_auto_execute_on_wrapper(app, NULL);
}

void handle_auto_off_wrapper(AppContext *app, const char *args) {
    handle_tee_off_wrapper(app, NULL);
    handle_auto_reply_off_wrapper(app, NULL);
    handle_auto_execute_off_wrapper(app, NULL);
}

