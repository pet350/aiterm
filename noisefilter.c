// part of aiterm project
// noisefilter.c
// Various utilities used in this project
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
#include "session_manager.h"
#include "tee_handler.h"
#include "ratelimit.h"
#include "noisefilter.h"

void noise_filter_add(AppContext *app, const char *filter_data) {
    if (!app || !filter_data || strlen(filter_data) == 0) {
        return;
    }

    // 2. Thread-safe DB Insert
    pthread_mutex_lock(&app->db_mutex);
    if (app->global_db_conn) {
        // Allocate space for escaping the string safely to prevent SQL injection
        unsigned long len = strlen(filter_data);
        char *escaped_data = malloc(len * 2 + 1);
        mysql_real_escape_string(app->global_db_conn, escaped_data, filter_data, len);

        // Construct the insert query (adjust table/column names to match your schema)
        char query[512];
        snprintf(query, sizeof(query),
                 "INSERT INTO noise_filters (pattern) VALUES ('%s');",
                 escaped_data);

        if (mysql_query(app->global_db_conn, query) != 0) {
            fprintf(stderr, "[ERROR]: MySQL insert failed: %s\n", mysql_error(app->global_db_conn));
        } else {
            // DEBUG_PRINT("[DEBUG]: [NOISE] Successfully saved pattern to DB.\n");
        }

        free(escaped_data);
    } else {
        fprintf(stderr, "[WARN]: Database connection not active. Pattern only saved to memory.\n");
    }
    pthread_mutex_unlock(&app->db_mutex);
}

void noise_filter_list(AppContext *app) {
    if (!app) return;

    pthread_mutex_lock(&app->db_mutex);

    if (!app->global_db_conn) {
        pthread_mutex_unlock(&app->db_mutex);
        write_to_ai_pane_wrapper(app, "[ERROR]: Database connection not active.");
        return;
    }

    // Query just the fields we care about
    const char *query = "SELECT id, pattern, uuid FROM noise_filters ORDER BY id ASC";

    if (mysql_query(app->global_db_conn, query) != 0) {
        char *err_msg = g_strdup_printf("[ERROR]: MySQL query failed: %s", mysql_error(app->global_db_conn));
        pthread_mutex_unlock(&app->db_mutex);
        write_to_ai_pane_wrapper(app, err_msg);
        g_free(err_msg);
        return;
    }

    MYSQL_RES *result = mysql_store_result(app->global_db_conn);
    if (!result) {
        pthread_mutex_unlock(&app->db_mutex);
        write_to_ai_pane_wrapper(app, "[ERROR]: Could not retrieve MySQL result set.");
        return;
    }

    my_ulonglong num_rows = mysql_num_rows(result);
    if (num_rows == 0) {
        mysql_free_result(result);
        pthread_mutex_unlock(&app->db_mutex);
        write_to_ai_pane_wrapper(app, "--- Active Noise Filters ---\nNo noise filters currently configured.");
        return;
    }

    // Build a dynamic string to hold the entire list display
    GString *output = g_string_new("--- Active Noise Filters ---\n");
    g_string_append_printf(output, "%-4s | %-36s | %s\n", "ID", "UUID", "Pattern");
    g_string_append(output, "----------------------------------------------------------------------\n");

    MYSQL_ROW row;
    while ((row = mysql_fetch_row(result))) {
        const char *id      = row[0] ? row[0] : "N/A";
        const char *pattern = row[1] ? row[1] : "";
        const char *uuid    = row[2] ? row[2] : "N/A";

        g_string_append_printf(output, "%-4s | %-36s | %s\n", id, uuid, pattern);
    }

    mysql_free_result(result);
    pthread_mutex_unlock(&app->db_mutex);

    // Write the complete multi-line block out to your AI pane wrapper
    write_to_ai_pane_wrapper(app, output->str);
    g_string_free(output, TRUE);
}

gboolean ignore_tee_line(AppContext *app, const char *line) {
    gboolean RV = FALSE;
    if (!app) return RV;
    if (!app->noise_filter_enabled) return RV;
    pthread_mutex_lock(&app->db_mutex);

    if (!app->global_db_conn) {
        pthread_mutex_unlock(&app->db_mutex);
        DEBUG_PRINT("[DEBUG]: Noise Filter: Database connection not active.\n");
        return RV;
    }

    // Query just the fields we care about
    const char *query = "SELECT id, pattern, uuid FROM noise_filters ORDER BY id ASC";

    if (mysql_query(app->global_db_conn, query) != 0) {
        char *err_msg = g_strdup_printf("[ERROR]: MySQL query failed: %s\n", mysql_error(app->global_db_conn));
        pthread_mutex_unlock(&app->db_mutex);
        DEBUG_PRINT("[DEBUG]: Noise Filter: ");
        DEBUG_PRINT(err_msg);
        g_free(err_msg);
        return RV;
    }

    MYSQL_RES *result = mysql_store_result(app->global_db_conn);
    if (!result) {
        pthread_mutex_unlock(&app->db_mutex);
        DEBUG_PRINT("[DEBUG]: Noise Filter: Could not retrieve MySQL result set.\n");
        return RV;
    }


    my_ulonglong num_rows = mysql_num_rows(result);
    if (num_rows == 0) {
        mysql_free_result(result);
        pthread_mutex_unlock(&app->db_mutex);
        DEBUG_PRINT("[DEBUG]: Noise Filter: No noise filters found!\n");
        return RV;
    }

    MYSQL_ROW row;
    int count = 0;
    while ((row = mysql_fetch_row(result))) {
        count++;
        const char *pattern = row[1];
        // const char *uuid    = row[2] ? row[2] : "N/A";
        if (pattern && strstr(line, pattern)) {
            RV = TRUE;
            DEBUG_PRINT("[DEBUG]: Noise Filter: Match found ignoring line\n");
            break;
        }
    }

    mysql_free_result(result);
    pthread_mutex_unlock(&app->db_mutex);
    return RV;
}


