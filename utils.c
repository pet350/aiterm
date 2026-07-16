// part of aiterm project
// utils.c
// Various utilities used in this project
// By: Peter Talbott
// Assisted by: Gemini
// May 2026

#include <stdlib.h>
#include <glib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <time.h>
#include <json-c/json.h>
#include <vte/vte.h>
#include <mariadb/mysql.h>
#include <gtk/gtk.h>
#include <pthread.h>
#include "utils.h"
#include "gui.h"
#include "openai.h"
#include "crypto.h" // Add this include
#include "update.h"
#include "tee_handler.h"
#include "ratelimit.h"
#include "commands.h"
#include "noisefilter.h"

void initialize_booleans(AppContext *app) {
    // 1. Set initial variables to their needed defaults
    app->sys.db_initialized = FALSE;
    app->sys.autoreply_enabled = FALSE;
    app->sys.tee_enabled = FALSE;
    app->sys.auto_execute_enabled = FALSE;
    app->sys.debug_mode = FALSE;
    app->sys.debug_mode_override = FALSE;
    app->sys.is_processing = FALSE;
    app->sys.ratelimit_enabled = FALSE;
    app->sys.mysql_busy = FALSE;
    app->sys.ai_busy = FALSE;
    app->sys.smart_cache_enabled = FALSE;

    // NON-Boolean initializer
    app->database.sequence_id = 0;
    app->limiter.requests_per_minute=20;

    app->session.cfg_loaded_write_to_global = FALSE;
    app->session.cfg_loaded_read_from_global = FALSE;

    app->xml.tagging_enabled = FALSE;
}

// Added 0.9.5-beta
// For wrapping payload in XML tags that AI will understand
char* xml_wrap(AppContext *app, const char *input) {
    if (!input) return NULL;
    if (!app->xml.tagging_enabled) return g_strdup(input);
    if (!app->xml.type) return g_strdup(input);

    GString *xml_buffer = g_string_new(NULL);
    time_t now = time(NULL);
    char time_str[20];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", localtime(&now));
    switch(app->xml.type) {
        case TAG_NONE:
            // xml buffer was already initialized so we're just going to append input to it
            DEBUG_PRINT("[DEBUG]: [XML_WRAP] xml.type is none, not wrapping\n");
            g_string_append(xml_buffer, input);
            break;
        case TAG_HISTORY:
            DEBUG_PRINT("[DEBUG]: [XML_WRAP] xml.type is history, wrapping with <context>\n");
            // Being a history payload we don't want to send the current timestamp
            g_string_append(xml_buffer, "<context");
            if (app->session.session_uuid) {
                g_string_append_printf(xml_buffer, " session=\"%s\"", app->session.session_uuid);
            }
            g_string_append_printf(xml_buffer, ">%s</context>\n", input);
            break;
        case TAG_MEMORY:
            DEBUG_PRINT("[DEBUG]: [XML_WRAP] xml.type is memory, wrapping with <memory>\n");
            // Here this would be user data loaded from database,
            //if the timestamp from the database is available we'll use it
            g_string_printf(xml_buffer, "<memory");
            if (app->xml.database_timestamp) {
                g_string_append_printf(xml_buffer, " timestamp=\"%s\"", app->xml.database_timestamp);
            }
            if (app->session.session_uuid) {
                g_string_append_printf(xml_buffer, " session=\"%s\"", app->session.session_uuid);
            }
            g_string_append_printf(xml_buffer, ">%s</memory>\n", input);
            break;
        case TAG_LOG_DUMP: // Was ** TAG_TEE: **
            DEBUG_PRINT("[DEBUG]: [XML_WRAP] xml.type is log_dump, wrapping with <log_dump>\n");
            // Tee is live data payload, we will timestamp it
	    // A Wise AI assistant suggested log_dump as the tag instead of Tee
            g_string_printf(xml_buffer, "<log_dump timestamp=\"%s\"", time_str);
            if (app->session.session_uuid) {
                g_string_append_printf(xml_buffer, " session=\"%s\"", app->session.session_uuid);
            }
            g_string_append_printf(xml_buffer, ">%s</log_dump>\n", input);
            break;
        case TAG_SYSTEM:
            DEBUG_PRINT("[DEBUG]: [XML_WRAP] xml.type is system, wrapping with <system>\n");
            // System is live data payload, we will timestamp it
            g_string_printf(xml_buffer, "<system timestamp=\"%s\"", time_str);
            if (app->session.session_uuid) {
                g_string_append_printf(xml_buffer, " session=\"%s\"", app->session.session_uuid);
            }
            g_string_append_printf(xml_buffer, ">%s</system>\n", input);
            break;
        case TAG_STATUS:
            DEBUG_PRINT("[DEBUG]: [XML_WRAP] xml.type is status, wrapping with <status>\n");
            // System is live data payload, we will timestamp it
            g_string_printf(xml_buffer, "<status timestamp=\"%s\"", time_str);
            if (app->session.session_uuid) {
                g_string_append_printf(xml_buffer, " session=\"%s\"", app->session.session_uuid);
            }
            g_string_append_printf(xml_buffer, ">%s</status>\n", input);
            break;
    }
    // Return the string and destroy the container, keeping the data alive
    return g_string_free(xml_buffer, FALSE);
}

