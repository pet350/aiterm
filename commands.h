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

extern const char* get_features_text();

typedef void (*cmd_handler_func)(AppContext *app, const char *args);

typedef struct {
    const char *name;        // The command string (e.g., "status")
    const char *description; // What shows up in /help
    cmd_handler_func handler;
} CommandRegistry;

// Function prototypes
gboolean ui_display_list(gpointer data);
gboolean ui_display_show(gpointer data);
gboolean execute_command(AppContext *app, const char *input);

void cmd_show_queue_wrapper(AppContext *app, const char *args);
void cmd_force_snmp_flush(AppContext *app, const char *args);
void dump_raw_snmp_payload_to_ai_wrapper(AppContext *app, const char *args);
void cmd_snmp_manager_wrapper(AppContext *app, const char *args);
void cmd_close_snmp_manager_wrapper(AppContext *app, const char *args);
void handle_snmp_command(AppContext *app, const char *args);
void parse_command_line_options(AppContext *app, int argc, char *argv[]);
void display_dynamic_help(AppContext *app);
void cmd_show_command_line_help_wrapper(AppContext *app, const char *args);
void cmd_invalidate_cache(AppContext *app, const char *args);
void cmd_session_sync(AppContext *app, const char *args);
void dispatch_command_to_pane(AppContext *app, int target_pane_id, const char *cmd);

// Handle Wrapper Prototypes
void handle_directive_wrapper(AppContext *app, const char *args);
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
void handle_extended_help(AppContext *app, const char *args);

void cmd_set_rpm(AppContext *app, const char *args);
void cmd_set_retry_times(AppContext *app, const char *args);
void cmd_set_retry_delay(AppContext *app, const char *args);
void cmd_reset_db_connect(AppContext *app, const char *args);

// CMD Session Prototypes
void cmd_session_new(AppContext *app, const char *args);
void cmd_session_list(AppContext *app, const char *args);
void cmd_session_show(AppContext *app, const char *args);
void cmd_session_description(AppContext *app, const char *args);
void cmd_session_load(AppContext *app, const char *args);
void cmd_session_delete(AppContext *app, const char *args);
void cmd_session_default(AppContext *app, const char *args);
void cmd_session_no_default(AppContext *app, const char *args);
void cmd_session_read_from_gloal_toggle(AppContext *app, const char *args);
void cmd_session_write_to_global_toggle(AppContext *app, const char *args);

// open different manager windows
void cmd_session_manager_wrapper(AppContext *app, const char *args);
void cmd_history_manager_wrapper(AppContext *app, const char *args);
void cmd_noisefilter_manager_wrapper(AppContext *app, const char *args);
void cmd_policy_manager_wrapper(AppContext *app, const char *args);

// CMD Noise Prototypes
void cmd_noise_add(AppContext *app, const char *args);
void cmd_noise_list(AppContext *app, const char *args);
void cmd_noise_delete(AppContext *app, const char *args);
void cmd_noisefilter_reload_wrapper(AppContext *app, const char *args);

// close different manager windows
void cmd_close_policy_manager_wrapper(AppContext *app, const char *args);
void cmd_close_history_manager_wrapper(AppContext *app, const char *args);
void cmd_close_session_manager_wrapper(AppContext *app, const char *args);
void cmd_close_noise_manager_wrapper(AppContext *app, const char *args);

// ==== Toggle ON / OFF / STATUS Function Prototypes ====
void cmd_toggle_snmp_payload(AppContext *app, const char *args);
void cmd_toggle_snmp_ticker(AppContext *app, const char *args);
void cmd_toggle_session_config(AppContext *app, const char *args);
void cmd_toggle_auto_all(AppContext *app, const char *args);
void cmd_toggle_tee(AppContext *app, const char *args);
void cmd_toggle_autoexe(AppContext *app, const char *args);
void cmd_toggle_autoreply(AppContext *app, const char *args);
void cmd_toggle_autoretry(AppContext *app, const char *args);
void cmd_toggle_debug(AppContext *app, const char *args);
void cmd_toggle_noise_filter(AppContext *app, const char *args);
void cmd_toggle_smart_cache(AppContext *app, const char *args);
void cmd_toggle_ratelimit(AppContext *app, const char *args);
void cmd_toggle_xml_tagging(AppContext *app, const char *args);

#endif

