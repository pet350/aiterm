// part of aiterm project
// utils.c
// Various utilities used in this project
// By: Peter Talbott
// Assisted by: Gemini
// May 2026

#include <stdlib.h>
#include <string.h>
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

// This is the actual definition where the memory is allocated
HistoryEntry history[5];
int history_count = 0;

// Worker thread function for database initialization
void* init_db_thread_worker(void *data) {
    AppContext *app = (AppContext*)data;

    // CRITICAL: Initialize thread-specific MySQL memory
    mysql_thread_init();

    DEBUG_PRINT("DEBUG: [DB_THREAD] Invoking init_remote_db...\n");
    if (init_remote_db(app)) {
        DEBUG_PRINT("Database setup and persistent connection ready.\n");
    } else {
        DEBUG_PRINT("Database offline. History will not be saved.\n");
    }

    // CRITICAL: Clean up thread-specific MySQL memory
    mysql_thread_end();
    return NULL;
}

void feed_terminal_header(VteTerminal *terminal, const char *msg) {
    char buf[1024];
    snprintf(buf, sizeof(buf), "\r%s[AI Executing]: %s%s\n", ANSI_CYAN, msg, ANSI_RESET);
    vte_terminal_feed(terminal, buf, -1);
}

gboolean check_rate_limit(CURL *curl) {
    long http_code = 0;

    // Extract the HTTP status code from the curl session
    CURLcode res = curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    if (res == CURLE_OK) {
        if (http_code == 429) {
            g_warning("Gemini Free Tier limit reached (HTTP 429).");
            return TRUE;
        }
    } else {
        g_printerr("Failed to get HTTP code: %s\n", curl_easy_strerror(res));
    }

    return FALSE;
}

char* trim_whitespace(char* str) {
    char* end;
    // Trim leading space
    while(isspace((unsigned char)*str)) str++;

    if(*str == 0) return str; // All spaces

    // Trim trailing space
    end = str + strlen(str) - 1;
    while(end > str && isspace((unsigned char)*end)) end--;

    // Write new null terminator character
    end[1] = '\0';

    return str;
}

// Function to save config file
void save_config(AppContext *app) {
    FILE *fp = fopen(CONFIG_FILE, "w");
    if (!fp) {
        DEBUG_PRINT("DEBUG: Error opening aiterm.conf for writing\n");
        return;
    }

    fprintf(fp, "provider=%s\n", app->provider ? app->provider : "gemini");
    fprintf(fp, "model=%s\n", app->model ? app->model : "gemini-flash-latest");
    char *encrypted_api_key = crypt_to_hex(app->api_key ? app->api_key : "", app->master_key);
    if (encrypted_api_key) {
	fprintf(fp, "api_key=%s\n", encrypted_api_key);
	free(encrypted_api_key);
    } else {
	fprintf(fp, "api_key=\n");
    }
    //fprintf(fp, "api_key=%s\n", app->api_key ? app->api_key : "");
    fprintf(fp, "db_host=%s\n", app->db_host ? app->db_host : "localhost");
    fprintf(fp, "db_user=%s\n", app->db_user ? app->db_user : "root");

    // NEW: Encrypt the in-memory plaintext password before writing to file
    char *encrypted_pass = crypt_to_hex(app->db_pass ? app->db_pass : "", app->master_key);
    if (encrypted_pass) {
        fprintf(fp, "db_pass=%s\n", encrypted_pass);
        free(encrypted_pass);
    } else {
        fprintf(fp, "db_pass=\n");
    }

    fprintf(fp, "db_name=%s\n", app->db_name ? app->db_name : "aiterm_db");
    fprintf(fp, "term_transparency=%f\n", app->transparency);
    fprintf(fp, "ai_transparency=%f\n", app->ai_transparency);
    fprintf(fp, "terminal_font=%s\n", app->terminal_font);
    fprintf(fp, "ai_font=%s\n", app->ai_font);
    fprintf(fp, "tee_enabled=%d\n", app->tee_enabled);
    fprintf(fp, "autoreply_enabled=%d\n", app->autoreply_enabled);
    fprintf(fp, "auto_execute_enabled=%d\n", app->auto_execute_enabled);
    fprintf(fp, "ratelimit_enabled=%d\n", app->ratelimit_enabled);

    fclose(fp);
    DEBUG_PRINT("DEBUG: Settings saved to aiterm.conf\n");
}

