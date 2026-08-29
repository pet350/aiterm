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
#include <stdio.h>
#include <regex.h>
#include <gtk/gtk.h>

#include "noisefilter.h"
#include "utils.h"
#include "gui.h"
#include "openai.h"
#include "crypto.h"
#include "update.h"
#include "session_manager.h"
#include "tee_handler.h"
#include "ratelimit.h"

/**
 * Strips ANSI escape sequences (colors, cursor movements) from a string.
 */
char* strip_ansi_sequences(const char *src) {
    if (!src) return NULL;

    size_t len = strlen(src);
    char *dst = g_malloc(len + 1);
    size_t j = 0;
    int in_escape = 0;

    for (size_t i = 0; i < len; i++) {
        if (in_escape) {
            if ((src[i] >= 'A' && src[i] <= 'Z') || (src[i] >= 'a' && src[i] <= 'z')) {
                in_escape = 0;
            }
        } else if (src[i] == '\x1b') {
            in_escape = 1;
        } else {
            dst[j++] = src[i];
        }
    }
    dst[j] = '\0';
    return dst;
}

/**
 * In-place substring removal helper.
 * Removes all occurrences of `sub` from `str`.
 */
void remove_substring(char *str, const char *sub, gboolean dash) {
    if (!str || !sub) return;
    size_t len = strlen(sub);
    if (len == 0) return;
    if (dash) {
        fprintf(stderr, "-");
    }
    char *match;
    while ((match = strstr(str, sub)) != NULL) {
        memmove(match, match + len, strlen(match + len) + 1);
    }
}

/**
 * Hydrates or refreshes the AppContext GtkListStore with active database noise rules.
 */
void noise_filter_load_from_db(AppContext *app) {
    if (!app || !app->database.global_db_conn) return;

    pthread_mutex_lock(&app->access.db_mutex);
    DEBUG_PRINT("[DEBUG]: [Noise Filter] Locked DB Mutex\n");

    // GTK model is maintained only for the UI. Background workers use the
    // thread-safe pattern snapshot below.
    if (!app->noise.filters) {
        app->noise.filters = gtk_list_store_new(1, G_TYPE_STRING);
    } else {
        gtk_list_store_clear(app->noise.filters);
    }

    g_mutex_lock(&app->noise.patterns_mutex);
    if (!app->noise.patterns)
        app->noise.patterns = g_ptr_array_new_with_free_func(g_free);
    else
        g_ptr_array_set_size(app->noise.patterns, 0);

    const char *query = "SELECT pattern FROM noise_filters ORDER BY id ASC";
    DEBUG_PRINT("[DEBUG]: [Noise Filter] Running SQL Query: %s\n", query);

    if (mysql_query(app->database.global_db_conn, query) != 0) {
        fprintf(stderr, "[Noise Filter] MySQL query error: %s\n",
                mysql_error(app->database.global_db_conn));
        g_mutex_unlock(&app->noise.patterns_mutex);
        pthread_mutex_unlock(&app->access.db_mutex);
        DEBUG_PRINT("[DEBUG]: [Noise Filter] Unlocked DB Mutex\n");
        return;
    }

    MYSQL_RES *result = mysql_store_result(app->database.global_db_conn);
    if (result) {
        MYSQL_ROW row;
        app->noise.count = 0;
        while ((row = mysql_fetch_row(result))) {
            if (row[0] && strlen(row[0]) > 0) {
                app->noise.count++;
                GtkTreeIter iter;
                gtk_list_store_append(app->noise.filters, &iter);
                gtk_list_store_set(app->noise.filters, &iter, 0, row[0], -1);
                g_ptr_array_add(app->noise.patterns, g_strdup(row[0]));
            }
        }
        DEBUG_PRINT("[DEBUG]: [Noise Filter] Loaded %ld Filters from database\n", app->noise.count);
        mysql_free_result(result);
    }

    g_mutex_unlock(&app->noise.patterns_mutex);
    pthread_mutex_unlock(&app->access.db_mutex);
    DEBUG_PRINT("[DEBUG]: [Noise Filter] Unlocked DB Mutex\n");
}

// Core Application Engine Hook
// Cleans ANSI escape codes and strips out registered DB noise filter patterns.
char* noise_filter_apply(AppContext *app, const char *raw_input) {
    if (!raw_input) return NULL;

    // Bypass completely if feature is disabled
    if (!app || !app->sys.noise_filter_enabled) {
        return g_strdup(raw_input);
    }

    // Step 1: Strip ANSI control codes
    char *working_text = strip_ansi_sequences(raw_input);
    if (!working_text) return g_strdup(raw_input);

    // Step 2: Iterate over the thread-safe pattern snapshot. Never touch the
    // GTK ListStore from a worker thread.
    g_mutex_lock(&app->noise.patterns_mutex);
    if (app->noise.patterns && app->noise.patterns->len > 0) {
        DEBUG_PRINT("[DEBUG]: [Noise Filter] Initiating substring removals: ");
        for (guint i = 0; i < app->noise.patterns->len; i++) {
            const char *pattern = g_ptr_array_index(app->noise.patterns, i);
            if (pattern && *pattern)
                remove_substring(working_text, pattern, TRUE);
        }
        fprintf(stderr, "\n");
    }
    g_mutex_unlock(&app->noise.patterns_mutex);

    size_t in_len = strlen(raw_input);
    size_t out_len = strlen(working_text);
    DEBUG_PRINT("[DEBUG]: [Noise Filter] In/Out Length %ld / %ld Bytes, Removed %ld Bytes\n", in_len, out_len, (in_len - out_len));

    return working_text; // Allocated via g_malloc; caller frees with g_free()
}