static void provider_replace_string(char **field, const char *value) {
    if (*field) free(*field);
    *field = value ? strdup(value) : NULL;
}

void free_provider_config(ProviderConfig *provider) {
    if (!provider) return;
    free(provider->name);
    free(provider->model);
    free(provider->api_key);
    free(provider->base_url);
    free(provider->endpoint);
    free(provider->auth_header);
    free(provider->auth_scheme);
    free(provider->query_key_name);
    memset(provider, 0, sizeof(ProviderConfig));
}

void init_provider_config(AppContext *app) {
    if (!app) return;

    ProviderConfig *provider = &app->provider_config;
    const char *name = app->provider_config.provider ? app->provider_config.provider : "openai";
    const char *model = app->aiterm_runtime.model ? app->aiterm_runtime.model : NULL;

    free_provider_config(provider);
    provider_replace_string(&provider->name, name);
    provider_replace_string(&provider->api_key, app->security.api_key);

    if (strcasecmp(name, "gemini") == 0) {
        provider->kind = PROVIDER_KIND_GEMINI_GENERATE;
        provider->api_key_in_query = TRUE;
        provider_replace_string(&provider->model, model ? model : "gemini-flash-latest");
        provider_replace_string(&provider->base_url, "https://generativelanguage.googleapis.com/v1beta");
        provider_replace_string(&provider->endpoint, "models/%s:generateContent");
        provider_replace_string(&provider->query_key_name, "key");
    } else {
        provider->kind = PROVIDER_KIND_OPENAI_CHAT;
        provider->api_key_in_query = FALSE;
        provider_replace_string(&provider->model, model ? model : OPENAI_MODEL);
        provider_replace_string(&provider->base_url, "https://api.openai.com/v1");
        provider_replace_string(&provider->endpoint, "chat/completions");
        provider_replace_string(&provider->auth_header, "Authorization");
        provider_replace_string(&provider->auth_scheme, "Bearer");
    }

    const char *env_base_url = getenv("AITERM_PROVIDER_BASE_URL");
    const char *env_endpoint = getenv("AITERM_PROVIDER_ENDPOINT");
    const char *env_auth_header = getenv("AITERM_PROVIDER_AUTH_HEADER");
    const char *env_auth_scheme = getenv("AITERM_PROVIDER_AUTH_SCHEME");
    const char *env_query_key = getenv("AITERM_PROVIDER_QUERY_KEY");

    if (env_base_url && *env_base_url) provider_replace_string(&provider->base_url, env_base_url);
    if (env_endpoint && *env_endpoint) provider_replace_string(&provider->endpoint, env_endpoint);
    if (env_auth_header && *env_auth_header) provider_replace_string(&provider->auth_header, env_auth_header);
    if (env_auth_scheme) provider_replace_string(&provider->auth_scheme, *env_auth_scheme ? env_auth_scheme : NULL);
    if (env_query_key && *env_query_key) provider_replace_string(&provider->query_key_name, env_query_key);
    DEBUG_PRINT("[DEBUG]: [Provider_Init]: Base URL: %s\n", provider->base_url);
}

// This is the actual definition where the memory is allocated
HistoryEntry history[5];
int history_count = 0;

char* get_uuid_filter(AppContext *app) {
    if (app->session.read_from_global) {
        return g_strdup_printf("IN ('%s', '%s')",
                               GLOBAL_SESSION_UUID,
                               app->session.session_uuid);
    } else {
        return g_strdup_printf("= '%s'", app->session.session_uuid);
    }
}

// Worker thread function for database initialization
void* init_db_thread_worker(void *data) {
    AppContext *app = (AppContext*)data;

    // CRITICAL: Initialize thread-specific MySQL memory
    mysql_thread_init();

    DEBUG_PRINT("[DEBUG]: [DB_THREAD] Invoking init_remote_db...\n");
    if (init_remote_db(app)) {
        DEBUG_PRINT("[DEBUG]: Database setup and persistent connection ready.\n");
    } else {
        DEBUG_PRINT("[DEBUG]: Database offline. History will not be saved.\n");
    }
    pthread_mutex_lock(&app->access.db_init_mutex);
    DEBUG_PRINT("[DEBUG]: INIT_DB_THREAD_WORKER: Locked DB init Mutex\n");
    app->sys.db_initialized = TRUE;
    pthread_cond_signal(&app->access.db_init_cond); // Wake up waiting threads
    pthread_mutex_unlock(&app->access.db_init_mutex);
    DEBUG_PRINT("[DEBUG]: INIT_DB_THREAD_WORKER: Unlocked DB init Mutex\n");
    // CRITICAL: Clean up thread-specific MySQL memory
    mysql_thread_end();
    return NULL;
}

