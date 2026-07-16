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
#include <string.h>
#include <regex.h>
#include <gtk/gtk.h>

#include "noisefilter.h"
#include "utils.h"
#include "gui.h"
#include "openai.h"
#include "crypto.h" // Add this include
#include "update.h"
#include "session_manager.h"
#include "tee_handler.h"
#include "ratelimit.h"

/**
 * Strips ANSI escape sequences (colors, cursor movements) from a string.
 * This ensures your database strings can match cleanly against actual text.
 */
char* strip_ansi_sequences(const char *src) {
    if (!src) return NULL;

    size_t len = strlen(src);
    char *dst = g_malloc(len + 1);
    size_t j = 0;
    int in_escape = 0;
    for (size_t i = 0; i < len; i++) {
        if (in_escape) {
            // ANSI escape sequences end with a letter (e.g., 'm' for color, 'H' for cursor)
            if ((src[i] >= 'A' && src[i] <= 'Z') || (src[i] >= 'a' && src[i] <= 'z')) {
                in_escape = 0;
            }
        } else if (src[i] == '\x1b') { // Escape character
            in_escape = 1;
        } else {
            dst[j++] = src[i];
        }
    }
    dst[j] = '\0';
    size_t out_len = strlen(dst);
    DEBUG_PRINT("[DEBUG]: [Noise Filter] Strip ANSI, Input string length %ld bytes, Output string length %ld bytes\n",
                 len, out_len);
    return dst;
}

/**
 * Hydrates or refreshes the AppContext GtkListStore with active database noise rules.
 * This function handles its own threading mutex lock boundaries safely.
 */
void noise_filter_load_from_db(AppContext *app) {
    if (!app || !app->database.global_db_conn) return;

    // 1. Thread safety gate for our shared global connection handle
    pthread_mutex_lock(&app->access.db_mutex);
    DEBUG_PRINT("[DEBUG]: [Noise Filter] Locked DB Mutex\n");

    // 2. Lazily allocate the ListStore or completely drop previous cached rows
    if (!app->noise.filters) {
        app->noise.filters = gtk_list_store_new(1, G_TYPE_STRING);
    } else {
        gtk_list_store_clear(app->noise.filters);
    }

    // 3. Query the target rules table (Adjust table/column name to match your schema)
    const char *query = "SELECT pattern FROM noise_filters ORDER BY id ASC";
    DEBUG_PRINT("[DEBUG]: [Noise Filter] Running SQL Query: %s\n", query);
    if (mysql_query(app->database.global_db_conn, query) != 0) {
        fprintf(stderr, "[Noise Filter] MySQL query compilation error: %s\n",
                mysql_error(app->database.global_db_conn));
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
                // Append and populate column index 0 with our pattern text
                gtk_list_store_append(app->noise.filters, &iter);
                gtk_list_store_set(app->noise.filters, &iter, 0, row[0], -1);
            }
        }
        DEBUG_PRINT("[DEBUG]: [Noise Filter] Loaded %ld Filters from database\n", app->noise.count);
        mysql_free_result(result);
    }

    pthread_mutex_unlock(&app->access.db_mutex);
    DEBUG_PRINT("[DEBUG]: [Noise Filter] Unlocked DB Mutex\n");
}


/**
 * In-place substring removal utility helper
 */
void remove_substring(char *str, const char *sub, gboolean dash) {
    size_t len = strlen(sub);
    if (len == 0) return;
    if (dash) {
        DEBUG_PRINT("-");
    }

    char *p = str;
    while ((p = strstr(p, sub)) != NULL) {
        memmove(p, p + len, strlen(p + len) + 1);
    }
}

/**
 * Core Application Engine Hook
 * Takes raw terminal text, cleans it up, processes DB rules, and returns a new string.
 */
