// part of aiterm project
// status.c
// Display status utility used in this project
// By: Peter Talbott
// Assisted by: Gemini
// May 2026

#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <json-c/json.h>
#include <vte/vte.h>
#include <mariadb/mysql.h>

#include "utils.h"
#include "gui.h"
#include "openai.h"
#include "crypto.h" // Add this include
#include "update.h"
#include "tee_handler.h"
#include "ratelimit.h"
#include "status.h"

// updated 0.8.3 send status to AI as well as display it
// modified 0.8.4 display status in color
void display_status(AppContext *app) {
    // Lock the database mutex to ensure thread-safety during the status check
    mysql_thread_init();
    pthread_mutex_lock(&app->db_mutex);

    gboolean is_connected = FALSE;
    int ping_res = -1;

    DEBUG_PRINT("[DEBUG]: [STATUS] Checking database connection status...\n");
    DEBUG_PRINT("[DEBUG]: [STATUS] app->global_db_conn pointer value: %p\n", (void*)app->global_db_conn);

    if (app->global_db_conn != NULL) {
        // mysql_ping returns 0 if the connection is alive
        ping_res = mysql_ping(app->global_db_conn);
        DEBUG_PRINT("[DEBUG]: [STATUS] mysql_ping returned: %d\n", ping_res);
        if (ping_res == 0) {
            is_connected = TRUE;
        } else {
            DEBUG_PRINT("[DEBUG]: [STATUS] mysql_ping failed error: %s\n", mysql_error(app->global_db_conn));
        }
    }

    pthread_mutex_unlock(&app->db_mutex);
    // 1. Build a single raw string containing the whole status report for the AI
    GString *status_report = g_string_new("--- SYSTEM STATUS ---\n");

    const char *tee_val = app->tee_enabled ? "ON" : "OFF";
    g_string_append_printf(status_report, "Tee Logging:\t%s\n", tee_val);

    const char *auto_val = app->autoreply_enabled ? "ON" : "OFF";
    g_string_append_printf(status_report, "Autoreply:\t%s\n", auto_val);

    const char *auto_exec_val = app->auto_execute_enabled ? "ON" : "OFF";
    g_string_append_printf(status_report, "Auto Execute:\t%s\n", auto_exec_val);

    const char *ratelimit_val = app->ratelimit_enabled ? "ON" : "OFF";
    g_string_append_printf(status_report, "Rate Limit Enabled:\t%s\n", ratelimit_val);

    const char *smart_cache_val = app->smart_cache_enabled ? "ON" : "OFF";
    g_string_append_printf(status_report, "Smart Cache Enabled:\t%s\n", smart_cache_val);

    const char *noise_filter_val = app->noise_filter_enabled ? "ON" : "OFF";
    g_string_append_printf(status_report, "Noise Filter Enabled:\t%s\n", noise_filter_val);

    const char *write_to_global_val = app->session.write_to_global ? "GLOBAL session" : "STRICT session";
    g_string_append_printf(status_report, "Writting to database:\t%s\n", write_to_global_val);

    const char *read_from_global_val = app->session.read_from_global ? "GLOBAL session" : "STRICT session";
    g_string_append_printf(status_report, "Reading from database:\t%s\n", read_from_global_val);

    int db_ok = (app->global_db_conn && mysql_ping(app->global_db_conn) == 0);
    g_string_append_printf(status_report, "Database:\t%s\n", db_ok ? "CONNECTED" : "DISCONNECTED");

    const char *mysql_ping_val = is_connected ? "ON" : "OFF";
    g_string_append_printf(status_report, "MariaDB Ping Results:\t%s\n", mysql_ping_val);

    int ai_ok = (app->api_key && strlen(app->api_key) > 0);
    g_string_append_printf(status_report, "AI Status:\t%s\n", ai_ok ? "READY" : "MISSING CONFIG");

    g_string_append_printf(status_report, "Session UUID:\t%s\n", app->session.session_uuid ? app->session.session_uuid : "N/A");
    g_string_append(status_report, "---------------------");

    // 2. Display to the User in the AI Pane with granular coloring
    append_ai_text(app, "[ Local Status ]\n", "cmd_tag");
    append_ai_text(app, "--- SYSTEM STATUS ---\n", "body_tag");

    // Tee Logging Row
    append_ai_text(app, "Tee Logging:\t\t", "body_tag");
    append_ai_text(app, tee_val, app->tee_enabled ? "ai_tag" : "cmd_tag");
    append_ai_text(app, "\n", "body_tag");

    // Autoreply Row
    append_ai_text(app, "Autoreply:\t\t", "body_tag");
    append_ai_text(app, auto_val, app->autoreply_enabled ? "ai_tag" : "cmd_tag");
    append_ai_text(app, "\n", "body_tag");

    // Auto Execute Row
    append_ai_text(app, "Auto Execute:\t\t", "body_tag");
    append_ai_text(app, auto_exec_val, app->auto_execute_enabled ? "ai_tag" : "cmd_tag");
    append_ai_text(app, "\n", "body_tag");

    // Ratelimit Row
    append_ai_text(app, "Ratelimit:\t\t", "body_tag");
    append_ai_text(app, ratelimit_val, app->ratelimit_enabled ? "ai_tag" : "cmd_tag");
    append_ai_text(app, "\n", "body_tag");

    // Smart Cache Row
    append_ai_text(app, "Smart Cache:\t\t", "body_tag");
    append_ai_text(app, smart_cache_val, app->smart_cache_enabled ? "ai_tag" : "cmd_tag");
    append_ai_text(app, "\n", "body_tag");

    // Noise Filter Row
    append_ai_text(app, "Noise Filter:\t\t", "body_tag");
    append_ai_text(app, noise_filter_val, app->noise_filter_enabled ? "ai_tag" : "cmd_tag");
    append_ai_text(app, "\n", "body_tag");

    // Database Writting
    append_ai_text(app, "Write to database:\t", "body_tag");
    append_ai_text(app, write_to_global_val, app->session.write_to_global ? "cmd_tag" : "ai_tag");
    append_ai_text(app, "\n", "body_tag");

    // Database Reading
    append_ai_text(app, "Read from database:\t", "body_tag");
    append_ai_text(app, read_from_global_val, app->session.read_from_global ? "cmd_tag" : "ai_tag");
    append_ai_text(app, "\n", "body_tag");

    // mysql ping rresults
    append_ai_text(app, "MYSQL Server:\t\t", "body_tag");
    append_ai_text(app, mysql_ping_val ? "ALIVE" : "DEAD", mysql_ping_val ? "ai_tag" : "cmd_tag");
    append_ai_text(app, "\n", "body_tag");

    // Database Row
    append_ai_text(app, "Database:\t\t", "body_tag");
    append_ai_text(app, db_ok ? "CONNECTED" : "DISCONNECTED", db_ok ? "ai_tag" : "cmd_tag");
    append_ai_text(app, "\n", "body_tag");

    // AI Status Row
    append_ai_text(app, "AI Status:\t\t", "body_tag");
    append_ai_text(app, ai_ok ? "READY" : "MISSING CONFIG", ai_ok ? "ai_tag" : "cmd_tag");
    append_ai_text(app, "\n", "body_tag");

    // Session UUID Row
    append_ai_text(app, "Session UUID:\t\t", "body_tag");
    append_ai_text(app, app->session.session_uuid, "ai_tag");
    append_ai_text(app, "\n---------------------\n\n", "body_tag");

    // 3. Send the raw un-tagged plain text block to the AI history
    if (app->tee_enabled) {
        tee_handle_output(app, status_report->str);
        tee_handle_output(app, "\n");
        tee_flush_timed(app);
    }

    // Cleanup
    g_string_free(status_report, TRUE);
    mysql_thread_end();
}