void load_config(AppContext *app) {
    app->api_key = NULL;
    app->provider = strdup("openai");

    app->terminal_font = strdup("Monospace 10");
    app->ai_font = strdup("Monospace 10");

    FILE *fp = fopen(CONFIG_FILE, "r");
    if (!fp) {
        DEBUG_PRINT("DEBUG: Config file %s not found\n", CONFIG_FILE);
        return;
    }

    char line[1024];
    while (fgets(line, sizeof(line), fp)) {
	line[strcspn(line, "\r\n")] = 0;
	if (strstr(line, "api_key=")) {
		char *val = strchr(line, '=') + 1;
		if (app->api_key) free(app->api_key);
		app->api_key = hex_to_decrypt(val, app->master_key);
		DEBUG_PRINT("DEBUG: [LOADED] API Key\n");
	} else if (strstr(line, "provider=")) {
		char *val = strchr(line, '=') + 1;
		if (app->provider) free(app->provider);
		app->provider = strdup(val);
		DEBUG_PRINT("DEBUG: [LOADED] Provider: [%s]\n", app->provider);
	} else if (strstr(line, "model=")) {
		char *val = strchr(line, '=') + 1;
		if (app->model) free(app->model);
		app->model = strdup(val);
		DEBUG_PRINT("DEBUG: [LOADED] Model: [%s]\n", app->model);
	} else if (strstr(line, "db_host=")) {
		char *val = strchr(line, '=') + 1;
		if (app->db_host) free(app->db_host);
		app->db_host = strdup(val);
		DEBUG_PRINT("DEBUG: [LOADED] DB Host: [%s]\n", app->db_host);
	} else if (strstr(line, "db_user=")) {
		char *val = strchr(line, '=') + 1;
		if (app->db_user) free(app->db_user);
		app->db_user = strdup(val);
		DEBUG_PRINT("DEBUG: [LOADED] DB User: [%s]\n", app->db_user);
	} else if (strstr(line, "db_pass=")) {
		char *val = strchr(line, '=') + 1;
		if (app->db_pass) free(app->db_pass);
		app->db_pass = hex_to_decrypt(val, app->master_key);
		DEBUG_PRINT("DEBUG: [LOADED] DB Password: [xxxxxx]\n");
	} else if (strstr(line, "db_name")) {
		char *val = strchr(line, '=') + 1;
		if (app->db_name) free(app->db_name);
		app->db_name = strdup(val);
		DEBUG_PRINT("DEBUG: [LOADED] DB Name: [%s]\n", app->db_name);
	} else if (strstr(line, "ai_transparency=")) {
		char *val = strchr(line, '=') + 1;
		app->ai_transparency = atof(val);
		if (app->ai_transparency < 0.1) app->ai_transparency = 0.8;
		DEBUG_PRINT("DEBUG: [LOADED] AI transparency: [%f]\n", app->ai_transparency);
	} else if (strstr(line, "term_transparency=")) {
		char *val = strchr(line, '=') + 1;
		app->transparency = atof(val);
		if (app->transparency < 0.1) app->transparency = 0.8;
		DEBUG_PRINT("DEBUG: [LOADED] terminal transparency: [%f]\n", app->transparency);
        } else if (strstr(line, "terminal_font=")) {
		char *val = strchr(line, '=') + 1;
		if (app->terminal_font) free(app->terminal_font);
		app->terminal_font = strdup(val);
		DEBUG_PRINT("DEBUG: [LOADED] terminal font: [%s]\n", app->terminal_font);
	} else if (strstr(line, "ai_font=")) {
		char *val = strchr(line, '=') + 1;
		if (app->ai_font) free(app->ai_font);
		app->ai_font = strdup(val);
		DEBUG_PRINT("DEBUG: [LOADED] AI font: [%s]\n", app->ai_font);
	} else if (strstr(line, "tee_enabled=")) {
		app->tee_enabled = atoi(strchr(line, '=') + 1);
		const char *tee_val = app->tee_enabled ? "ON" : "OFF";
		DEBUG_PRINT("DEBUG: [LOADED] default tee enabled: [%s]\n", tee_val);
	} else if (strstr(line, "autoreply_enabled=")) {
		app->autoreply_enabled = atoi(strchr(line, '=') + 1);
		const char *auto_val = app->autoreply_enabled ? "ON" : "OFF";
		DEBUG_PRINT("DEBUG: [LOADED] default auto reply enabled: [%s]\n", auto_val);
	} else if (strstr(line, "auto_execute_enabled=")) {
		app->auto_execute_enabled = atoi(strchr(line, '=') + 1);
		const char *auto_exec_val = app->auto_execute_enabled ? "ON" : "OFF";
		DEBUG_PRINT("DEBUG: [LOADED] Default auto execute enabled: [%s]\n", auto_exec_val);
	} else if (strstr(line, "ratelimit_enabled=")) {
        	app->ratelimit_enabled = atoi(strchr(line, '=') + 1);
        	DEBUG_PRINT("DEBUG: [LOADED] Rate limit enabled: [%d]\n", app->ratelimit_enabled);
        }
    }
    fclose(fp);
}

