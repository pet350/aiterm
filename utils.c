#include <stdlib.h>
#include <string.h>
#include <json-c/json.h>
#include "utils.h"
#include "gui.h"
#include "openai.h"
#include <mariadb/mysql.h>

// Function to display All History
void display_all_history(AppContext *app) {
    MYSQL *conn = mysql_init(NULL);
    if (mysql_real_connect(conn, app->db_host, app->db_user, app->db_pass, app->db_name, 0, NULL, 0)) {
        mysql_query(conn, "SELECT created_at, role, content FROM aiterm_history ORDER BY id ASC");
        MYSQL_RES *res = mysql_store_result(conn);
        MYSQL_ROW row;

        while ((row = mysql_fetch_row(res))) {
            char entry[10240];
            snprintf(entry, sizeof(entry), "[%s] %s: %s", row[0], row[1], row[2]);
            append_to_view(app->gemini_view, NULL, entry);
        }
        mysql_free_result(res);
        mysql_close(conn);
    }
}

// Function ot Initialize MYSQL/MariaDB Database
void init_remote_db(AppContext *app) {
    MYSQL *conn = mysql_init(NULL);
    if (conn == NULL) {
        DEBUG_PRINT("Error: mysql_init() failed\n");
        return;
    }

    // 1. Connect to the host (leaving db name NULL for now)
    if (mysql_real_connect(conn, app->db_host, app->db_user, app->db_pass, NULL, 0, NULL, 0) == NULL) {
        DEBUG_PRINT("Error: Connection to %s failed: %s\n", app->db_host, mysql_error(conn));
        mysql_close(conn);
        return;
    }

    // 2. Create the Database
    char db_query[256];
    snprintf(db_query, sizeof(db_query), "CREATE DATABASE IF NOT EXISTS %s", app->db_name);
    if (mysql_query(conn, db_query)) {
        DEBUG_PRINT("Error creating database: %s\n", mysql_error(conn));
    }

    // 3. Select the Database
    mysql_select_db(conn, app->db_name);

    // 4. Create the History Table (with Tee data support)
    const char *table_query = 
        "CREATE TABLE IF NOT EXISTS aiterm_history ("
        "id INT AUTO_INCREMENT PRIMARY KEY, "
        "role VARCHAR(20), "
        "content TEXT, "
        "is_tee TINYINT(1) DEFAULT 0, "
        "created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP)";

    if (mysql_query(conn, table_query)) {
        DEBUG_PRINT("Error creating table: %s\n", mysql_error(conn));
    }

    mysql_close(conn);
    DEBUG_PRINT("DEBUG: Remote MariaDB initialized successfully on %s\n", app->db_host);
}

// This is the actual definition where the memory is allocated
HistoryEntry history[5];
int history_count = 0;

char* strip_prompt(const char *input) {
    if (!input) return NULL;

    // Look for the last occurrence of '#' (root) or '$' (user)
    char *last_hash = strrchr(input, '#');
    char *last_dollar = strrchr(input, '$');

    char *start = NULL;

    // Determine which delimiter comes last in the string
    if (last_hash > last_dollar) {
        start = last_hash;
    } else {
        start = last_dollar;
    }

    // If we found a prompt delimiter, move pointer past it and any following space
    if (start) {
        start++; // Move past # or $
        while (*start == ' ') start++; // Skip leading spaces
        return strdup(start);
    }

    // If no prompt found, just return the original string
    return strdup(input);
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
    char *output = malloc(strlen(input) + 1);
    char *ptr = output;
    int in_escape = 0;

    for (int i = 0; input[i]; i++) {
        if (input[i] == '\033') { // ESC character
            in_escape = 1;
        } else if (in_escape) {
            if ((input[i] >= 'A' && input[i] <= 'Z') || (input[i] >= 'a' && input[i] <= 'z')) {
                in_escape = 0; // End of escape sequence
            }
        } else {
            *ptr++ = input[i];
        }
    }
    *ptr = '\0';
    return output;
}