void feed_terminal_header(VteTerminal *terminal, const char *msg) {
    char buf[1024];
    snprintf(buf, sizeof(buf), "\r%s[AI Executing]: %s%s\n", ANSI_CYAN, msg, ANSI_RESET);
    vte_terminal_feed(terminal, buf, -1);
}


// Function to display All History
// Modified 0.7.4-delta to use global mysql connection
// Modified 0.7.5-alpha for autoreply and color responces
void display_all_history(AppContext *app) {
    // 1) FIRST THING: Initialize MySQL for this thread
    mysql_thread_init();

    //DBWorkerData *data = (DBWorkerData *)arg;
    extern AppContext *global_app;
    GString *history_output = g_string_new("");
    MYSQL_RES *res = NULL;
    // If this fails, it safely jumps to cleanup where mysql_thread_end() handles it
    if (!global_app->database.global_db_conn) {
        cmd_reset_db_connect(app, NULL);
        goto cleanup;
    }

    // LOCK: Ensure only one thread uses the database pipe at a time
    pthread_mutex_lock(&global_app->access.db_mutex);
    DEBUG_PRINT("[DEBUG]: DISPLAY_ALL_HISTORY: Locked DB Mutex\n");
    if (!app->database.global_db_conn) {
        write_to_ai_pane(app, "System: ", "Database connection is not active.", "cmd_tag", "cmd_tag");
        goto cleanup;
    }

    MYSQL_ROW row;
    char *uuid_filter = get_uuid_filter(app);
    char *query = g_strdup_printf(
       "SELECT role, content FROM aiterm_history WHERE session_uuid %s ORDER BY id DESC LIMIT 50",
        uuid_filter);


    if (mysql_query(app->database.global_db_conn, query)) {
        write_to_ai_pane(app, "System: ", "Error fetching history from database.", "cmd_tag", "cmd_tag");
        goto cleanup;
    }
    DEBUG_PRINT("[DEBUG]: DISPLAY_ALL_HISTORY: Query %s\n", query);
    res = mysql_store_result(app->database.global_db_conn);
    if (!res) goto cleanup;

    // Use a GString to build the history output
    g_string_append(history_output, "--- Last 50 Messages ---\n\n");

    while ((row = mysql_fetch_row(res))) {
        // row[0] is role, row[1] is content
        g_string_append_printf(history_output, "[%s]: %s\n\n",
                               row[0] ? row[0] : "unknown",
                               row[1] ? row[1] : "");
    }

    if (history_output->len > 25) { // If we actually found rows
        write_to_ai_pane(app, "[ History ]\n", history_output->str, "cmd_tag", "ai_tag");
    } else {
        write_to_ai_pane(app, "System: ", "History is empty.", "cmd_tag", "cmd_tag");
    }

    cleanup:
    if (history_output) {
        g_string_free(history_output, TRUE);
        history_output = NULL; // Prevent double free
    }
    if (res) {
        mysql_free_result(res);
        res = NULL;
    }

    pthread_mutex_unlock(&global_app->access.db_mutex);
    DEBUG_PRINT("[DEBUG]: DISPLAY_ALL_HISTORY: Unlocked DB Mutex\n");
    mysql_thread_end();
    return;
}

