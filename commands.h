// part of aiterm project
// commands.h
// header for local command handler / registry / wrappers
// By: Peter Talbott
// Assisted by: Gemini
// June 2026

#ifndef COMMANDS_H
#define COMMANDS_H

#include "gui.h"
#include "utils.h"
#include "update.h"

extern const char* get_features_teext();

typedef void (*cmd_handler_func)(AppContext *app, const char *args);

typedef struct {
    const char *name;        // The command string (e.g., "status")
    const char *description; // What shows up in /help
    cmd_handler_func handler;
} CommandRegistry;

// Function prototypes
gboolean execute_command(AppContext *app, const char *input);
void display_dynamic_help(AppContext *app);
void handle_help_wrapper(AppContext *app, const char *args);
void handle_status_wrapper(AppContext *app, const char *args);
void handle_features_wrapper(AppContext *app, const char *args);
void handle_clear_wrapper(AppContext *app, const char *args);
void handle_provider_wrapper(AppContext *app, const char *args);
void handle_list_models_wrapper(AppContext *app, const char *args);
void handle_reset_state_wrapper(AppContext *app, const char *args);
void handle_hw_wrapper(AppContext *app, const char *args);
void handle_history_wrapper(AppContext *app, const char *args);
void handle_version_wrapper(AppContext *app, const char *args);
void handle_load_config_wrapper(AppContext *app, const char *args);
void handle_save_config_wrapper(AppContext *app, const char *args);
void handle_tee_on_wrapper(AppContext *app, const char *args);
void handle_tee_off_wrapper(AppContext *app, const char *args);
void handle_auto_reply_on_wrapper(AppContext *app, const char *args);
void handle_auto_reply_off_wrapper(AppContext *app, const char *args);
void handle_auto_execute_on_wrapper(AppContext *app, const char *args);
void handle_auto_execute_off_wrapper(AppContext *app, const char *args);
void handle_auto_on_wrapper(AppContext *app, const char *args);
void handle_auto_off_wrapper(AppContext *app, const char *args);

#endif