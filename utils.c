// utils.c
// Part of the aiterm project
// C Program file for defining functions used in various parts of this application
// By: Peter Talbott
// With assistance from Gemini and OpenAI
// May 2026

#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <json-c/json.h>
#include "utils.h"
#include "gui.h"
#include "openai.h"
#include <mariadb/mysql.h>
#include "crypto.h" // Add this include
#include "update.h"

// This is the actual definition where the memory is allocated
HistoryEntry history[5];
int history_count = 0;

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
    fprintf(fp, "api_key=%s\n", app->api_key ? app->api_key : "");
    fprintf(fp, "db_host=%s\n", app->db_host ? app->db_host : "localhost");
    fprintf(fp, "db_user=%s\n", app->db_user ? app->db_user : "root");

    // NEW: Encrypt the in-memory plaintext password before writing to file
    char *encrypted_pass = crypt_to_hex(app->db_pass ? app->db_pass : "");
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
            app->api_key = strdup(val);
            DEBUG_PRINT("DEBUG: Loaded API Key\n");
        } else if (strstr(line, "provider=")) {
            char *val = strchr(line, '=') + 1;
            if (app->provider) free(app->provider);
            app->provider = strdup(val);
            DEBUG_PRINT("DEBUG: Loaded Provider: [%s]\n", app->provider);
        } else if (strstr(line, "model=")) {
            char *val = strchr(line, '=') + 1;
            if (app->model) free(app->model);
            app->model = strdup(val);
            DEBUG_PRINT("DEBUG: Loaded Model: [%s]\n", app->model);
        } else if (strstr(line, "db_host=")) {
	    char *val = strchr(line, '=') + 1;
	    if (app->db_host) free(app->db_host);
	    app->db_host = strdup(val);
	    DEBUG_PRINT("DEBUG: Loaded DB Host: [%s]\n", app->db_host);
        } else if (strstr(line, "db_user=")) {
	    char *val = strchr(line, '=') + 1;
	    if (app->db_user) free(app->db_user);
	    app->db_user = strdup(val);
	    DEBUG_PRINT("DEBUG: Loaded DB User: [%s]\n", app->db_user);
	} else if (strstr(line, "db_pass=")) {
	    char *val = strchr(line, '=') + 1;
	    if (app->db_pass) free(app->db_pass);
	    app->db_pass = hex_to_decrypt(val);
	    DEBUG_PRINT("DEBUG: Loaded DB Password: [xxxxxx]\n");
	} else if (strstr(line, "db_name")) {
	    char *val = strchr(line, '=') + 1;
	    if (app->db_name) free(app->db_name);
	    app->db_name = strdup(val);
	    DEBUG_PRINT("DEBUG: Loaded DB Name: [%s]\n", app->db_name);
	} else if (strstr(line, "ai_transparency=")) {
            char *val = strchr(line, '=') + 1;
            app->ai_transparency = atof(val);
            if (app->ai_transparency < 0.1) app->ai_transparency = 0.8;
	    DEBUG_PRINT("DEBUG: Loaded AI transparency: [%f]\n", app->ai_transparency);
        } else if (strstr(line, "term_transparency=")) {
            char *val = strchr(line, '=') + 1;
	    app->transparency = atof(val);
	    if (app->transparency < 0.1) app->transparency = 0.8;
	    DEBUG_PRINT("DEBUG: Loaded terminal transparency: [%f]\n", app->transparency);
    	} else if (strstr(line, "terminal_font=")) {
	    char *val = strchr(line, '=') + 1;
	    if (app->terminal_font) free(app->terminal_font);
	    app->terminal_font = strdup(val);
	    DEBUG_PRINT("DEBUG: Loaded terminal font: [%s]\n", app->terminal_font);
	} else if (strstr(line, "ai_font=")) {
	    char *val = strchr(line, '=') + 1;
	    if (app->ai_font) free(app->ai_font);
	    app->ai_font = strdup(val);
	    DEBUG_PRINT("DEBUG: Loaded AI font: [%s]\n", app->ai_font);
	} else if (strstr(line, "tee_enabled=")) {
            app->tee_enabled = atoi(strchr(line, '=') + 1);
	} else if (strstr(line, "autoreply_enabled=")) {
          app->autoreply_enabled = atoi(strchr(line, '=') + 1);
        }
    }
    fclose(fp);
}

// Function to display All History
// Modified 0.7.4-delta to use global mysql connection
// Modified 0.7.5-alpha for autoreply and color responces
void display_all_history(AppContext *app) {
    if (!app->global_db_conn) {
        write_to_ai_pane(app, "System: ", "Database connection is not active.", "cmd_tag", "cmd_tag");
        return;
    }

    MYSQL_RES *res;
    MYSQL_ROW row;
    // Query the history table
    const char *query = "SELECT role, content FROM aiterm_history ORDER BY id ASC LIMIT 50";

    if (mysql_query(app->global_db_conn, query)) {
        write_to_ai_pane(app, "System: ", "Error fetching history from database.", "cmd_tag", "cmd_tag");
        return;
    }

    res = mysql_store_result(app->global_db_conn);
    if (!res) return;

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

    g_string_free(history_output, TRUE);
    mysql_free_result(res);
}

// Modified 0.7.4-delta for global mysql connection
void* db_worker_thread(void *arg) {
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
            //snprintf(query, query_len,
            //         "INSERT INTO aiterm_history (role, content, is_tee, session_uuid) "
            //         "VALUES ('user', '%s', 0, '%s'), ('assistant', '%s', 0, '%s')",
            //         esc_user, data->session_uuid, esc_ai, data->session_uuid);
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
    return NULL;
}