// Modified 0.7.4-delta for global mysql connection
// Rewritten 0.8.4-delta
// Modified 0.8.5-gamma for target uuid
void* db_worker_thread(void *arg) {
    mysql_thread_init();
    DBWorkerData *data = (DBWorkerData *)arg;
    if (!data) { DEBUG_PRINT("[DEBUG]: [WORKER] Received NULL data pointer!\n"); return NULL; }
    extern AppContext *global_app;

    if (!global_app->database.global_db_conn) goto cleanup;

    // ROUTING LOGIC: Respect the write_to_global flag
    const char *target_uuid = global_app->session.write_to_global
                              ? GLOBAL_SESSION_UUID
                              : global_app->session.session_uuid;

    // LOCK: Ensure only one thread uses the database pipe at a time
    pthread_mutex_lock(&global_app->access.db_mutex);
    DEBUG_PRINT("[DEBUG]: [WORKER] Locked DB Mutex\n");
    DEBUG_PRINT("[DEBUG]: [WORKER] Starting job for seq %d, is_tee=%d\n", data->sequence_id, data->is_tee);
    if (data->is_tee) {
        char *esc_out = malloc(strlen(data->terminal_output) * 2 + 1);
        char *esc_ai = malloc(strlen(data->ai_analysis) * 2 + 1);

        mysql_real_escape_string(global_app->database.global_db_conn, esc_out, data->terminal_output, strlen(data->terminal_output));
        mysql_real_escape_string(global_app->database.global_db_conn, esc_ai, data->ai_analysis, strlen(data->ai_analysis));

        size_t query_len = strlen(esc_out) + strlen(esc_ai) + strlen(data->session_uuid) + 1024;
        char *query = malloc(query_len);
        if (query) {
            snprintf(query, query_len,
                     "INSERT INTO aiterm_history (role, content, is_tee, session_uuid, sequence_id) VALUES "
                     "('terminal', '%s', 1, '%s', %d), ('assistant', '%s', 1, '%s', %d)",
                     esc_out, target_uuid, data->sequence_id, esc_ai, data->session_uuid, data->sequence_id);
            mysql_query(global_app->database.global_db_conn, query);
            free(query);
        }
        free(esc_out); free(esc_ai);
    } else {
        char *esc_user = malloc(strlen(data->user_text) * 2 + 1);
        char *esc_ai = malloc(strlen(data->ai_text) * 2 + 1);
        mysql_real_escape_string(global_app->database.global_db_conn, esc_user, data->user_text, strlen(data->user_text));
        mysql_real_escape_string(global_app->database.global_db_conn, esc_ai, data->ai_text, strlen(data->ai_text));

        size_t query_len = strlen(esc_user) + strlen(esc_ai) + strlen(data->session_uuid) + 512;
        char *query = malloc(query_len);
        if (query) {
	     snprintf(query, query_len,
                     "INSERT INTO aiterm_history (role, content, is_tee, session_uuid, sequence_id) " // ADD sequence_id
                     "VALUES ('user', '%s', 0, '%s', %d), ('assistant', '%s', 0, '%s', %d)",         // ADD %d twice
                     esc_user, target_uuid, data->sequence_id, esc_ai, data->session_uuid, data->sequence_id);
            mysql_query(global_app->database.global_db_conn, query);
            free(query);
        }
        free(esc_user); free(esc_ai);
    }
    // UNLOCK: Let the next thread in
    pthread_mutex_unlock(&global_app->access.db_mutex);
    DEBUG_PRINT("[DEBUG]: [WORKER] Unlocked DB Mutex]n\n");
    cleanup:
    if (data->terminal_output) free(data->terminal_output);
    if (data->ai_analysis) free(data->ai_analysis);
    if (data->user_text) free(data->user_text);
    if (data->ai_text) free(data->ai_text);
    if (data->session_uuid) free(data->session_uuid);
    DEBUG_PRINT("[DEBUG]: [WORKER] Job completed and memory freed for seq %d\n", data->sequence_id);
    free(data);
    mysql_thread_end();
    return NULL;
}

//Added 0.8.5-gamma
// returns mysql query string based on app->session.read_from_global
char* build_delta_sync_query(AppContext *app) {
    char *uuid_clause = get_uuid_filter(global_app);
    // Now construct the final query
    char *query = g_strdup_printf(
        "SELECT role, content FROM aiterm_history "
        "WHERE session_uuid %s AND sequence_id > %d "
        "ORDER BY sequence_id ASC",
        uuid_clause,
        app->session.last_sent_db_id
    );

    g_free(uuid_clause); // Clean up the temporary clause
    return query;
}

