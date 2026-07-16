// part of aiterm project
// session_manager.c
// Various utilities used for session management
// By: Peter Talbott
// Assisted by: Gemini
// May 2026, June 2026

#include <stdio.h>
#include <uuid/uuid.h>
#include <time.h>
#include "gui.h"
#include "update.h"
#include "session_manager.h"
#include "session_manager_gui.h"
#include "commands.h"

// Helper that actually executes on the Main UI thread
static gboolean ui_notify_callback(gpointer data) {
    char *message = (char *)data;
    // Assuming you have access to the app context or use a global if necessary
    // If you need the 'app' context, you'll need to wrap it in a struct
    write_to_ai_pane(global_app, "System", message, "ai_tag", "body_tag");

    g_free(message); // Free the string we allocated in the background thread
    return FALSE;    // Return FALSE so the idle source is removed
}


void write_to_ai_pane_wrapper(AppContext *app, char *data) {
    // We duplicate the string because the worker thread might free its
    // local copy before the UI thread gets a chance to read it.
    char *msg_copy = g_strdup(data);

    // Schedule the UI update on the main thread
    g_idle_add(ui_notify_callback, msg_copy);
}



void session_init(AppContext *app) {
    // 1. Setup Mutex and Condition Variable for DB sync
    pthread_mutex_init(&app->access.db_init_mutex, NULL);
    pthread_cond_init(&app->access.db_init_cond, NULL);
    app->sys.db_initialized = FALSE;

    DEBUG_PRINT("[DEBUG]: SESSION_INIT: Waiting for DB_INIT Mutex to be unlocked.\n");

    // 2. Wait for the DB initialization thread to signal completion
    pthread_mutex_lock(&app->access.db_init_mutex);
    while (!app->sys.db_initialized) {
        pthread_cond_wait(&app->access.db_init_cond, &app->access.db_init_mutex);
    }
    pthread_mutex_unlock(&app->access.db_init_mutex);

    DEBUG_PRINT("[DEBUG]: SESSION_INIT: DB_INIT is complete, proceeding to lock Mutex for SESSION_INIT.\n");

    // 3. Initialize Session Mutex
    pthread_mutex_init(&app->access.session_mutex, NULL);

    // 4. Set Initial Defaults
    app->session.history_cache = g_string_new("");
    app->session.last_sync = time(NULL);
    app->session.is_seeded = FALSE;
    app->session.last_sent_db_id = 0;
    app->session.session_uuid = NULL;

    if (!app->session.cfg_loaded_write_to_global)  {
        app->session.write_to_global = FALSE;
        DEBUG_PRINT("[DEBUG]: SESSION_INIT: set Session write to global: FALSE\n");
    }

    if (!app->session.cfg_loaded_read_from_global) {
         app->session.read_from_global= TRUE;
         DEBUG_PRINT("[DEBUG]: SESSION_INIT: set Session read from global: TRUE\n");
    }

    // 5. Query for Default Session
    pthread_mutex_lock(&app->access.session_mutex);
    MYSQL_RES *res = NULL;

    DEBUG_PRINT("[DEBUG]: SESSION_INIT: DB is ready. Querying is_default...\n");
    // Note: Ensure global_db_conn is initialized by the time we get here
    if (mysql_query(app->database.global_db_conn, "SELECT uuid FROM sessions WHERE is_default = 1 LIMIT 1") == 0) {
        res = mysql_store_result(app->database.global_db_conn);
        if (res) {
            MYSQL_ROW row = mysql_fetch_row(res);
            if (row) {
                app->session.session_uuid = g_strdup(row[0]);
                DEBUG_PRINT("[DEBUG]: [SESSION]: Loaded DEFAULT session: %s\n", app->session.session_uuid);
            }
            mysql_free_result(res);
        }
    }

    // 6. Fallback: Generate New UUID if no default found
    if (!app->session.session_uuid) {
        uuid_t binuuid;
        uuid_generate_random(binuuid);
        app->session.session_uuid = g_malloc(37);
        uuid_unparse_lower(binuuid, app->session.session_uuid);

        DEBUG_PRINT("[DEBUG]: [SESSION]: Initialized NEW session: %s\n", app->session.session_uuid);
    }

    pthread_mutex_unlock(&app->access.session_mutex);
    DEBUG_PRINT("[DEBUG]: SESSION_INIT: Unblocked SESSION Mutex\n");
}

void session_sync_to_db(AppContext *app) {
    // 1. Get the cache content
    char *data = app->session.history_cache->str;

    // 2. Add your database insert logic here
    // e.g., mysql_query(app->db_conn, "INSERT INTO history...");

    // 3. Reset the cache after successful sync
    g_string_truncate(app->session.history_cache, 0);
    app->session.last_sync = time(NULL);

    DEBUG_PRINT("[DEBUG]: SESSION: Synced %ld bytes to DB\n", strlen(data));
}