// Function to display All History
// Modified 0.7.4-delta to use global mysql connection
// Modified 0.7.5-alpha for autoreply and color responces
void display_all_history(AppContext *app) {
    // 1) FIRST THING: Initialize MySQL for this thread
    mysql_thread_init();

    //DBWorkerData *data = (DBWorkerData *)arg;
    extern AppContext *global_app;

    // If this fails, it safely jumps to cleanup where mysql_thread_end() handles it
    if (!global_app->global_db_conn) goto cleanup;

    // LOCK: Ensure only one thread uses the database pipe at a time
    pthread_mutex_lock(&global_app->db_mutex);
    if (!app->global_db_conn) {
        write_to_ai_pane(app, "System: ", "Database connection is not active.", "cmd_tag", "cmd_tag");
        goto cleanup;
    }

    MYSQL_RES *res;
    MYSQL_ROW row;
    // Query the history table
    const char *query = "SELECT role, content FROM aiterm_history ORDER BY id ASC LIMIT 50";

    if (mysql_query(app->global_db_conn, query)) {
        write_to_ai_pane(app, "System: ", "Error fetching history from database.", "cmd_tag", "cmd_tag");
        goto cleanup;
    }

    res = mysql_store_result(app->global_db_conn);
    if (!res) goto cleanup;

    // Use a GString to build the history output
    GString *history_output = g_string_new("");
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
    g_string_free(history_output, TRUE);
    mysql_free_result(res);

    pthread_mutex_unlock(&global_app->db_mutex);

    mysql_thread_end();
    return;
}