// Function ot Initialize MYSQL/MariaDB Database
// Modified 0.7.4-delta to handle global mysql connection
int init_remote_db(AppContext *app) {
    // 1. Initialize the GLOBAL handle
    app->global_db_conn = mysql_init(NULL);
    if (app->global_db_conn == NULL) return 0;

    // 2. Connect to the server (No DB selected yet)
    if (mysql_real_connect(app->global_db_conn, app->db_host, app->db_user, app->db_pass, NULL, 0, NULL, 0) == NULL) {
        DEBUG_PRINT("DB Connection Error: %s\n", mysql_error(app->global_db_conn));
        mysql_close(app->global_db_conn);
        app->global_db_conn = NULL;
        return 0;
    }

    // 3. Create and Select Database
    char db_query[256];
    snprintf(db_query, sizeof(db_query), "CREATE DATABASE IF NOT EXISTS %s", app->db_name);
    mysql_query(app->global_db_conn, db_query);
    mysql_select_db(app->global_db_conn, app->db_name);

    // 4. Create History Table
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
        mysql_query(app->global_db_conn, migrations[i]);
    }

    // 6. Setup Triggers Table
    const char *trigger_table_query =
        "CREATE TABLE IF NOT EXISTS relevance_triggers ("
        "id INT AUTO_INCREMENT PRIMARY KEY, "
        "keyword VARCHAR(50) UNIQUE, "
        "hit_count INT DEFAULT 1, "
        "last_used TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON DUPLICATE KEY UPDATE last_used=CURRENT_TIMESTAMP)";

    mysql_query(app->global_db_conn, trigger_table_query);

    // CRITICAL: Connection stays open in app->global_db_conn
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
}

// 0.7.3-beta: The Smart Retrieval Engine (Global Magic Version)
// 0.7.4-delta modified to use global mysql connection
void load_smart_history(AppContext *app, struct json_object *target_array, const char *current_prompt, int is_gemini) {
    if (!app->global_db_conn) return;

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
}

// Updated load_history_to_gemini
// 0.7.4-delta modified to use global mysql connection
void load_history_to_gemini(AppContext *app, struct json_object *contents_array, const char *current_prompt) {
    if (!app->global_db_conn) return;

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

            json_object_object_add(part, "text", json_object_new_string(row[1]));
            json_object_array_add(parts_array, part);
            json_object_object_add(item, "role", json_object_new_string(role));
            json_object_object_add(item, "parts", parts_array);
            json_object_array_add(contents_array, item); 
        }
        mysql_free_result(res);
    }
    pthread_mutex_unlock(&app->db_mutex);
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

// updated 0.7.4-delta to use global mysql connection
void save_to_history(const char *user_text, const char *ai_text) {
    extern AppContext *global_app;

    DBWorkerData *data = malloc(sizeof(DBWorkerData));
    memset(data, 0, sizeof(DBWorkerData));
    data->user_text = strdup(user_text);
    data->ai_text = strdup(ai_text);
    data->session_uuid = strdup(global_app->session_uuid);
    data->is_tee = 0;

    pthread_t thread_id;
    pthread_create(&thread_id, NULL, db_worker_thread, data);
    pthread_detach(thread_id);

    // This part stays in the main thread
    extract_and_save_keywords(global_app, ai_text);
}

void save_tee_to_history(const char *terminal_output, const char *ai_analysis) {
    extern AppContext *global_app;

    if (!terminal_output || !ai_analysis || !global_app->session_uuid) return;

    // 1. Pack the data
    DBWorkerData *data = malloc(sizeof(DBWorkerData));
    data->terminal_output = strdup(terminal_output);
    data->ai_analysis = strdup(ai_analysis);
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
    } else {
        pthread_detach(thread_id); // Thread cleans up itself
    }
}

// updated 0.7.4-delta to use global mysql connection
void load_history_to_api(struct json_object *messages_array) {
    extern AppContext *global_app;
    if (!global_app || !global_app->global_db_conn) return;

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
}

// updated 0.8.3 send status to AI as well as display it
void display_status(AppContext *app) {
    // 1. Build a single string containing the whole status report
    GString *status_report = g_string_new("--- SYSTEM STATUS ---\n");

    const char *tee_val = app->tee_enabled ? "ON" : "OFF";
    g_string_append_printf(status_report, "Tee Logging:  %s\n", tee_val);

    const char *auto_val = app->autoreply_enabled ? "ON" : "OFF";
    g_string_append_printf(status_report, "Autoreply:    %s\n", auto_val);

    int db_ok = (app->global_db_conn && mysql_ping(app->global_db_conn) == 0);
    g_string_append_printf(status_report, "Database:     %s\n", db_ok ? "CONNECTED" : "DISCONNECTED");

    int ai_ok = (app->api_key && strlen(app->api_key) > 0);
    g_string_append_printf(status_report, "AI Status:    %s\n", ai_ok ? "READY" : "MISSING CONFIG");

    g_string_append_printf(status_report, "Session UUID: %s\n", app->session_uuid ? app->session_uuid : "N/A");
    g_string_append(status_report, "---------------------");

    // 2. Display to the User in the AI Pane
    write_to_ai_pane(app, "[ Local Status ]\n", status_report->str, "cmd_tag", "body_tag");

    // 3. THE NEW PART: Send to Tee Handler so the AI "sees" it
    // We call tee_handle_output just like the terminal does
    if (app->tee_enabled) {
        tee_handle_output(app, status_report->str);
        // We add a small newline to the buffer to keep the AI history clean
        tee_handle_output(app, "\n");
        // --- THE FIX ---
        // This manually "pours the bucket" into the AI thread immediately
        tee_flush_timed(app); 
    }

    // Cleanup
    g_string_free(status_report, TRUE);
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