// This creates the <tee> tags for live stream
char* session_create_tee_chunk(AppContext *app, const char *raw_data) {
    if (!app || !raw_data) return NULL;
    return g_strdup_printf("<tee session_id=\"%s\">\n%s\n</tee>\n",
                           app->session.session_uuid,
                           raw_data);
}

// This creates the <history> tags for DB-fetched logs
char* session_create_history_chunk(AppContext *app, const char *raw_data) {
    if (!app || !raw_data) return NULL;
    return g_strdup_printf("<history session_uuid=\"%s\" timestamp=\"%ld\">\n%s\n</history>\n",
                           app->session.session_uuid,
                           (long)time(NULL),
                           raw_data);
}

void session_log(AppContext *app, const char *data) {
    // Wrap data in XML
    g_string_append_printf(app->session.history_cache,
        "<history session=\"%s\" timestamp=\"%ld\">%s</history>\n",
        app->session.session_uuid, time(NULL), data);

    // Auto-sync if cache is large or time has passed
    if (app->session.history_cache->len > 4096) {
        session_sync_to_db(app);
    }
}

gpointer session_db_worker(gpointer data) {
    SessionThreadData *std = (SessionThreadData*)data;
    AppContext *app = std->app;
    char escaped_desc[1024];
    char *query = NULL;
    uuid_t new_uuid;
    char new_uuid_str[37];
    uuid_generate_random(new_uuid);
    uuid_unparse_lower(new_uuid, new_uuid_str);

    pthread_mutex_lock(&app->access.db_mutex);
    DEBUG_PRINT("[DEBUG]: SESSION_DB_WORKER: Locked DB mutex\n");

    switch (std->type) {
        case CMD_SESSION_NEW: {
            char *new_desc = g_strdup("[ New Session ]");
            query = g_strdup_printf("INSERT IGNORE INTO sessions (uuid, description) VALUES ('%s', '%s');",
                                         new_uuid_str, new_desc);
            DEBUG_PRINT("[DEBUG]: SESSION_DB_WORKER: Creating new session %s: %s\n", new_uuid_str, new_desc);
            g_free(new_desc); // Properly scoped free
            break;
        }
        case CMD_SESSION_LIST: {
            DEBUG_PRINT("[DEBUG]: SESSION_DB_WORKER: Listing sessions\n");
            query = g_strdup("SELECT uuid, description FROM sessions");
            break;
        }
        case CMD_SESSION_SHOW: {
            DEBUG_PRINT("[DEBUG]: SESSION_DB_WORKER: Fetching info for Session %s\n", app->session.session_uuid);
            query = g_strdup_printf("SELECT uuid, created_at, description FROM sessions WHERE uuid = '%s';", app->session.session_uuid);
            break;
        }
        case CMD_SESSION_LOAD: {
            if (std->arg) {
                DEBUG_PRINT("[DEBUG]: SESSION_DB_WORKER: Loading session %s\n", std->arg);
                query = g_strdup_printf("SELECT uuid FROM sessions WHERE uuid = '%s';", std->arg);
            }
            break;
        }
        case CMD_SESSION_DELETE: {
            char *del_hist = g_strdup_printf("DELETE FROM aiterm_history WHERE session_uuid = '%s';", std->arg);
            DEBUG_PRINT("[DEBUG]: SESSION_DB_WORKER: Executing query: %s\n", del_hist);

            if (mysql_query(app->database.global_db_conn, del_hist) == 0) {
                my_ulonglong affected = mysql_affected_rows(app->database.global_db_conn);
                DEBUG_PRINT("[DEBUG]: SESSION_DB_WORKER: Deleted %llu rows from history.\n", affected);
            } else {
                DEBUG_PRINT("[ERROR]: SESSION_DB_WORKER: History delete failed: %s\n", mysql_error(app->database.global_db_conn));
            }
            g_free(del_hist);
            query = g_strdup_printf("DELETE FROM sessions WHERE uuid = '%s';", std->arg);
            break;
        }
        case CMD_SESSION_DESCRIPTION: {
            if (std->arg) {
                mysql_real_escape_string(app->database.global_db_conn, escaped_desc, std->arg, strlen(std->arg));
                query = g_strdup_printf("UPDATE sessions SET description = '%s' WHERE uuid = '%s';",
                                        escaped_desc, app->session.session_uuid);
                DEBUG_PRINT("[DEBUG]: SESSION_DB_WORKER: Updating description for %s to '%s'\n",
                            app->session.session_uuid, escaped_desc);
            }
            break;
        }
        case CMD_SESSION_NO_DEFAULT: {
            query = g_strdup("UPDATE sessions SET is_default = 0");
            DEBUG_PRINT("[DEBUG]: [SESSION]: Clearing default session flag.\n");
            break;
        }
        case CMD_SESSION_DEFAULT: {
            if (std->arg) {
                // 1. Unset all defaults
                mysql_query(app->database.global_db_conn, "UPDATE sessions SET is_default = 0");
                // 2. Set the requested UUID as default
                query = g_strdup_printf("UPDATE sessions SET is_default = 1 WHERE uuid = '%s'", std->arg);
                DEBUG_PRINT("[DEBUG]: [SESSION]: Setting default session to %s\n", std->arg);
            }
            break;
        }
    }
    if (query) {
        DEBUG_PRINT("[DEBUG]: SESSION_DB_WORKER: Case query %s\n", query);
        if (mysql_query(app->database.global_db_conn, query) == 0) {
            DEBUG_PRINT("[DEBUG]: SESSION_DB_WORKER: Successful execution of mysql query!\n");

            if (std->type == CMD_SESSION_LIST) {
                MYSQL_RES *res = mysql_store_result(app->database.global_db_conn);
                if (res) {
                    SessionListResult *list_res = g_malloc0(sizeof(SessionListResult));
                    MYSQL_ROW row;
                    while ((row = mysql_fetch_row(res))) {
                        SessionEntry *entry = g_malloc0(sizeof(SessionEntry));
                        entry->uuid = g_strdup(row[0]);
                        entry->description = row[1] ? g_strdup(row[1]) : g_strdup("No description");
                        list_res->entries = g_list_append(list_res->entries, entry);
                    }
                    g_idle_add(ui_display_list, list_res);
                    mysql_free_result(res);
                }
            } else if (std->type == CMD_SESSION_NEW) {
                pthread_mutex_lock(&app->access.session_mutex);
                if (app->session.session_uuid) {
                    g_free(app->session.session_uuid);
                }
                app->session.session_uuid = g_strdup(new_uuid_str);
                pthread_mutex_unlock(&app->access.session_mutex);
                write_to_ai_pane_wrapper(app, " Session loaded successfully.");

                SessionShowResult *show_res = g_malloc0(sizeof(SessionShowResult));
                show_res->uuid = g_strdup(app->session.session_uuid);
                show_res->description = g_strdup("[ New Session ]");
                g_idle_add(ui_display_show, show_res);
                g_idle_add((GSourceFunc)refresh_list_callback, app);
            } else if (std->type == CMD_SESSION_DELETE ) {
                g_idle_add((GSourceFunc)refresh_list_callback, app);
            } else if (std->type == CMD_SESSION_SHOW) {
                MYSQL_RES *res = mysql_store_result(app->database.global_db_conn);
                if (res) {
                    MYSQL_ROW row = mysql_fetch_row(res);
                    SessionShowResult *show_res = g_malloc0(sizeof(SessionShowResult));
                    if (row) {
                        show_res->uuid = g_strdup(row[0]);
                        show_res->description = row[2] ? g_strdup(row[2]) : g_strdup("No description");
                    } else {
                        show_res->uuid = g_strdup(app->session.session_uuid);
                        show_res->description = g_strdup("No description");
                    }
                    g_idle_add(ui_display_show, show_res);
                    mysql_free_result(res);
                }
            } else if (std->type == CMD_SESSION_DESCRIPTION) {
                char *msg = g_strdup_printf("Updated session: %s description: %s\n", app->session.session_uuid, escaped_desc);
                write_to_ai_pane_wrapper(app, msg);
                g_free(msg);
            } else if (std->type == CMD_SESSION_LOAD) {
                MYSQL_RES *res = mysql_store_result(app->database.global_db_conn);
                if (res) {
                    if (mysql_num_rows(res) > 0) {
                        pthread_mutex_lock(&app->access.session_mutex);
                        g_free(app->session.session_uuid);
                        app->session.session_uuid = g_strdup(std->arg);
                        pthread_mutex_unlock(&app->access.session_mutex);
                        write_to_ai_pane_wrapper(app, "Session loaded successfully.");
                    } else {
                        write_to_ai_pane_wrapper(app, "Error: Session UUID not found.");
                    }
                    mysql_free_result(res);
                }
            }  else if (std->type == CMD_SESSION_DEFAULT) {
                    char *msg = g_strdup_printf("System: Default session set to %s", std->arg);
                    write_to_ai_pane_wrapper(app, msg);
                    g_free(msg);
            } else if (std->type == CMD_SESSION_NO_DEFAULT) {
                   write_to_ai_pane_wrapper(app, "System: Default session cleared. Reverting to random sessions.");
            } else {
                my_ulonglong affected = mysql_affected_rows(app->database.global_db_conn);
                DEBUG_PRINT("[DEBUG]: SESSION_DB_WORKER: %llu row(s) affected.\n", affected);
            }
        } else {
            DEBUG_PRINT("[DEBUG]: [ERROR] DB Query failed: %s\n", mysql_error(app->database.global_db_conn));
        }
        g_free(query);
    }

    pthread_mutex_unlock(&app->access.db_mutex);
    if (std->arg) g_free(std->arg);
    if (std) g_free(std);

    return NULL;
}