// Modified 0.7.4-delta for global mysql connection
// Rewritten 0.8.4-delta
// Modified 0.7.4-delta for global mysql connection
void* db_worker_thread(void *arg) {
    mysql_thread_init();
    DBWorkerData *data = (DBWorkerData *)arg;
    extern AppContext *global_app;

    if (!global_app->global_db_conn) goto cleanup;

    // LOCK: Ensure only one thread uses the database pipe at a time
    pthread_mutex_lock(&global_app->db_mutex);

    if (data->is_tee) {
        char *esc_out = malloc(strlen(data->terminal_output) * 2 + 1);
        char *esc_ai = malloc(strlen(data->ai_analysis) * 2 + 1);

        mysql_real_escape_string(global_app->global_db_conn, esc_out, data->terminal_output, strlen(data->terminal_output));
        mysql_real_escape_string(global_app->global_db_conn, esc_ai, data->ai_analysis, strlen(data->ai_analysis));

        size_t query_len = strlen(esc_out) + strlen(esc_ai) + strlen(data->session_uuid) + 1024;
        char *query = malloc(query_len);
        if (query) {
            snprintf(query, query_len,
                     "INSERT INTO aiterm_history (role, content, is_tee, session_uuid, sequence_id) VALUES "
                     "('terminal', '%s', 1, '%s', %d), ('assistant', '%s', 1, '%s', %d)",
                     esc_out, data->session_uuid, data->sequence_id, esc_ai, data->session_uuid, data->sequence_id);
            mysql_query(global_app->global_db_conn, query);
            free(query);
        }
        free(esc_out); free(esc_ai);
    } else {
        char *esc_user = malloc(strlen(data->user_text) * 2 + 1);
        char *esc_ai = malloc(strlen(data->ai_text) * 2 + 1);
        mysql_real_escape_string(global_app->global_db_conn, esc_user, data->user_text, strlen(data->user_text));
        mysql_real_escape_string(global_app->global_db_conn, esc_ai, data->ai_text, strlen(data->ai_text));

        size_t query_len = strlen(esc_user) + strlen(esc_ai) + strlen(data->session_uuid) + 512;
        char *query = malloc(query_len);
        if (query) {
	     snprintf(query, query_len,
                     "INSERT INTO aiterm_history (role, content, is_tee, session_uuid, sequence_id) " // ADD sequence_id
                     "VALUES ('user', '%s', 0, '%s', %d), ('assistant', '%s', 0, '%s', %d)",         // ADD %d twice
                     esc_user, data->session_uuid, data->sequence_id, esc_ai, data->session_uuid, data->sequence_id);
            mysql_query(global_app->global_db_conn, query);
            free(query);
        }
        free(esc_user); free(esc_ai);
    }
    // UNLOCK: Let the next thread in
    pthread_mutex_unlock(&global_app->db_mutex);

    cleanup:
    if (data->terminal_output) free(data->terminal_output);
    if (data->ai_analysis) free(data->ai_analysis);
    if (data->user_text) free(data->user_text);
    if (data->ai_text) free(data->ai_text);
    if (data->session_uuid) free(data->session_uuid);
    free(data);
    mysql_thread_end();
    return NULL;
}

