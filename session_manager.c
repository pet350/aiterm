// part of aiterm project
// session_manager.c
// Various utilities used for session management
// By: Peter Talbott
// Assisted by: Gemini
// May 2026, June 2026

#include "session_manager.h"
#include <stdio.h>

void session_init(AppContext *app) {
    uuid_t binuuid;
    uuid_generate_random(binuuid);
    uuid_unparse_lower(binuuid, app->session.session_uuid);

    app->session.history_cache = g_string_new("");
    app->session.last_sync = time(NULL);

    DEBUG_PRINT("DEBUG: [SESSION]: Initialized: %s\n", app->session.session_uuid);
}

void session_sync_to_db(AppContext *app) {
    // 1. Get the cache content
    char *data = app->session.history_cache->str;

    // 2. Add your database insert logic here
    // e.g., mysql_query(app->db_conn, "INSERT INTO history...");

    // 3. Reset the cache after successful sync
    g_string_truncate(app->session.history_cache, 0);
    app->session.last_sync = time(NULL);

    DEBUG_PRINT("[DEBUG] SESSION: Synced %ld bytes to DB\n", strlen(data));
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