void noise_filter_add(AppContext *app, const char *filter_data) {
    if (!app || !filter_data || strlen(filter_data) == 0) return;

    pthread_mutex_lock(&app->access.db_mutex);
    if (app->database.global_db_conn) {
        unsigned long len = strlen(filter_data);
        char *escaped_data = malloc(len * 2 + 1);
        mysql_real_escape_string(app->database.global_db_conn, escaped_data, filter_data, len);

        char query[512];
        snprintf(query, sizeof(query),
                 "INSERT INTO noise_filters (pattern) VALUES ('%s');",
                 escaped_data);

        if (mysql_query(app->database.global_db_conn, query) != 0) {
            fprintf(stderr, "[ERROR]: MySQL insert failed: %s\n", mysql_error(app->database.global_db_conn));
        }

        free(escaped_data);
    } else {
        fprintf(stderr, "[WARN]: Database connection not active. Pattern only saved to memory.\n");
    }
    pthread_mutex_unlock(&app->access.db_mutex);
}

void noise_filter_list(AppContext *app) {
    if (!app) return;

    pthread_mutex_lock(&app->access.db_mutex);

    if (!app->database.global_db_conn) {
        pthread_mutex_unlock(&app->access.db_mutex);
        write_to_ai_pane_wrapper(app, "[ERROR]: Database connection not active.");
        return;
    }

    const char *query = "SELECT id, pattern, uuid FROM noise_filters ORDER BY id ASC";

    if (mysql_query(app->database.global_db_conn, query) != 0) {
        char *err_msg = g_strdup_printf("[ERROR]: MySQL query failed: %s", mysql_error(app->database.global_db_conn));
        pthread_mutex_unlock(&app->access.db_mutex);
        write_to_ai_pane_wrapper(app, err_msg);
        g_free(err_msg);
        return;
    }

    MYSQL_RES *result = mysql_store_result(app->database.global_db_conn);
    if (!result) {
        pthread_mutex_unlock(&app->access.db_mutex);
        write_to_ai_pane_wrapper(app, "[ERROR]: Could not retrieve MySQL result set.");
        return;
    }

    my_ulonglong num_rows = mysql_num_rows(result);
    if (num_rows == 0) {
        mysql_free_result(result);
        pthread_mutex_unlock(&app->access.db_mutex);
        write_to_ai_pane_wrapper(app, "--- Active Noise Filters ---\nNo noise filters currently configured.");
        return;
    }

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
    pthread_mutex_unlock(&app->access.db_mutex);

    write_to_ai_pane_wrapper(app, output->str);
    g_string_free(output, TRUE);
}

gboolean ignore_tee_line(AppContext *app, const char *line) {
    if (!app || !line) return FALSE;
    if (!app->sys.noise_filter_enabled) return FALSE;

    pthread_mutex_lock(&app->access.db_mutex);

    if (!app->database.global_db_conn) {
        pthread_mutex_unlock(&app->access.db_mutex);
        DEBUG_PRINT("[DEBUG]: [Noise Filter] Database connection not active.\n");
        return FALSE;
    }

    const char *query = "SELECT id, pattern, uuid FROM noise_filters ORDER BY id ASC";

    if (mysql_query(app->database.global_db_conn, query) != 0) {
        char *err_msg = g_strdup_printf("[ERROR]: MySQL query failed: %s\n", mysql_error(app->database.global_db_conn));
        pthread_mutex_unlock(&app->access.db_mutex);
        DEBUG_PRINT("[DEBUG]: [Noise Filter] %s\n", err_msg);
        g_free(err_msg);
        return FALSE;
    }

    MYSQL_RES *result = mysql_store_result(app->database.global_db_conn);
    if (!result) {
        pthread_mutex_unlock(&app->access.db_mutex);
        DEBUG_PRINT("[DEBUG]: [Noise Filter] Could not retrieve MySQL result set.\n");
        return FALSE;
    }

    my_ulonglong num_rows = mysql_num_rows(result);
    if (num_rows == 0) {
        mysql_free_result(result);
        pthread_mutex_unlock(&app->access.db_mutex);
        DEBUG_PRINT("[DEBUG]: [Noise Filter] No noise filters found!\n");
        return FALSE;
    }

    gboolean rv = FALSE;
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(result))) {
        const char *pattern = row[1];
        if (pattern && strstr(line, pattern)) {
            rv = TRUE;
            DEBUG_PRINT("[DEBUG]: [Noise Filter] Match found ignoring line\n");
            break;
        }
    }

    mysql_free_result(result);
    pthread_mutex_unlock(&app->access.db_mutex);
    return rv;
}