// Function ot Initialize MYSQL/MariaDB Database
// Modified 0.7.4-delta to handle global mysql connection
int init_remote_db(AppContext *app) {
    // 1. Initialize the GLOBAL handle
    app->global_db_conn = mysql_init(NULL);
    if (app->global_db_conn == NULL) return 0;

    // === NEW: Set a 3-second connection timeout ===
    unsigned int timeout = 3; // 3 seconds
    mysql_options(app->global_db_conn, MYSQL_OPT_CONNECT_TIMEOUT, (const char *)&timeout);

    my_bool reconnect = 1;
    mysql_options(app->global_db_conn, MYSQL_OPT_RECONNECT, &reconnect);

    // NEW: Set read/write timeouts for queries so they don't hang forever
    // mysql_options(app->global_db_conn, MYSQL_OPT_READ_TIMEOUT, (const char *)&timeout);
    // mysql_options(app->global_db_conn, MYSQL_OPT_WRITE_TIMEOUT, (const char *)&timeout);

    DEBUG_PRINT("DEBUG: [DB] Connecting to %s (timeout: %us)...\n", app->db_host, timeout);
    // 2. Connect to the server (No DB selected yet)
    if (mysql_real_connect(app->global_db_conn, app->db_host, app->db_user, app->db_pass, NULL, 0, NULL, 0) == NULL) {
        DEBUG_PRINT("DB Connection Error: %s\n", mysql_error(app->global_db_conn));
        mysql_close(app->global_db_conn);
        app->global_db_conn = NULL;
        return 0;
    }
    DEBUG_PRINT("DEBUG: [DB] Successfully connected to database host.\n");

    // 3. Create and Select Database
    char db_query[256];
    snprintf(db_query, sizeof(db_query), "CREATE DATABASE IF NOT EXISTS %s", app->db_name);

    DEBUG_PRINT("DEBUG: [DB] Executing: %s\n", db_query);
    mysql_query(app->global_db_conn, db_query);

    DEBUG_PRINT("DEBUG: [DB] Selecting database: %s\n", app->db_name);
    mysql_select_db(app->global_db_conn, app->db_name);

    // 4. Create History Table
    DEBUG_PRINT("DEBUG: [DB] Executing: CREATE TABLE IF NOT EXISTS aiterm_history\n");
    mysql_query(app->global_db_conn, "CREATE TABLE IF NOT EXISTS aiterm_history (id INT AUTO_INCREMENT PRIMARY KEY)");

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
        DEBUG_PRINT("DEBUG: [DB] Running migration query [%d]: %s\n", i, migrations[i]);
        mysql_query(app->global_db_conn, migrations[i]);
    }

    // 6. Setup Triggers Table
    const char *trigger_table_query =
        "CREATE TABLE IF NOT EXISTS relevance_triggers ("
        "id INT AUTO_INCREMENT PRIMARY KEY, "
        "keyword VARCHAR(50) UNIQUE, "
        "hit_count INT DEFAULT 1, "
        "last_used TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON DUPLICATE KEY UPDATE last_used=CURRENT_TIMESTAMP)";
    DEBUG_PRINT("DEBUG: [DB] Executing: CREATE TABLE IF NOT EXISTS relevance_triggers\n");
    mysql_query(app->global_db_conn, trigger_table_query);

    const char *command_policy_table =
	"CREATE TABLE IF NOT EXISTS command_policies ("
	"command VARCHAR(256) PRIMARY KEY, "
	"type VARCHAR(32) NOT NULL, "
	"risk_level VARCHAR(32) NOT NULL, "
	"updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON DUPLICATE KEY UPDATE updated_at=CURRENT_TIMESTAMP"
	")";
    DEBUG_PRINT("DEBUG: [DB] Executing: CREATE TABLE IF NOT EXISTS command_policies\n");
    mysql_query(app->global_db_conn, command_policy_table);


    DEBUG_PRINT("DEBUG: [DB] init_remote_db sequence fully complete!\n");
    return 1;
}

char* strip_prompt(const char *input) {
    if (!input) return NULL;
    char *last_delim = strrchr(input, '$');
    if (!last_delim) last_delim = strrchr(input, '#');

    if (last_delim) {
        char *start = last_delim + 1;
        // More aggressive cleaning of prompt leftovers
        while (*start && (isspace(*start) || *start == ']' || *start == '>' || *start == '$')) {
            start++;
        }
        return strdup(start);
    }
    return strdup(input);
}

// 0.7.4-delta modified to use global mysql connection
void extract_and_save_keywords(AppContext *app, const char *text) {
    if (!text || !app || !app->global_db_conn) return;

    mysql_thread_init();
    char *buf = strdup(text);
    char *token = strtok(buf, " ,.!?;:()[]\"");

    pthread_mutex_lock(&app->db_mutex);

    while (token != NULL) {
        if (strlen(token) > 3 && (isupper(token[0]) || strpbrk(token, "0123456789-"))) {
            char query[1024];
            char esc_token[256];
            mysql_real_escape_string(app->global_db_conn, esc_token, token, strlen(token));

		            snprintf(query, sizeof(query),
                     "INSERT INTO relevance_triggers (keyword, hit_count) "
                     "VALUES ('%s', 1) "
                     "ON DUPLICATE KEY UPDATE hit_count = hit_count + 1, last_used = CURRENT_TIMESTAMP", 
                     esc_token);
            mysql_query(app->global_db_conn, query);
        }
        token = strtok(NULL, " ,.!?;:()[]\"");
    }

    pthread_mutex_unlock(&app->db_mutex);
    free(buf);
    mysql_thread_end();
}