char* extract_ai_text(const char *json_str) {
    if (!json_str) return NULL;
    
    struct json_object *root = json_tokener_parse(json_str);
    if (!root) return NULL;

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

void load_config(AppContext *app) {
    // 1. Set safe defaults first!
    app->api_key = NULL;
    app->provider = strdup("openai"); 

    FILE *fp = fopen("aiterm.conf", "r");
    if (!fp) {
        DEBUG_PRINT("DEBUG: Config file aiterm.conf not found\n");
        return;
    }

    char line[1024];
    while (fgets(line, sizeof(line), fp)) {
        line[strcspn(line, "\r\n")] = 0; // Clean newline

        // Use strstr for more flexible matching
        if (strstr(line, "api_key=")) {
            char *val = strchr(line, '=') + 1;
            if (app->api_key) free(app->api_key);
            app->api_key = strdup(val);
            DEBUG_PRINT("DEBUG: Loaded API Key\n");
        } 
        else if (strstr(line, "provider=")) {
            char *val = strchr(line, '=') + 1;
            if (app->provider) free(app->provider);
            app->provider = strdup(val);
            DEBUG_PRINT("DEBUG: Loaded Provider: [%s]\n", app->provider);
        }
            // --- ADD THIS BLOCK ---
        else if (strstr(line, "model=")) {
           char *val = strchr(line, '=') + 1;
           if (app->model) free(app->model);
           app->model = strdup(val);
           DEBUG_PRINT("DEBUG: Loaded Model: [%s]\n", app->model);
        }
    	// Inside load_config in utils.c
	    else if (strstr(line, "db_host=")) {
	        char *val = strchr(line, '=') + 1;
	        if (app->db_host) free(app->db_host);
	        app->db_host = strdup(val);
	        DEBUG_PRINT("DEBUG: Loaded DB Host: [%s]\n", app->db_host);
	    }
	    else if (strstr(line, "db_user=")) {
	        char *val = strchr(line, '=') + 1;
	        if (app->db_user) free(app->db_user);
	        app->db_user = strdup(val);
	        DEBUG_PRINT("DEBUG: Loaded DB User: [%s]\n", app->db_user);
	    }  
	    else if (strstr(line, "db_pass=")) {
	        char *val = strchr(line, '=') + 1;
	        if (app->db_pass) free(app->db_pass);
	        app->db_pass = strdup(val);
	        DEBUG_PRINT("DEBUG: Loaded DB Password: [xxxxxx]\n");
	    }
	    else if (strstr(line, "db_name")) {
	        char *val = strchr(line, '=') + 1;
	        if (app->db_name) free(app->db_name);
	        app->db_name = strdup(val);
	        DEBUG_PRINT("DEBUG: Loaded DB Name: [%s]\n", app->db_name);
	    }  
	// ... repeat for db_pass and db_name ...
    }
    fclose(fp);
}

void save_to_history(const char *user_text, const char *ai_text) {
    extern AppContext *global_app;
    MYSQL *conn = mysql_init(NULL);

    if (mysql_real_connect(conn, global_app->db_host, global_app->db_user, 
                           global_app->db_pass, global_app->db_name, 0, NULL, 0)) {
        
        // Allocate enough space for escaped strings (2x + 1 is the standard)
        char *esc_user = malloc(strlen(user_text) * 2 + 1);
        char *esc_ai = malloc(strlen(ai_text) * 2 + 1);
        
        mysql_real_escape_string(conn, esc_user, user_text, strlen(user_text));
        mysql_real_escape_string(conn, esc_ai, ai_text, strlen(ai_text));

        char query[16384]; // Terminal/AI text can be large
        snprintf(query, sizeof(query), 
                 "INSERT INTO aiterm_history (role, content, is_tee) VALUES ('user', '%s', 0), ('assistant', '%s', 0)",
                 esc_user, esc_ai);

        if (mysql_query(conn, query)) {
            DEBUG_PRINT("DB Insert Error: %s\n", mysql_error(conn));
        }

        free(esc_user);
        free(esc_ai);
        mysql_close(conn);
    }
}
// Old flat file function
//void save_to_history(const char *user_text, const char *ai_text) {
//    char path[256];
//    snprintf(path, sizeof(path), "%s/.aiterm_history", getenv("HOME"));
//
//    FILE *fp = fopen(path, "a"); // Append mode
//    if (fp) {
//        // Using a simple delimiter like ::: so it's easy to parse later
//        fprintf(fp, "USER:%s\nAI:%s\n---\n", user_text, ai_text);
//        fclose(fp);
//    }
//}

void load_history_to_api(struct json_object *messages_array) {
    extern AppContext *global_app;
    MYSQL *conn = mysql_init(NULL);

    if (mysql_real_connect(conn, global_app->db_host, global_app->db_user, 
                           global_app->db_pass, global_app->db_name, 0, NULL, 0)) {
        
        // Grab the last 10 conversational entries (not Tee data)
        mysql_query(conn, "SELECT role, content FROM aiterm_history WHERE is_tee = 0 ORDER BY created_at DESC LIMIT 10");
        MYSQL_RES *res = mysql_store_result(conn);
        MYSQL_ROW row;

        while ((row = mysql_fetch_row(res))) {
            struct json_object *msg = json_object_new_object();
            json_object_object_add(msg, "role", json_object_new_string(row[0]));
            json_object_object_add(msg, "content", json_object_new_string(row[1]));
            json_object_array_add(messages_array, msg);
        }
        mysql_free_result(res);
        mysql_close(conn);
    }
}

// Old flat file
//void load_history_to_api(struct json_object *messages_array) {
//    char path[256];
//    snprintf(path, sizeof(path), "%s/.aiterm_history", getenv("HOME"));
//
//    FILE *fp = fopen(path, "r");
//    if (!fp) return;
//
//    char line[1024];
//    // This is a simple version; in a full 'tail' you'd seek to the end,
//    // but for 0.3, reading the last few lines works.
//    while (fgets(line, sizeof(line), fp)) {
//        if (strncmp(line, "USER:", 5) == 0) {
//            struct json_object *msg = json_object_new_object();
//            json_object_object_add(msg, "role", json_object_new_string("user"));
//            json_object_object_add(msg, "content", json_object_new_string(line + 5));
//            json_object_array_add(messages_array, msg);
//        } else if (strncmp(line, "AI:", 3) == 0) {
//            struct json_object *msg = json_object_new_object();
//            json_object_object_add(msg, "role", json_object_new_string("assistant"));
//            json_object_object_add(msg, "content", json_object_new_string(line + 3));
//            json_object_array_add(messages_array, msg);
//        }
//    }
//    fclose(fp);
//}

// Add to utils.c
void save_tee_to_history(const char *terminal_output, const char *ai_analysis) {
    extern AppContext *global_app;
    MYSQL *conn = mysql_init(NULL);

    if (mysql_real_connect(conn, global_app->db_host, global_app->db_user, 
                           global_app->db_pass, global_app->db_name, 0, NULL, 0)) {

        char query[10240]; // Terminal output can be large!
        // Again, eventually use mysql_real_escape_string for safety
        snprintf(query, sizeof(query), 
                 "INSERT INTO aiterm_history (role, content, is_tee) VALUES "
                 "('terminal', '%s', 1), ('assistant', '%s', 1)",
                 terminal_output, ai_analysis);

        mysql_query(conn, query);
        mysql_close(conn);
    }
}

void load_history_to_gemini(struct json_object *contents_array) {
    extern AppContext *global_app;
    MYSQL *conn = mysql_init(NULL);

    if (mysql_real_connect(conn, global_app->db_host, global_app->db_user, 
                           global_app->db_pass, global_app->db_name, 0, NULL, 0)) {
        
        // Grab only standard chat entries (is_tee = 0) to save RAM and tokens
        mysql_query(conn, "SELECT role, content FROM aiterm_history WHERE is_tee = 0 ORDER BY id DESC LIMIT 10");
        MYSQL_RES *res = mysql_store_result(conn);
        MYSQL_ROW row;

        // Note: Gemini expects chronological order, but we fetched DESC for the LIMIT.
        // For a true fix, you'd reverse these, but for a quick test, let's just pull them.
        while ((row = mysql_fetch_row(res))) {
            struct json_object *content_obj = json_object_new_object();
            struct json_object *parts = json_object_new_array();
            struct json_object *part_obj = json_object_new_object();

            // Gemini role "assistant" is called "model"
            const char *role = (strcmp(row[0], "assistant") == 0) ? "model" : "user";
            
            json_object_object_add(part_obj, "text", json_object_new_string(row[1]));
            json_object_array_add(parts, part_obj);
            
            json_object_object_add(content_obj, "role", json_object_new_string(role));
            json_object_object_add(content_obj, "parts", parts);
            
            // Add to the main contents array
            json_object_array_add(contents_array, content_obj);
        }
        mysql_free_result(res);
        mysql_close(conn);
    }
}