// Function ot Initialize MYSQL/MariaDB Database
// Modified 0.7.4-delta to handle global mysql connection
int init_remote_db(AppContext *app) {
    // 1. Initialize the GLOBAL handle
    app->database.global_db_conn = mysql_init(NULL);
    if (app->database.global_db_conn == NULL) return 0;

    // === NEW: Set a 3-second connection timeout ===
    unsigned int timeout = 3; // 3 seconds
    mysql_options(app->database.global_db_conn, MYSQL_OPT_CONNECT_TIMEOUT, (const char *)&timeout);

    my_bool reconnect = 1;
    mysql_options(app->database.global_db_conn, MYSQL_OPT_RECONNECT, &reconnect);

    // NEW: Set read/write timeouts for queries so they don't hang forever
    // mysql_options(app->database.global_db_conn, MYSQL_OPT_READ_TIMEOUT, (const char *)&timeout);
    // mysql_options(app->database.global_db_conn, MYSQL_OPT_WRITE_TIMEOUT, (const char *)&timeout);

    DEBUG_PRINT("[DEBUG]: [DB] Connecting to %s (timeout: %us)...\n", app->database.db_host, timeout);
    // 2. Connect to the server (No DB selected yet)
    if (mysql_real_connect(app->database.global_db_conn, app->database.db_host, app->database.db_user, app->database.db_pass, NULL, 0, NULL, 0) == NULL) {
        DEBUG_PRINT("[DEBUG]: DB Connection Error: %s\n", mysql_error(app->database.global_db_conn));
        mysql_close(app->database.global_db_conn);
        app->database.global_db_conn = NULL;
        return 0;
    }
    DEBUG_PRINT("[DEBUG]: [DB] Successfully connected to database host.\n");

    // 3. Create and Select Database
    char db_query[256];
    snprintf(db_query, sizeof(db_query), "CREATE DATABASE IF NOT EXISTS %s", app->database.db_name);

    DEBUG_PRINT("[DEBUG]: [DB] Executing: %s\n", db_query);
    mysql_query(app->database.global_db_conn, db_query);

    DEBUG_PRINT("[DEBUG]: [DB] Selecting database: %s\n", app->database.db_name);
    mysql_select_db(app->database.global_db_conn, app->database.db_name);

    // 4. Create History Table
    DEBUG_PRINT("[DEBUG]: [DB] Executing: CREATE TABLE IF NOT EXISTS aiterm_history\n");
    mysql_query(app->database.global_db_conn, "CREATE TABLE IF NOT EXISTS aiterm_history (id INT AUTO_INCREMENT PRIMARY KEY)");

    // 5. Run Migrations
    const char* migrations[] = {
        "ALTER TABLE aiterm_history ADD COLUMN IF NOT EXISTS role VARCHAR(20)",
        "ALTER TABLE aiterm_history ADD COLUMN IF NOT EXISTS content TEXT",
        "ALTER TABLE aiterm_history ADD COLUMN IF NOT EXISTS is_tee TINYINT(1) DEFAULT 0",
        "ALTER TABLE aiterm_history ADD COLUMN IF NOT EXISTS session_uuid VARCHAR(36)",
        "ALTER TABLE aiterm_history ADD COLUMN IF NOT EXISTS sequence_id INT DEFAULT 0",
        "ALTER TABLE aiterm_history ADD COLUMN IF NOT EXISTS created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP"
    };

    for (int i = 0; i < sizeof(migrations)/sizeof(char*); i++) {
        DEBUG_PRINT("[DEBUG]: [DB] Running migration query [%d]: %s\n", i, migrations[i]);
        mysql_query(app->database.global_db_conn, migrations[i]);
    }

    // 6. Setup Triggers Table
    const char *trigger_table_query =
        "CREATE TABLE IF NOT EXISTS relevance_triggers ("
        "id INT AUTO_INCREMENT PRIMARY KEY, "
        "keyword VARCHAR(50) UNIQUE, "
        "hit_count INT DEFAULT 1, "
        "last_used TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON DUPLICATE KEY UPDATE last_used=CURRENT_TIMESTAMP)";
    DEBUG_PRINT("[DEBUG]: [DB] Executing: CREATE TABLE IF NOT EXISTS relevance_triggers\n");
    mysql_query(app->database.global_db_conn, trigger_table_query);

    const char *command_policy_table =
	"CREATE TABLE IF NOT EXISTS command_policies ("
	"command VARCHAR(256) PRIMARY KEY, "
	"type VARCHAR(32) NOT NULL, "
	"risk_level VARCHAR(32) NOT NULL, "
	"updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON DUPLICATE KEY UPDATE updated_at=CURRENT_TIMESTAMP"
	")";
    DEBUG_PRINT("[DEBUG]: [DB] Executing: CREATE TABLE IF NOT EXISTS command_policies\n");
    mysql_query(app->database.global_db_conn, command_policy_table);


    DEBUG_PRINT("[DEBUG]: [DB] init_remote_db sequence fully complete!\n");
    return 1;
}

// 0.7.4-delta modified to use global mysql connection
void extract_and_save_keywords(AppContext *app, const char *text) {
    if (!text || !app || !app->database.global_db_conn) return;

    mysql_thread_init();
    char *buf = strdup(text);
    char *token = strtok(buf, " ,.!?;:()[]\"");

    pthread_mutex_lock(&app->access.db_mutex);

    while (token != NULL) {
        if (strlen(token) > 3 && (isupper(token[0]) || strpbrk(token, "0123456789-"))) {
            char query[1024];
            char esc_token[256];
            mysql_real_escape_string(app->database.global_db_conn, esc_token, token, strlen(token));

		            snprintf(query, sizeof(query),
                     "INSERT INTO relevance_triggers (keyword, hit_count) "
                     "VALUES ('%s', 1) "
                     "ON DUPLICATE KEY UPDATE hit_count = hit_count + 1, last_used = CURRENT_TIMESTAMP", 
                     esc_token);
            mysql_query(app->database.global_db_conn, query);
        }
        token = strtok(NULL, " ,.!?;:()[]\"");
    }

    pthread_mutex_unlock(&app->access.db_mutex);
    free(buf);
    mysql_thread_end();
}