// 0.7.3-beta: The Smart Retrieval Engine (Global Magic Version)
// 0.7.4-delta modified to use global mysql connection
void load_smart_history(AppContext *app, struct json_object *target_array, const char *current_prompt, int is_gemini) {
    if (!app->global_db_conn) return;

    mysql_thread_init();
    pthread_mutex_lock(&app->db_mutex);

    const char *query =
        "SELECT role, content FROM ("
        "  SELECT role, content, created_at FROM aiterm_history "
        "  WHERE is_tee = 0 "
        "  ORDER BY created_at DESC LIMIT 100"
        ") AS sub ORDER BY created_at ASC";

    if (mysql_query(app->global_db_conn, query) == 0) {
        MYSQL_RES *res = mysql_store_result(app->global_db_conn);
        MYSQL_ROW row;
        while ((row = mysql_fetch_row(res))) {
            struct json_object *msg = json_object_new_object();
            json_object_object_add(msg, "role", json_object_new_string(row[0]));
            json_object_object_add(msg, "content", json_object_new_string(row[1]));
            json_object_array_add(target_array, msg);
        }
        mysql_free_result(res);
    }

    pthread_mutex_unlock(&app->db_mutex);
    mysql_thread_end();
}

// Updated load_history_to_gemini
// 0.7.4-delta modified to use global mysql connection
void load_history_to_gemini(AppContext *app, struct json_object *contents_array, const char *current_prompt) {
    if (!app->global_db_conn) return;

    mysql_thread_init();
    pthread_mutex_lock(&app->db_mutex);

    const char *query =
        "SELECT role, content FROM ("
        "  SELECT role, content, created_at FROM aiterm_history "
        "  WHERE is_tee = 0 "
        "  ORDER BY created_at DESC LIMIT 100"
        ") AS sub ORDER BY created_at ASC";

    if (mysql_query(app->global_db_conn, query) == 0) {
        MYSQL_RES *res = mysql_store_result(app->global_db_conn);
        MYSQL_ROW row;
        while ((row = mysql_fetch_row(res))) {
            struct json_object *item = json_object_new_object();
            struct json_object *parts_array = json_object_new_array();
            struct json_object *part = json_object_new_object();

            const char* role = strcmp(row[0], "assistant") == 0 ? "model" : "user";

	    // WRAPPING LOGIC:
            // Wrap the database row content in the <history> tag
            char *wrapped_content = g_strdup_printf(
		"<history session_uuid=\"%s\">\n%s\n</history>",
		app->session.session_uuid, row[1]);

            json_object_object_add(part, "text", json_object_new_string(wrapped_content));
            json_object_array_add(parts_array, part);
            json_object_object_add(item, "role", json_object_new_string(role));
            json_object_object_add(item, "parts", parts_array);
            json_object_array_add(contents_array, item);
	    g_free(wrapped_content);
        }
        mysql_free_result(res);
    }
    pthread_mutex_unlock(&app->db_mutex);
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
    data->user_text = cleaned_user_text; // Assign the cleaned text
    data->ai_text = cleaned_ai_text;     // Assign the cleaned text
    data->session_uuid = strdup(global_app->session_uuid);
    data->is_tee = 0;

    pthread_t thread_id;
    pthread_create(&thread_id, NULL, db_worker_thread, data);
    pthread_detach(thread_id);

    // This part stays in the main thread
    extract_and_save_keywords(global_app, cleaned_ai_text); // Use cleaned text for keywords too
}