char* noise_filter_apply(AppContext *app, const char *raw_input) {
    if (!raw_input) return NULL;

    // If the GUI menu toggle is off, bypass filtering completely
    if (!app->sys.noise_filter_enabled) {
        return g_strdup(raw_input);
    }
    size_t in_len = strlen(raw_input);
    // Step 1: Strip ANSI clutter so rules can actually match
    char *filtered_text = strip_ansi_sequences(raw_input);

    if (app->noise.filters) {
        GtkTreeModel *model = GTK_TREE_MODEL(app->noise.filters);
        GtkTreeIter iter;

        if (gtk_tree_model_get_iter_first(model, &iter)) {
            DEBUG_PRINT("[DEBUG]: [Noise Filter] Initiated Remove Substring: ");
            long COUNT = 0;
            do {
                COUNT++;
                char *pattern = NULL;
                // Fetch the dynamic string reference assigned out of column 0
                gtk_tree_model_get(model, &iter, 0, &pattern, -1);

                if (pattern && strlen(pattern) > 0 && COUNT < app->noise.count) {
                    remove_substring(filtered_text, pattern, TRUE);
                }

                // Important: GTK allocates copies for text lookups, always free it
                if (pattern) {
                    g_free(pattern);
                }
            } while (gtk_tree_model_iter_next(model, &iter));
        }
    }

    // Fallback simple safety cleanup: Strip trailing or orphan carriage returns
    remove_substring(filtered_text, "\r", FALSE);
    size_t out_len = strlen(filtered_text);
    DEBUG_PRINT("-\n[DEBUG]: [Noise Filter] Input Length %ld Bytes, Output Length %ld Bytes\n", in_len, out_len);
    return filtered_text; // Remember to g_free() this string after sending it to Gemini!
}

void noise_filter_add(AppContext *app, const char *filter_data) {
    if (!app || !filter_data || strlen(filter_data) == 0) {
        return;
    }

    // 2. Thread-safe DB Insert
    pthread_mutex_lock(&app->access.db_mutex);
    if (app->database.global_db_conn) {
        // Allocate space for escaping the string safely to prevent SQL injection
        unsigned long len = strlen(filter_data);
        char *escaped_data = malloc(len * 2 + 1);
        mysql_real_escape_string(app->database.global_db_conn, escaped_data, filter_data, len);

        // Construct the insert query (adjust table/column names to match your schema)
        char query[512];
        snprintf(query, sizeof(query),
                 "INSERT INTO noise_filters (pattern) VALUES ('%s');",
                 escaped_data);

        if (mysql_query(app->database.global_db_conn, query) != 0) {
            fprintf(stderr, "[ERROR]: MySQL insert failed: %s\n", mysql_error(app->database.global_db_conn));
        } else {
            // DEBUG_PRINT("[DEBUG]: [NOISE] Successfully saved pattern to DB.\n");
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

    // Query just the fields we care about
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
    pthread_mutex_unlock(&app->access.db_mutex);

    // Write the complete multi-line block out to your AI pane wrapper
    write_to_ai_pane_wrapper(app, output->str);
    g_string_free(output, TRUE);
}

gboolean ignore_tee_line(AppContext *app, const char *line) {
    gboolean RV = FALSE;
    if (!app) return RV;
    if (!app->sys.noise_filter_enabled) return RV;
    pthread_mutex_lock(&app->access.db_mutex);

    if (!app->database.global_db_conn) {
        pthread_mutex_unlock(&app->access.db_mutex);
        DEBUG_PRINT("[DEBUG]: [Noise Filter] Database connection not active.\n");
        return RV;
    }

    // Query just the fields we care about
    const char *query = "SELECT id, pattern, uuid FROM noise_filters ORDER BY id ASC";

    if (mysql_query(app->database.global_db_conn, query) != 0) {
        char *err_msg = g_strdup_printf("[ERROR]: MySQL query failed: %s\n", mysql_error(app->database.global_db_conn));
        pthread_mutex_unlock(&app->access.db_mutex);
        DEBUG_PRINT("[DEBUG]: [Noise Filter] %s\n", err_msg);
        g_free(err_msg);
        return RV;
    }

    MYSQL_RES *result = mysql_store_result(app->database.global_db_conn);
    if (!result) {
        pthread_mutex_unlock(&app->access.db_mutex);
        DEBUG_PRINT("[DEBUG]: [Noise Filter] Could not retrieve MySQL result set.\n");
        return RV;
    }


    my_ulonglong num_rows = mysql_num_rows(result);
    if (num_rows == 0) {
        mysql_free_result(result);
        pthread_mutex_unlock(&app->access.db_mutex);
        DEBUG_PRINT("[DEBUG]: [Noise Filter] No noise filters found!\n");
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
            DEBUG_PRINT("[DEBUG]: [Noise Filter] Match found ignoring line\n");
            break;
        }
    }

    mysql_free_result(result);
    pthread_mutex_unlock(&app->access.db_mutex);
    return RV;
}