// Updated load_history_to_gemini
// 0.7.4-delta modified to use global mysql connection
void load_history_to_gemini(AppContext *app, struct json_object *contents_array, const char *current_prompt) {
    if (!app->database.global_db_conn) return;
    int count=0;
    mysql_thread_init();
    pthread_mutex_lock(&app->access.db_mutex);
    DEBUG_PRINT("[DEBUG]: LOAD_HISTORY_TO_GEMINI: Locked DB Mutex\n");
    char *uuid_filter = get_uuid_filter(global_app);


    const char *query = g_strdup_printf(
        "  SELECT role, content FROM aiterm_history "
        "  WHERE session_uuid %s"
        "  ORDER BY created_at DESC LIMIT 100", uuid_filter);

    DEBUG_PRINT("[DEBUG]: LOAD_HISTORY_TO_GEMINI: Query %s\n", query);
    if (mysql_query(app->database.global_db_conn, query) == 0) {
        MYSQL_RES *res = mysql_store_result(app->database.global_db_conn);
        MYSQL_ROW row;
        while ((row = mysql_fetch_row(res))) {
            count++;
            struct json_object *item = json_object_new_object();
            struct json_object *parts_array = json_object_new_array();
            struct json_object *part = json_object_new_object();

            const char* role = strcmp(row[0], "assistant") == 0 ? "model" : "user";

            // Apply noise filter to the data being loaded from the database
            char *data = noise_filter_apply(app, row[1]);

	    // WRAPPING LOGIC:
            // Wrap the database row content in the <history> tag
            if        (strcmp(role, "model") == 0) {
               app->xml.type = TAG_HISTORY;
            } else if (strcmp(role, "user") == 0) {
               app->xml.type = TAG_MEMORY;
            } else {
               app->xml.type = TAG_LOG_DUMP;
            }
            // Added 0.9.5-beta
            char *wrapped_content = g_strdup(xml_wrap(app, data));

            // Old tag code
            //g_strdup_printf("<history session_uuid=\"%s\">\n%s\n</history>",app->session.session_uuid, data);

            json_object_object_add(part, "text", json_object_new_string(wrapped_content));
            json_object_array_add(parts_array, part);
            json_object_object_add(item, "role", json_object_new_string(role));
            json_object_object_add(item, "parts", parts_array);
            json_object_array_add(contents_array, item);
	    g_free(wrapped_content);
        }
        mysql_free_result(res);
    }
    DEBUG_PRINT("[DEBUG]: LOAD_HISTORY_TO_GEMINI: Sent %d rows to AI\n", count);
    pthread_mutex_unlock(&app->access.db_mutex);
    DEBUG_PRINT("[DEBUG]: LOAD_HISTORY_TO_GEMINI: Unlocked DB Mutex\n");
    mysql_thread_end();
}