void save_tee_to_history(const char *terminal_output, const char *ai_analysis) {
    extern AppContext *global_app;

    if (!terminal_output || !ai_analysis || !global_app->session_uuid) return;

    mysql_thread_init();
    // Apply strip_blank_lines to clean the text before saving
    char *cleaned_terminal_output = strip_blank_lines(terminal_output);
    char *cleaned_ai_analysis = strip_blank_lines(ai_analysis);

    // 1. Pack the data
    DBWorkerData *data = malloc(sizeof(DBWorkerData));
    data->terminal_output = cleaned_terminal_output; // Assign the cleaned text
    data->ai_analysis = cleaned_ai_analysis;         // Assign the cleaned text
    data->session_uuid = strdup(global_app->session_uuid);
    data->sequence_id = global_app->sequence_id;
    global_app->sequence_id++;
    data->is_tee = 1;
    data->user_text = NULL;
    data->ai_text = NULL;


    // 3. Launch the worker thread
    pthread_t thread_id;
    if (pthread_create(&thread_id, NULL, db_worker_thread, data) != 0) {
        DEBUG_PRINT("Failed to create DB thread\n");
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
    if (!global_app || !global_app->global_db_conn) return;

    mysql_thread_init();
    pthread_mutex_lock(&global_app->db_mutex);

    const char *query = "SELECT role, content FROM aiterm_history WHERE is_tee = 0 ORDER BY created_at DESC LIMIT 100";

    if (mysql_query(global_app->global_db_conn, query) == 0) {
        MYSQL_RES *res = mysql_store_result(global_app->global_db_conn);
        MYSQL_ROW row;

        while ((row = mysql_fetch_row(res))) {
            struct json_object *msg = json_object_new_object();
            json_object_object_add(msg, "role", json_object_new_string(row[0]));
            json_object_object_add(msg, "content", json_object_new_string(row[1]));
            json_object_array_add(messages_array, msg);
        }
        mysql_free_result(res);
    }

    pthread_mutex_unlock(&global_app->db_mutex);
    mysql_thread_end();
}

// updated 0.8.3 send status to AI as well as display it
// modified 0.8.4 display status in color
void display_status(AppContext *app) {
    // Lock the database mutex to ensure thread-safety during the status check
    mysql_thread_init();
    pthread_mutex_lock(&app->db_mutex);

    gboolean is_connected = FALSE;
    int ping_res = -1;

    DEBUG_PRINT("DEBUG: [STATUS] Checking database connection status...\n");
    DEBUG_PRINT("DEBUG: [STATUS] app->global_db_conn pointer value: %p\n", (void*)app->global_db_conn);

    if (app->global_db_conn != NULL) {
        // mysql_ping returns 0 if the connection is alive
        ping_res = mysql_ping(app->global_db_conn);
        DEBUG_PRINT("DEBUG: [STATUS] mysql_ping returned: %d\n", ping_res);
        if (ping_res == 0) {
            is_connected = TRUE;
        } else {
            DEBUG_PRINT("DEBUG: [STATUS] mysql_ping failed error: %s\n", mysql_error(app->global_db_conn));
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

    int db_ok = (app->global_db_conn && mysql_ping(app->global_db_conn) == 0);
    g_string_append_printf(status_report, "Database:\t%s\n", db_ok ? "CONNECTED" : "DISCONNECTED");

    const char *mysql_ping_val = is_connected ? "ON" : "OFF";
    g_string_append_printf(status_report, "MariaDB Ping Results:\t%s\n", mysql_ping_val);

    int ai_ok = (app->api_key && strlen(app->api_key) > 0);
    g_string_append_printf(status_report, "AI Status:\t%s\n", ai_ok ? "READY" : "MISSING CONFIG");

    g_string_append_printf(status_report, "Session UUID:\t%s\n", app->session_uuid ? app->session_uuid : "N/A");
    g_string_append(status_report, "---------------------");

    // 2. Display to the User in the AI Pane with granular coloring
    append_ai_text(app, "[ Local Status ]\n", "cmd_tag");
    append_ai_text(app, "--- SYSTEM STATUS ---\n", "body_tag");

    // Tee Logging Row
    append_ai_text(app, "Tee Logging:\t", "body_tag");
    append_ai_text(app, tee_val, app->tee_enabled ? "ai_tag" : "cmd_tag");
    append_ai_text(app, "\n", "body_tag");

    // Autoreply Row
    append_ai_text(app, "Autoreply:\t", "body_tag");
    append_ai_text(app, auto_val, app->autoreply_enabled ? "ai_tag" : "cmd_tag");
    append_ai_text(app, "\n", "body_tag");

    // Auto Execute Row
    append_ai_text(app, "Auto Execute:\t", "body_tag");
    append_ai_text(app, auto_exec_val, app->auto_execute_enabled ? "ai_tag" : "cmd_tag");
    append_ai_text(app, "\n", "body_tag");

    // Ratelimit Row
    append_ai_text(app, "Ratelimit:\t", "body_tag");
    append_ai_text(app, ratelimit_val, app->ratelimit_enabled ? "ai_tag" : "cmd_tag");
    append_ai_text(app, "\n", "body_tag");

    // mysql ping rresults
    append_ai_text(app, "MYSQL Server:\t", "body_tag");
    append_ai_text(app, mysql_ping_val ? "ALIVE" : "DEAD", mysql_ping_val ? "ai_tag" : "cmd_tag");
    append_ai_text(app, "\n", "body_tag");

    // Database Row
    append_ai_text(app, "Database:\t", "body_tag");
    append_ai_text(app, db_ok ? "CONNECTED" : "DISCONNECTED", db_ok ? "ai_tag" : "cmd_tag");
    append_ai_text(app, "\n", "body_tag");

    // AI Status Row
    append_ai_text(app, "AI Status:\t", "body_tag");
    append_ai_text(app, ai_ok ? "READY" : "MISSING CONFIG", ai_ok ? "ai_tag" : "cmd_tag");
    append_ai_text(app, "\n", "body_tag");

    // Session UUID Row
    append_ai_text(app, "Session UUID:\t", "body_tag");
    append_ai_text(app, app->session_uuid ? app->session_uuid : "N/A", "body_tag");
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

// added 0.8.4-alpha
char* copyString(const char *s) {
    if (!s) return NULL;
    char *res = malloc(strlen(s) + 1);
    if (res) strcpy(res, s);
    return res;
}

/*
 * @brief Removes blank lines (empty or containing only whitespace) from a string.
 *
 * This function iterates through the input string line by line. If a line
 * is found to be empty or contain only whitespace characters, it is
 * discarded. Non-blank lines are retained with their original content
 * and internal whitespace (including leading/trailing on non-blank lines).
 *
 * @param input_string The original string potentially containing blank lines.
 * @return A new Glib-allocated string with blank lines removed. The caller
 *         is responsible for freeing this string using g_free(). Returns NULL
 *         if input_string is NULL. Returns an empty string if input_string
 *         contains only blank lines or is empty.
 */
char* strip_blank_lines(const char *input_string) {
    if (!input_string) {
        return NULL;
    }
    int trimmed_count=0;

    // If the input is an empty string, return an empty string
    if (input_string[0] == '\0') {
        return g_strdup("");
    }

    GString *output_buffer = g_string_new("");
    char **lines = g_strsplit(input_string, "\n", -1); // Split by newline, -1 means no limit

    for (int i = 0; lines[i] != NULL; i++) {
        char *current_line = lines[i];

        // Create a temporary mutable copy for trimming to check if it's blank.
        // g_strstrip modifies in place, so we need a copy to not alter original line array.
        char *trimmed_line_copy = g_strdup(current_line);
        g_strstrip(trimmed_line_copy); // Remove leading/trailing whitespace from the copy

        // If the trimmed copy is not empty, then the original line was not blank.
        if (trimmed_line_copy[0] != '\0') {
            // Append the original line (with its original whitespace) and a newline
            g_string_append(output_buffer, current_line);
            g_string_append_c(output_buffer, '\n');
        } else {
           trimmed_count++;
        }
        g_free(trimmed_line_copy); // Free the temporary trimmed copy
    }

    g_strfreev(lines); // Free the array of lines returned by g_strsplit

    // Remove the trailing newline character if any lines were appended.
    // This prevents an extra blank line at the end if the last actual line wasn't blank.
    if (output_buffer->len > 0 && output_buffer->str[output_buffer->len - 1] == '\n') {
        g_string_set_size(output_buffer, output_buffer->len - 1);
    }
    DEBUG_PRINT("[DEBUG] Stripped %d blank lines\n", trimmed_count);
    return g_string_free(output_buffer, FALSE); // Return the C string and free the GString object
}