size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    struct MemoryStruct *mem = (struct MemoryStruct *)userp;

    char *ptr = realloc(mem->memory, mem->size + realsize + 1);
    if(!ptr) return 0;

    mem->memory = ptr;
    memcpy(&(mem->memory[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->memory[mem->size] = 0;

    return realsize;
}

char* strip_ansi(const char *input) {
    if (!input) return NULL;
    size_t len = strlen(input);
    char *output = malloc(len + 1);
    size_t j = 0;
    int in_escape = 0;

    for (size_t i = 0; i < len; i++) {
        if (input[i] == '\033') { // ESC
            in_escape = 1;
            continue;
        }
        if (in_escape) {
            // ANSI CSI sequences start with '[' and end with a char in range 0x40-0x7E
            // We ignore everything between ESC and the final character of the sequence
            if (input[i] >= 0x40 && input[i] <= 0x7E) {
                in_escape = 0;
            }
            continue;
        }
        output[j++] = input[i];
    }
    output[j] = '\0';
    return output;
}

char* extract_ai_text(const char *json_str) {
    if (!json_str) return NULL;

    struct json_object *root = json_tokener_parse(json_str);
    if (!root) return NULL;

    // Added 0.7.5-beta for error reporting
    struct json_object *error_obj, *msg_obj;

    // --- NEW: Trap API Errors (Gemini Format) ---
    if (json_object_object_get_ex(root, "error", &error_obj)) {
        if (json_object_object_get_ex(error_obj, "message", &msg_obj)) {
            const char *error_msg = json_object_get_string(msg_obj);
            // Prefix with "API_ERROR:" so update.c can recognize it
            char *formatted_error = g_strdup_printf("API_ERROR: %s", error_msg);
            json_object_put(root);
            return formatted_error;
        }
    }

    struct json_object *candidates, *candidate, *content, *parts, *part, *text_obj;

    // 1. Try Gemini Format (candidates -> content -> parts -> text)
    if (json_object_object_get_ex(root, "candidates", &candidates)) {
        if (json_object_get_type(candidates) == json_type_array) {
            candidate = json_object_array_get_idx(candidates, 0);
            if (candidate && json_object_object_get_ex(candidate, "content", &content)) {
                if (json_object_object_get_ex(content, "parts", &parts)) {
                    part = json_object_array_get_idx(parts, 0);
                    if (part && json_object_object_get_ex(part, "text", &text_obj)) {
                        char *result = strdup(json_object_get_string(text_obj));
                        json_object_put(root);
                        return result;
                    }
                }
            }
        }
    }

    // 2. Try OpenAI/Internal Format (output array)
    struct json_object *output_array;
    if (json_object_object_get_ex(root, "output", &output_array)) {
        // Ensure it is actually an array before getting length
        if (json_object_get_type(output_array) == json_type_array) {
            int len = json_object_array_length(output_array);
            for (int i = 0; i < len; i++) {
                struct json_object *item = json_object_array_get_idx(output_array, i);
                struct json_object *type_obj, *content_array;
                if (json_object_object_get_ex(item, "type", &type_obj)) {
                    if (strcmp(json_object_get_string(type_obj), "message") == 0) {
                        if (json_object_object_get_ex(item, "content", &content_array)) {
                            int clen = json_object_array_length(content_array);
                            for (int j = 0; j < clen; j++) {
                                struct json_object *c_item = json_object_array_get_idx(content_array, j);
                                struct json_object *t_obj;
                                if (json_object_object_get_ex(c_item, "text", &t_obj)) {
                                    char *result = strdup(json_object_get_string(t_obj));
                                    json_object_put(root);
                                    return result;
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // 3. Cleanup if no matches found
    json_object_put(root);
    return NULL;
}

void save_to_history(const char *user_text, const char *ai_text) {
    extern AppContext *global_app;

    // Apply strip_blank_lines to clean the text before saving
    char *cleaned_user_text = strip_blank_lines(user_text);
    char *cleaned_ai_text = strip_blank_lines(ai_text);

    DBWorkerData *data = malloc(sizeof(DBWorkerData));
    memset(data, 0, sizeof(DBWorkerData));
    data->user_text = g_strdup(cleaned_user_text); // Assign the cleaned text
    data->ai_text = g_strdup(cleaned_ai_text);     // Assign the cleaned text
    data->session_uuid = g_strdup(global_app->session.session_uuid);
    data->is_tee = 0;

    pthread_t thread_id;
    pthread_create(&thread_id, NULL, db_worker_thread, data);
    pthread_detach(thread_id);

    // This part stays in the main thread
    extract_and_save_keywords(global_app, cleaned_ai_text); // Use cleaned text for keywords too
}

void save_tee_to_history(const char *terminal_text, const char *ai_analysis) {
    extern AppContext *global_app;

    if (!terminal_text || !ai_analysis || !global_app->session.session_uuid) {
       DEBUG_PRINT("[DEBUG]: [TEE_SAVE] WARNING: Invalid inputs detected. Aborting.\n");
       return;
    }

    char *terminal_output = noise_filter_apply(global_app, terminal_text);

    mysql_thread_init();
    // Apply strip_blank_lines to clean the text before saving
    char *cleaned_terminal_output = strip_blank_lines(terminal_output);
    char *cleaned_ai_analysis = strip_blank_lines(ai_analysis);
    DEBUG_PRINT("[DEBUG]: [TEE_SAVE] Data cleaned. Allocating DBWorkerData.\n");

    // 1. Pack the data
    DBWorkerData *data = malloc(sizeof(DBWorkerData));
    if (!data) {
        DEBUG_PRINT("[DEBUG]: [TEE_SAVE] FATAL: Malloc failed for DBWorkerData\n");
        return;
    }

    data->terminal_output = cleaned_terminal_output; // Assign the cleaned text
    data->ai_analysis = cleaned_ai_analysis;         // Assign the cleaned text
    data->session_uuid = strdup(global_app->session.session_uuid);
    data->sequence_id = global_app->database.sequence_id;
    global_app->database.sequence_id++;
    data->is_tee = 1;
    data->user_text = NULL;
    data->ai_text = NULL;

    // 3. Launch the worker thread
    DEBUG_PRINT("[DEBUG]: [TEE_SAVE] Launching thread for sequence_id: %d\n", data->sequence_id);
    pthread_t thread_id;
    if (pthread_create(&thread_id, NULL, db_worker_thread, data) != 0) {
        DEBUG_PRINT("[DEBUG]: [TEE_SAVE] FATAL: pthread_create failed!\n");
        // Don't leak memory if thread creation fails
        g_free(cleaned_terminal_output);
        g_free(cleaned_ai_analysis);
        g_free(data->session_uuid); // Assuming it was strdup'd
        free(data);
    } else {
        pthread_detach(thread_id); // Thread cleans up itself
    }
    mysql_thread_end();
}


// updated 0.7.4-delta to use global mysql connection
void load_history_to_api(struct json_object *messages_array) {
    extern AppContext *global_app;
    if (!global_app || !global_app->database.global_db_conn) return;

    mysql_thread_init();
    pthread_mutex_lock(&global_app->access.db_mutex);

    DEBUG_PRINT("[DEBUG] LOAD_HISTORY_TO_API: Locked DB Mutex\n");
    char *uuid_filter = get_uuid_filter(global_app);
    const char *query = g_strdup_printf(
        "SELECT role, content FROM ("
        "  SELECT role, content FROM aiterm_history "
        "  WHERE session_uuid %s AND is_tee = 0 AND sequence_id > %d"
        "  ORDER BY sequence_id DESC LIMIT 100"
        ") AS sub ORDER BY created_at ASC", uuid_filter, global_app->session.last_sent_db_id);

    DEBUG_PRINT("[DEBUG]: LOAD_HISTORY_TO_API: Query %s\n", query);

    if (mysql_query(global_app->database.global_db_conn, query) == 0) {
        MYSQL_RES *res = mysql_store_result(global_app->database.global_db_conn);
        MYSQL_ROW row;

        while ((row = mysql_fetch_row(res))) {
            struct json_object *msg = json_object_new_object();
            json_object_object_add(msg, "role", json_object_new_string(row[0]));
            json_object_object_add(msg, "content", json_object_new_string(row[1]));
            json_object_array_add(messages_array, msg);
        }
        mysql_free_result(res);
    }

    pthread_mutex_unlock(&global_app->access.db_mutex);
    DEBUG_PRINT("[DEBUG] LOAD_HISTORY_TO_API: Unlocked DB Mutex\n");
    mysql_thread_end();
}

// Helper to read file for analysis, Added 0.7.5-beta
char* read_file_to_string(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (fsize <= 0) {
        fclose(f);
        return strdup("");
    }

    char *string = malloc(fsize + 1);
    if (string) {
        size_t read_size = fread(string, 1, fsize, f);
        string[read_size] = 0;
    }
    fclose(f);
    return string;
}

char* extract_cmd_name(const char *input) {
    char *buf = strdup(input);
    char *saveptr;
    char *token = strtok_r(buf, " ", &saveptr);
    char *result = strdup(token ? token : "");
    free(buf);
    return result;
}

// Added 0.8.4-alpha
gboolean is_ai_command(const char *text) {
    if (!text) return FALSE;
    // Check if the AI returned a string containing our command delimiter
    return (strstr(text, "<cmd>") != NULL && strstr(text, "</cmd>") != NULL);
}

char* extract_ai_command(const char *text) {
    char *start = strstr(text, "<cmd>");
    char *end = strstr(text, "</cmd>");
    if (!start || !end || end < start) return NULL;

    start += 5; // Skip "<cmd>"
    size_t len = end - start;
    char *cmd = malloc(len + 1);
    strncpy(cmd, start, len);
    cmd[len] = '\0';
    return cmd;
}

// brief Removes blank lines (empty or containing only whitespace) from a string.
char* strip_blank_lines(const char *input_text) {
    if (!input_text) {
        return NULL;
    }
    if (input_text[0] == '\0') {
        return g_strdup("");
    }
    long in_len = strlen(input_text);
    char *input_string = g_strdup(noise_filter_apply(global_app, input_text));
    int trimmed_count=0;
    GString *output_buffer = g_string_new("");
    char **lines = g_strsplit(input_string, "\n", -1);
    for (int i = 0; lines[i] != NULL; i++) {
        char *current_line = lines[i];
        char *trimmed_line_copy = g_strdup(current_line);
        g_strstrip(trimmed_line_copy);
        if (trimmed_line_copy[0] != '\0') {
            g_string_append(output_buffer, current_line);
            g_string_append_c(output_buffer, '\n');
        } else {
           trimmed_count++;
        }
        g_free(trimmed_line_copy);
    }
    g_strfreev(lines);
    if (output_buffer->len > 0 && output_buffer->str[output_buffer->len - 1] == '\n') {
        g_string_set_size(output_buffer, output_buffer->len - 1);
    }
    DEBUG_PRINT("[DEBUG]: [Blank Lines] Input length: %ld bytes, Output length: %ld bytes. Stripped %d blank lines\n",
          in_len, output_buffer->len, trimmed_count);
    return g_string_free(output_buffer, FALSE);
}

void print_version() {
    printf("aiterm version %-16s\n", AITERM_VERSION);
    printf("Build ID: %s\n", AITERM_BUILDID);
    printf("Build Time: %s\n", AITERM_BUILD_TIME);
}

