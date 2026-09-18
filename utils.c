// part of aiterm project
// utils.c
// Various utilities used in this project
// By: Peter Talbott
// Assisted by: Gemini
// April 2026 - September 2026

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
#include <sys/types.h>
#include <unistd.h>
#include <ifaddrs.h>
#include <sys/socket.h>

#include "utils.h"
#include "gui.h"
#include "openai.h"
#include "crypto.h"
#include "update.h"
#include "tee_handler.h"
#include "ratelimit.h"
#include "commands.h"
#include "noisefilter.h"
#include "gemini.h"
#include "ai_provider.h"
#include "snmp_manager.h"

// Added 0.9.9-beta
// Helper function to read and execute a single .sql file
int execute_sql_file(MYSQL *conn, const char *filepath) {
    FILE *f = fopen(filepath, "rb");
    if (!f) {
        DEBUG_PRINT("[ DEBUG ]: [DB] Failed to open SQL file: %s\n", filepath);
        return 0;
    }

    fseek(f, 0, SEEK_END);
    long length = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (length <= 0) {
        fclose(f);
        return 1; // Empty file is non-fatal
    }

    char *buffer = malloc(length + 1);
    if (!buffer) {
        fclose(f);
        DEBUG_PRINT("[ DEBUG ]: [DB] Memory allocation error reading %s\n", filepath);
        return 0;
    }

    size_t read_bytes = fread(buffer, 1, length, f);
    buffer[read_bytes] = '\0';
    fclose(f);

    DEBUG_PRINT("[ DEBUG ]: [DB] Executing SQL script: %s\n", filepath);

    // Run query
    if (mysql_query(conn, buffer) != 0) {
        DEBUG_PRINT("[ DEBUG ]: [DB] Error executing %s: %s\n", filepath, mysql_error(conn));
        free(buffer);
        return 0;
    }

    // Process any residual result sets if the script generates them
    MYSQL_RES *res = NULL;
    do {
        res = mysql_store_result(conn);
        if (res) {
            mysql_free_result(res);
        }
    } while (mysql_next_result(conn) == 0);

    free(buffer);
    return 1;
}

// Added 0.9.9-beta
void init_colors(AppContext *app) {
    check_debug_tty(app);
    if (app->sys.debug_color && app->sys.debug_tty) {
        app->ansi.red		= g_strdup(ANSI_RED);
        app->ansi.yellow	= g_strdup(ANSI_YELLOW);
        app->ansi.green		= g_strdup(ANSI_GREEN);
        app->ansi.blue		= g_strdup(ANSI_BLUE);
        app->ansi.purple	= g_strdup(ANSI_PURPLE);
        app->ansi.orange	= g_strdup(ANSI_ORANGE);
        app->ansi.cyan		= g_strdup(ANSI_CYAN);
        app->ansi.lt_red	= g_strdup(ANSI_LT_RED);
        app->ansi.lt_green	= g_strdup(ANSI_LT_GREEN);
        app->ansi.lt_blue	= g_strdup(ANSI_LT_BLUE);
        app->ansi.lt_purple	= g_strdup(ANSI_LT_PURPLE);
        app->ansi.normal	= g_strdup(ANSI_NORMAL);
    } else {
        // Initialize with empty strings
        app->ansi.red           = g_strdup("");
        app->ansi.yellow        = g_strdup("");
        app->ansi.green         = g_strdup("");
        app->ansi.blue          = g_strdup("");
        app->ansi.purple        = g_strdup("");
        app->ansi.orange        = g_strdup("");
        app->ansi.cyan          = g_strdup("");
        app->ansi.lt_red        = g_strdup("");
        app->ansi.lt_green      = g_strdup("");
        app->ansi.lt_blue       = g_strdup("");
        app->ansi.lt_purple     = g_strdup("");
        app->ansi.normal        = g_strdup("");
    }
}

// Added 0.9.9-beta
void cleanup_colors(AppContext *app) {
    g_free(app->ansi.red);
    g_free(app->ansi.yellow);
    g_free(app->ansi.green);
    g_free(app->ansi.blue);
    g_free(app->ansi.purple);
    g_free(app->ansi.orange);
    g_free(app->ansi.cyan);
    g_free(app->ansi.lt_red);
    g_free(app->ansi.lt_green);
    g_free(app->ansi.lt_blue);
    g_free(app->ansi.lt_purple);
    g_free(app->ansi.normal);
}

// Added 0.9.9-beta
// returns TRUE if the network is up
gboolean check_network_availability(AppContext *app) {
    gboolean found = FALSE;
    if (getifaddrs(&app->net.ifaddr) == -1) return FALSE;

    for (app->net.ifa = app->net.ifaddr; app->net.ifa != NULL; app->net.ifa = app->net.ifa->ifa_next) {
        if (app->net.ifa->ifa_addr && app->net.ifa->ifa_addr->sa_family == AF_INET) {
            // Check for a non-loopback interface
            if (strcmp(app->net.ifa->ifa_name, "lo") != 0) {
                found = TRUE;
                break;
            }
        }
    }
    freeifaddrs(app->net.ifaddr);
    char YES_VAL[32];
    char NO_VAL[30];
    snprintf(YES_VAL, 32, "%sYes%s", app->ansi.green, app->ansi.normal);
    snprintf(NO_VAL,  32, "%sNo%s", app->ansi.red, app->ansi.normal);
    DEBUG_PRINT("[%s DEBUG %s]: [%sNetwork%s] %sIs Network Online%s [%s]\n", 
        app->ansi.lt_purple, app->ansi.normal, app->ansi.yellow, app->ansi.normal, 
        app->ansi.lt_purple, app->ansi.normal, found ? YES_VAL : NO_VAL);
    return found;
}

// Checks multiple locations for config file and returns the one it found
const char* get_config_filename(void) {
    static char path[512];
    char *home = getenv("HOME");
    uid_t uid = getuid();
    char *env_cfg = getenv("AITERM_CONFIG");

    // 1. Check Environment Variable first
    if (env_cfg && access(env_cfg, F_OK) == 0) {
        return env_cfg;
    }

    // 2. Check /etc/.(UID).aiterm.conf
    snprintf(path, sizeof(path), "/etc/.%d.aiterm.conf", uid);
    if (access(path, F_OK) == 0) return path;

    // 3. Check ~/.config/aiterm.conf
    if (home) {
        snprintf(path, sizeof(path), "%s/.config/aiterm.conf", home);
        if (access(path, F_OK) == 0) return path;

        // 4. Check ~/.aiterm.conf
        snprintf(path, sizeof(path), "%s/.aiterm.conf", home);
        if (access(path, F_OK) == 0) return path;
    }

    // 5. Default to /etc/aiterm.conf
    return "/etc/aiterm.conf";
}

void init_config_pointer(AppContext *app) {
    if (!CONFIG_FILE) {    
        CONFIG_FILE = get_config_filename();
        DEBUG_PRINT("[%s DEBUG %s]: [%sConfig File%s] %sSet config filename: %s%s%s\n",
	    app->ansi.lt_purple, app->ansi.normal, app->ansi.yellow, app->ansi.normal,
	    app->ansi.lt_purple, app->ansi.green, CONFIG_FILE, app->ansi.normal);
    }
}

// Kept the functions "legacy name"
// for the most part it all booleans initialized here 
void initialize_booleans(AppContext *app) {
    // 1. Set initial variables to their needed defaults

    // Booleans set to FALSE on startup
    app->sys.debug_tty = FALSE;
    app->sys.debug_mode = FALSE;
    app->sys.mysql_busy = FALSE;
    app->sys.tee_enabled = FALSE;
    app->sys.db_initialized = FALSE;
    app->xml.tagging_enabled = FALSE;
    app->sys.offline_override = FALSE;
    app->sys.autoreply_enabled = FALSE;
    app->sys.ratelimit_enabled = FALSE;
    app->sys.load_from_session = FALSE;
    app->sys.smart_cache_enabled = FALSE;
    app->sys.debug_mode_override = FALSE;
    app->sys.is_network_available = FALSE;
    app->sys.auto_execute_enabled = FALSE;
    app->SnmpContext.enable_gemini_feed = FALSE;
    app->session.cfg_loaded_write_to_global = FALSE;
    app->session.cfg_loaded_read_from_global = FALSE;

     // Specify FALSE if not initialized,
     // leave it alone if set by environment
    if (!app->sys.debug_color) {
        app->sys.debug_color = FALSE;
    }

    // Booleans set TRUE at startup
    app->sys.is_initializing = TRUE;
    app->sys.snmp_ticker_enabled = TRUE;

    // NON-Boolean initializer
    // [ I know the function's name is 
    //   initialize_booleans and these 
    //   are not boolean, but it is the 
    //   perfect place to initialize. lol ]
    g_atomic_int_set(&app->sys.is_processing, 0);
    g_atomic_int_set(&app->sys.ai_busy, 0);

    // Noise-filter patterns are copied into a non-GTK cache so background
    // workers never access the GTK ListStore.
    g_mutex_init(&app->noise.patterns_mutex);
    app->noise.patterns = g_ptr_array_new_with_free_func(g_free);

    for (int i = 0; i < MAX_TABS; i++) {
        memset(&app->tabs[i], 0, sizeof(TabSettings));
        app->tabs[i].tab_label_box = NULL;
        app->tabs[i].label = NULL;
        app->tabs[i].close_btn = NULL;
        app->tabs[i].is_active = FALSE;
        app->tabs[i].close_tab_button_enabled = TRUE; 
        // ... etc
    }
    app->database.sequence_id = 0;
    app->limiter.requests_per_minute=20;
}

// Added 0.9.9-beta
void check_debug_tty(AppContext *app) {
    if (isatty(fileno(stderr))) {
        app->sys.debug_tty = 1;
    } else {
        app->sys.debug_tty = 0;
    }
}

// Added 0.9.8-alpha
void init_runtime_queues(AppContext *app) {
    if (!app) return;
    DEBUG_PRINT("[%s DEBUG %s]: [%sRuntime Queues%s]%s Initializing... ",
	app->ansi.lt_purple, app->ansi.normal, app->ansi.cyan,
	app->ansi.normal, app->ansi.yellow);
    app->aiterm_runtime.pending_autoexec_queue = g_queue_new();
    app->aiterm_runtime.is_command_running = FALSE;
    app->aiterm_runtime.ticker_completed = TRUE;
    fprintf(stderr,"%sDone!%s\n", app->ansi.green, app->ansi.normal);
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
    free(provider->provider);
    memset(provider, 0, sizeof(ProviderConfig));
}

void init_provider_config(AppContext *app) {
    if (!app) return;

    ProviderConfig *provider = &app->provider_config;
    const char *env_provider = getenv("AITERM_PROVIDER");
    const char *env_model = getenv("AITERM_MODEL");
    char *name = g_strdup((env_provider && *env_provider) ? env_provider :
                          (provider->provider ? provider->provider : "openai"));
    const char *model = (env_model && *env_model) ? env_model :
                        (app->aiterm_runtime.model ? app->aiterm_runtime.model : NULL);
    char *saved_base_url = g_strdup(provider->base_url);
    char *saved_endpoint = g_strdup(provider->endpoint);
    char *saved_auth_header = g_strdup(provider->auth_header);
    char *saved_auth_scheme = g_strdup(provider->auth_scheme);
    char *saved_query_key = g_strdup(provider->query_key_name);
    gboolean saved_key_in_query = provider->api_key_in_query;

    /* ProviderConfig owns all of its strings.  Preserve the configured provider
     * name before clearing the old structure. */
    free_provider_config(provider);
    provider_replace_string(&provider->provider, name);
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

        /* These providers use the OpenAI Chat Completions wire format. */
        if (strcasecmp(name, "groq") == 0) {
            provider_replace_string(&provider->model, model ? model : "llama-3.3-70b-versatile");
            provider_replace_string(&provider->base_url, "https://api.groq.com/openai/v1");
        } else if (strcasecmp(name, "openrouter") == 0) {
            provider_replace_string(&provider->model, model ? model : "openai/gpt-oss-20b:free");
            provider_replace_string(&provider->base_url, "https://openrouter.ai/api/v1");
        } else if (strcasecmp(name, "mistral") == 0) {
            provider_replace_string(&provider->model, model ? model : "mistral-small-latest");
            provider_replace_string(&provider->base_url, "https://api.mistral.ai/v1");
        } else if (strcasecmp(name, "ollama") == 0) {
            provider_replace_string(&provider->model, model ? model : "llama3.2");
            provider_replace_string(&provider->base_url, "http://127.0.0.1:11434/v1");
        } else {
            provider_replace_string(&provider->model, model ? model : OPENAI_MODEL);
            provider_replace_string(&provider->base_url, "https://api.openai.com/v1");
        }

        provider_replace_string(&provider->endpoint, "chat/completions");
        provider_replace_string(&provider->auth_header, "Authorization");
        provider_replace_string(&provider->auth_scheme, "Bearer");

        /* Ollama does not require an API key. */
        if (strcasecmp(name, "ollama") == 0) {
            g_free(provider->auth_header);
            provider->auth_header = NULL;
            g_free(provider->auth_scheme);
            provider->auth_scheme = NULL;
        }
    }

    const char *env_base_url = getenv("AITERM_PROVIDER_BASE_URL");
    const char *env_endpoint = getenv("AITERM_PROVIDER_ENDPOINT");
    const char *env_auth_header = getenv("AITERM_PROVIDER_AUTH_HEADER");
    const char *env_auth_scheme = getenv("AITERM_PROVIDER_AUTH_SCHEME");
    const char *env_query_key = getenv("AITERM_PROVIDER_QUERY_KEY");

    /* Values saved by the Provider Manager override built-in defaults. */
    if (saved_base_url && *saved_base_url) provider_replace_string(&provider->base_url, saved_base_url);
    if (saved_endpoint && *saved_endpoint) provider_replace_string(&provider->endpoint, saved_endpoint);
    if (saved_auth_header) provider_replace_string(&provider->auth_header, saved_auth_header);
    if (saved_auth_scheme) provider_replace_string(&provider->auth_scheme, saved_auth_scheme);
    if (saved_query_key) provider_replace_string(&provider->query_key_name, saved_query_key);
    if (saved_key_in_query) provider->api_key_in_query = TRUE;

    char *lt_pl = g_strdup(app->ansi.lt_purple);
    char *cy = g_strdup(app->ansi.cyan);
    char *yl = g_strdup(app->ansi.yellow);
    char *gr = g_strdup(app->ansi.green);
    char *red = g_strdup(app->ansi.red);
    char *nml = g_strdup(app->ansi.normal);

    if (env_base_url && *env_base_url) provider_replace_string(&provider->base_url, env_base_url);
    if (env_endpoint && *env_endpoint) provider_replace_string(&provider->endpoint, env_endpoint);
    if (env_auth_header && *env_auth_header) provider_replace_string(&provider->auth_header, env_auth_header);
    if (env_auth_scheme) provider_replace_string(&provider->auth_scheme, *env_auth_scheme ? env_auth_scheme : NULL);
    if (env_query_key && *env_query_key) provider_replace_string(&provider->query_key_name, env_query_key);

    DEBUG_PRINT("[%s DEBUG %s]: [%sProvider_Init%s]:%s Provider: %s%s%s | Protocol: %s | Base URL: %s%s%s\n",
                lt_pl, nml, cy, nml, yl, gr, name, nml,
                provider->kind == PROVIDER_KIND_GEMINI_GENERATE ? "gemini-generateContent" : "openai-chat-completions",
                gr, provider->base_url ? provider->base_url : "(none)", nml);

    g_free(name);
    g_free(saved_base_url);
    g_free(saved_endpoint);
    g_free(saved_auth_header);
    g_free(saved_auth_scheme);
    g_free(saved_query_key);
    g_free(lt_pl);
    g_free(cy);
    g_free(yl);
    g_free(gr);
    g_free(red);
    g_free(nml);
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

     char *lt_pl   = g_strdup(app->ansi.lt_purple);
     char *cy      = g_strdup(app->ansi.cyan);
     char *yl      = g_strdup(app->ansi.yellow);
     char *gr      = g_strdup(app->ansi.green);
     char *red      = g_strdup(app->ansi.red);
     char *nml      = g_strdup(app->ansi.normal);

    // CRITICAL: Initialize thread-specific MySQL memory
    mysql_thread_init();

    DEBUG_PRINT("[%s DEBUG %s]: [%sDB_THREAD%s]%s Invoking init_remote_db...%s\n",
	lt_pl, nml, cy, nml, yl, nml);
    if (init_remote_db(app)) {
        DEBUG_PRINT("[%s DEBUG %s]: [%sDatabase%s]%s setup and persistent connection ready.%s\n",
	lt_pl, nml, cy, nml, yl, nml);
    } else {
        DEBUG_PRINT("[%s DEBUG %s]: [%sDatabase%s]%s offline. History will not be saved.%s\n",
	lt_pl, nml, cy, nml, yl, nml);
    }
    pthread_mutex_lock(&app->access.db_init_mutex);
    DEBUG_PRINT("[%s DEBUG %s]: [%sINIT_DB_THREAD_WORKER%s]%s Locked DB init Mutex%s\n",
	lt_pl, nml, cy, nml, yl, nml);
    app->sys.db_initialized = TRUE;
    pthread_cond_signal(&app->access.db_init_cond); // Wake up waiting threads
    pthread_mutex_unlock(&app->access.db_init_mutex);
    DEBUG_PRINT("[%s DEBUG %s]: [%sINIT_DB_THREAD_WORKER%s]%s Unlocked DB init Mutex%s\n",
	lt_pl, nml, cy, nml, yl, nml);
    // CRITICAL: Clean up thread-specific MySQL memory
    mysql_thread_end();
    g_free(cy);
    g_free(yl);
    g_free(gr);
    g_free(red);
    g_free(nml);
    return NULL;
}

void feed_terminal_header(VteTerminal *terminal, const char *msg) {
    char buf[1024];
    snprintf(buf, sizeof(buf), "\r%s[AI Executing]: %s%s\n", global_app->ansi.cyan, msg, global_app->ansi.normal);
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
    DEBUG_PRINT("[ DEBUG ]: DISPLAY_ALL_HISTORY: Locked DB Mutex\n");
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
    DEBUG_PRINT("[ DEBUG ]: DISPLAY_ALL_HISTORY: Query %s\n", query);
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
    DEBUG_PRINT("[ DEBUG ]: DISPLAY_ALL_HISTORY: Unlocked DB Mutex\n");
    mysql_thread_end();
    return;
}

// Modified 0.7.4-delta for global mysql connection
// Rewritten 0.8.4-delta
// Modified 0.8.5-gamma for target uuid
void* db_worker_thread(void *arg) {
     char *lt_pl   = g_strdup(global_app->ansi.lt_purple);
     char *cy      = g_strdup(global_app->ansi.cyan);
     char *yl      = g_strdup(global_app->ansi.yellow);
     char *gr      = g_strdup(global_app->ansi.green);
     char *red      = g_strdup(global_app->ansi.red);
     char *nml      = g_strdup(global_app->ansi.normal);

    DEBUG_PRINT("[%sMEMDBG %s]: [%sWORKER%s]%s ENTER arg%s=%s%p%s\n",
	lt_pl, nml, cy, nml, yl, nml, gr, arg, nml);
    mysql_thread_init();
    DBWorkerData *data = (DBWorkerData *)arg;
    SET_THREAD_NAME("aiterm-db");
    DEBUG_PRINT("[%sMEMDBG %s]: [%sWORKER%s]%s data=%s%p%s terminal=%s%p%s ai_analysis=%s%p%s user=%s%p%s ai=%s%p%s uuid=%s%p%s\n",
                lt_pl, nml, cy, nml, yl, 
		red, (void*)data,yl,
                red, data ? (void*)data->terminal_output : NULL, yl,
                red, data ? (void*)data->ai_analysis : NULL, yl,
                red, data ? (void*)data->user_text : NULL, yl, 
                red, data ? (void*)data->ai_text : NULL, yl,
                red, data ? (void*)data->session_uuid : NULL, nml);
    if (!data) {
        DEBUG_PRINT("[%s DEBUG %s]: [%sWORKER%s]%s Received %sNULL%s data pointer!%s\n",
		lt_pl, nml, cy, nml, yl, red, yl, nml);
        mysql_thread_end();
        return NULL;
    }

    extern AppContext *global_app;
    if (!global_app || !global_app->database.global_db_conn) {
        DEBUG_PRINT("[%sMEMDBG %s]: [%sWORKER%s]%s No global app/DB connection, jumping to cleanup data=%s%p%s\n",
		lt_pl, nml, cy, nml, yl, red, (void*)data, nml);
        goto cleanup;
    }

    const char *target_uuid = global_app->session.write_to_global
                              ? GLOBAL_SESSION_UUID
                              : global_app->session.session_uuid;

    pthread_mutex_lock(&global_app->access.db_mutex);
    DEBUG_PRINT("[%s DEBUG %s]: [%sWORKER%s]%s Locked DB Mutex%s\n",
	lt_pl, nml, cy, nml, yl, nml);
    DEBUG_PRINT("[%s DEBUG %s]: [%sWORKER%s]%s Starting job for seq %s%d%s, is_tee=%s%d%s\n",
        lt_pl, nml, cy, nml, yl, red, data->sequence_id, yl, red, data->is_tee, nml);

    if (data->is_tee) {
        DEBUG_PRINT("[%sMEMDBG %s]: [%sWORKER%s]%s TEE pointers before strlen: data=%s%p%s terminal=%s%p%s ai=%s%p%s uuid=%s%p%s\n",
                lt_pl, nml, cy, nml, yl, 
		gr, (void*)data, yl,
		gr, (void*)data->terminal_output, yl,
		gr, (void*)data->ai_analysis, yl,
		gr, (void*)data->session_uuid, nml);

        size_t out_len = data->terminal_output ? strlen(data->terminal_output) : 0;
        size_t ai_len  = data->ai_analysis ? strlen(data->ai_analysis) : 0;
        size_t uuid_len = data->session_uuid ? strlen(data->session_uuid) : 0;

        DEBUG_PRINT("[%sMEMDBG %s]: [%sWORKER%s]%s TEE lengths out=%s%zu%s ai=%s%zu%s uuid=%s%zu%s\n",
		lt_pl, nml, cy, nml, yl,
		gr, out_len, yl,
		gr, ai_len, yl,
		gr, uuid_len, nml);

        char *esc_out = malloc(out_len * 2 + 1);
        char *esc_ai  = malloc(ai_len * 2 + 1);
        char *esc_uuid = malloc(uuid_len * 2 + 1);

        DEBUG_PRINT("[%sMEMDBG %s]: [%sWORKER%s]%s escaped allocations out=%s%p%s ai=%s%p%s uuid=%s%p%s\n",
                lt_pl, nml, cy, nml, yl,
                red, (void*)esc_out, yl,
		red, (void*)esc_ai, yl,
		red, (void*)esc_uuid, nml);

        if (esc_out && esc_ai && esc_uuid) {
            mysql_real_escape_string(global_app->database.global_db_conn,
                                     esc_out, data->terminal_output ? data->terminal_output : "", out_len);
            mysql_real_escape_string(global_app->database.global_db_conn,
                                     esc_ai, data->ai_analysis ? data->ai_analysis : "", ai_len);
            mysql_real_escape_string(global_app->database.global_db_conn,
                                     esc_uuid, data->session_uuid ? data->session_uuid : "", uuid_len);

            size_t query_len = strlen(esc_out) + strlen(esc_ai) + strlen(esc_uuid) +
                               strlen(target_uuid ? target_uuid : "") + 512;
            char *query = malloc(query_len);
            DEBUG_PRINT("[%sMEMDBG %s]: [%sWORKER%s]%s query alloc=%s%p%s size=%s%zu%s\n",
                lt_pl, nml, cy, nml, yl,
		red, (void*)query, yl,
		red, query_len, yl);

            if (query) {
                snprintf(query, query_len,
                         "INSERT INTO aiterm_history (role, content, is_tee, session_uuid, sequence_id) VALUES "
                         "('terminal', '%s', 1, '%s', %d), ('assistant', '%s', 1, '%s', %d)",
                         esc_out, target_uuid ? target_uuid : "", data->sequence_id,
                         esc_ai, esc_uuid, data->sequence_id);
                int qrc = mysql_query(global_app->database.global_db_conn, query);

                DEBUG_PRINT("[%sMEMDBG %s]: [%sWORKER%s]%s mysql_query rc=%s%d%s query=%s%p%s\n",
			lt_pl, nml, cy, nml, yl,
			red, qrc, yl,
			red, (void*)query, nml);

                DEBUG_PRINT("[%sMEMDBG %s]: [%sWORKER%s]%s FREE query=%s%p%s\n",
	                lt_pl, nml, cy, nml, yl,
 			gr, (void*)query, nml);

                free(query);
            }
        }

        DEBUG_PRINT("[MEMDBG ]: [WORKER] FREE esc_out=%p\n", (void*)esc_out);
        free(esc_out);
        DEBUG_PRINT("[MEMDBG ]: [WORKER] FREE esc_ai=%p\n", (void*)esc_ai);
        free(esc_ai);
        DEBUG_PRINT("[MEMDBG ]: [WORKER] FREE esc_uuid=%p\n", (void*)esc_uuid);
        free(esc_uuid);
    } else {
        size_t user_len = data->user_text ? strlen(data->user_text) : 0;
        size_t ai_len   = data->ai_text ? strlen(data->ai_text) : 0;
        size_t uuid_len = data->session_uuid ? strlen(data->session_uuid) : 0;

        char *esc_user = malloc(user_len * 2 + 1);
        char *esc_ai   = malloc(ai_len * 2 + 1);
        char *esc_uuid = malloc(uuid_len * 2 + 1);

        if (esc_user && esc_ai && esc_uuid) {
            mysql_real_escape_string(global_app->database.global_db_conn,
                                     esc_user, data->user_text ? data->user_text : "", user_len);
            mysql_real_escape_string(global_app->database.global_db_conn,
                                     esc_ai, data->ai_text ? data->ai_text : "", ai_len);
            mysql_real_escape_string(global_app->database.global_db_conn,
                                     esc_uuid, data->session_uuid ? data->session_uuid : "", uuid_len);

            size_t query_len = strlen(esc_user) + strlen(esc_ai) + strlen(esc_uuid) +
                               strlen(target_uuid ? target_uuid : "") + 512;
            char *query = malloc(query_len);
            DEBUG_PRINT("[MEMDBG ]: [WORKER] nonTEE query alloc=%p size=%zu\n", (void*)query, query_len);
            if (query) {
                snprintf(query, query_len,
                         "INSERT INTO aiterm_history (role, content, is_tee, session_uuid, sequence_id) "
                         "VALUES ('user', '%s', 0, '%s', %d), ('assistant', '%s', 0, '%s', %d)",
                         esc_user, target_uuid ? target_uuid : "", data->sequence_id,
                         esc_ai, esc_uuid, data->sequence_id);
                int qrc = mysql_query(global_app->database.global_db_conn, query);
                DEBUG_PRINT("[MEMDBG ]: [WORKER] nonTEE mysql_query rc=%d query=%p\n", qrc, (void*)query);
                DEBUG_PRINT("[MEMDBG ]: [WORKER] FREE nonTEE query=%p\n", (void*)query);
                free(query);
            }
        }

        DEBUG_PRINT("[MEMDBG ]: [WORKER] FREE esc_user=%p\n", (void*)esc_user);
        free(esc_user);
        DEBUG_PRINT("[MEMDBG ]: [WORKER] FREE nonTEE esc_ai=%p\n", (void*)esc_ai);
        free(esc_ai);
        DEBUG_PRINT("[MEMDBG ]: [WORKER] FREE nonTEE esc_uuid=%p\n", (void*)esc_uuid);
        free(esc_uuid);
    }

    pthread_mutex_unlock(&global_app->access.db_mutex);
    DEBUG_PRINT("[ DEBUG ]: [WORKER] Unlocked DB Mutex\n");

    cleanup:
    /* DBWorkerData owns these copies exclusively.  The caller never frees them
     * after successful pthread_create(). */
    DEBUG_PRINT("[MEMDBG ]: [WORKER] CLEANUP data=%p\n", (void*)data);
    DEBUG_PRINT("[MEMDBG ]: [WORKER] FREE terminal_output=%p\n", (void*)data->terminal_output);
    g_free(data->terminal_output);
    DEBUG_PRINT("[MEMDBG ]: [WORKER] FREE ai_analysis=%p\n", (void*)data->ai_analysis);
    g_free(data->ai_analysis);
    DEBUG_PRINT("[MEMDBG ]: [WORKER] FREE user_text=%p\n", (void*)data->user_text);
    g_free(data->user_text);
    DEBUG_PRINT("[MEMDBG ]: [WORKER] FREE ai_text=%p\n", (void*)data->ai_text);
    g_free(data->ai_text);
    DEBUG_PRINT("[MEMDBG ]: [WORKER] FREE session_uuid=%p\n", (void*)data->session_uuid); 
    g_free(data->session_uuid);
    /* Save scalar debug information before releasing the owning structure.
     * Do not evaluate the freed pointer in a DEBUG_PRINT after free(). */
    int completed_sequence_id = data->sequence_id;
    DEBUG_PRINT("[ DEBUG ]: [WORKER] Job completed for seq %d\n",
                completed_sequence_id);
    DEBUG_PRINT("[MEMDBG ]: [WORKER] FREE DBWorkerData=%p\n", (void*)data);
    free(data);
    DEBUG_PRINT("[MEMDBG ]: [WORKER] DBWorkerData released for seq %d\n",
                completed_sequence_id);
    mysql_thread_end();

    g_free(cy);
    g_free(yl);
    g_free(gr);
    g_free(red);
    g_free(nml);
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

    char *lt_pl   = g_strdup(app->ansi.lt_purple);
    char *cy      = g_strdup(app->ansi.cyan);
    char *yl      = g_strdup(app->ansi.yellow);
    char *gr      = g_strdup(app->ansi.green);
    char *red     = g_strdup(app->ansi.red);
    char *nml     = g_strdup(app->ansi.normal);

    // === NEW: Set a 3-second connection timeout ===
    unsigned int timeout = 3; // 3 seconds
    mysql_options(app->database.global_db_conn, MYSQL_OPT_CONNECT_TIMEOUT, (const char *)&timeout);

    my_bool reconnect = 1;
    mysql_options(app->database.global_db_conn, MYSQL_OPT_RECONNECT, &reconnect);

    // NEW: Set read/write timeouts for queries so they don't hang forever
    // mysql_options(app->database.global_db_conn, MYSQL_OPT_READ_TIMEOUT, (const char *)&timeout);
    // mysql_options(app->database.global_db_conn, MYSQL_OPT_WRITE_TIMEOUT, (const char *)&timeout);

    DEBUG_PRINT("[%s DEBUG %s]: [%sDB%s]%s Connecting to %s%s%s (%stimeout: %us%s)...%s\n", 
	lt_pl, nml, cy,nml, yl, red, app->database.db_host, yl, red, timeout, yl, nml);
    // 2. Connect to the server (No DB selected yet)
    if (mysql_real_connect(app->database.global_db_conn, app->database.db_host, app->database.db_user, app->database.db_pass, NULL, 0, NULL, 0) == NULL) {
        DEBUG_PRINT("[ DEBUG ]: DB Connection Error: %s\n", mysql_error(app->database.global_db_conn));
        mysql_close(app->database.global_db_conn);
        app->database.global_db_conn = NULL;
        return 0;
    }
    DEBUG_PRINT("[ DEBUG ]: [DB] Successfully connected to database host.\n");

    // 3. Create and Select Database
    char db_query[256];
    snprintf(db_query, sizeof(db_query), "CREATE DATABASE IF NOT EXISTS %s", app->database.db_name);

    DEBUG_PRINT("[ DEBUG ]: [DB] Executing: %s\n", db_query);
    mysql_query(app->database.global_db_conn, db_query);

    DEBUG_PRINT("[ DEBUG ]: [DB] Selecting database: %s\n", app->database.db_name);
    mysql_select_db(app->database.global_db_conn, app->database.db_name);

    // 4. Create History Table
    DEBUG_PRINT("[ DEBUG ]: [DB] Executing: CREATE TABLE IF NOT EXISTS aiterm_history\n");
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
        DEBUG_PRINT("[ DEBUG ]: [DB] query [%d]: %s\n", i, migrations[i]);
        mysql_query(app->database.global_db_conn, migrations[i]);
    }

    // 6. Setup Triggers Table
    const char *trigger_table_query =
        "CREATE TABLE IF NOT EXISTS relevance_triggers ("
        "id INT AUTO_INCREMENT PRIMARY KEY, "
        "keyword VARCHAR(50) UNIQUE, "
        "hit_count INT DEFAULT 1, "
        "last_used TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON DUPLICATE KEY UPDATE last_used=CURRENT_TIMESTAMP)";
    DEBUG_PRINT("[ DEBUG ]: [DB] Executing: CREATE TABLE IF NOT EXISTS relevance_triggers\n");
    mysql_query(app->database.global_db_conn, trigger_table_query);

    const char *command_policy_table =
	"CREATE TABLE IF NOT EXISTS command_policies ("
	"command VARCHAR(256) PRIMARY KEY, "
	"type VARCHAR(32) NOT NULL, "
	"risk_level VARCHAR(32) NOT NULL, "
	"updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON DUPLICATE KEY UPDATE updated_at=CURRENT_TIMESTAMP"
	")";
    DEBUG_PRINT("[ DEBUG ]: [DB] Executing: CREATE TABLE IF NOT EXISTS command_policies\n");
    mysql_query(app->database.global_db_conn, command_policy_table);

    // Run Migrations for Sessions Table
    const char* session_migrations[] = {
        "ALTER TABLE sessions ADD COLUMN IF NOT EXISTS load_from_session TINYINT(1) DEFAULT 0",
        "ALTER TABLE sessions ADD COLUMN IF NOT EXISTS debug_mode TINYINT(1) DEFAULT 0",
        "ALTER TABLE sessions ADD COLUMN IF NOT EXISTS tee_enabled TINYINT(1) DEFAULT 0",
        "ALTER TABLE sessions ADD COLUMN IF NOT EXISTS autoreply_enabled TINYINT(1) DEFAULT 0",
        "ALTER TABLE sessions ADD COLUMN IF NOT EXISTS auto_execute_enabled TINYINT(1) DEFAULT 0",
        "ALTER TABLE sessions ADD COLUMN IF NOT EXISTS ratelimit_enabled TINYINT(1) DEFAULT 1",
        "ALTER TABLE sessions ADD COLUMN IF NOT EXISTS smart_cache_enabled TINYINT(1) DEFAULT 0",
        "ALTER TABLE sessions ADD COLUMN IF NOT EXISTS noise_filter_enabled TINYINT(1) DEFAULT 1",
        "ALTER TABLE sessions ADD COLUMN IF NOT EXISTS xml_payload_tagging TINYINT(1) DEFAULT 1",
        "ALTER TABLE sessions ADD COLUMN IF NOT EXISTS write_to_global TINYINT(1) DEFAULT 0",
        "ALTER TABLE sessions ADD COLUMN IF NOT EXISTS read_from_global TINYINT(1) DEFAULT 1"
    };

    for (int i = 0; i < sizeof(session_migrations)/sizeof(char*); i++) {
        DEBUG_PRINT("[ DEBUG ]: [DB] query [%d]: %s\n", i, session_migrations[i]);
        mysql_query(app->database.global_db_conn, session_migrations[i]);
    }
    DEBUG_PRINT("[ DEBUG ]: [DB] init_remote_db sequence fully complete!\n");

    g_free(cy);
    g_free(yl);
    g_free(gr);
    g_free(red);
    g_free(nml);

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

    char *lt_pl   = g_strdup(app->ansi.lt_purple);
    char *cy      = g_strdup(app->ansi.cyan);
    char *yl      = g_strdup(app->ansi.yellow);
    char *gr      = g_strdup(app->ansi.green);
    char *red     = g_strdup(app->ansi.red);
    char *nml     = g_strdup(app->ansi.normal);

    pthread_mutex_lock(&app->access.db_mutex);
    DEBUG_PRINT("[%s DEBUG %s]: [%sLOAD_HISTORY_TO_GEMINI%s]:%s Locked DB Mutex%s\n",
	lt_pl, nml, cy, nml, yl, nml);
    char *uuid_filter = get_uuid_filter(global_app);

    const char *query = g_strdup_printf(
        "  SELECT role, content FROM aiterm_history "
        "  WHERE session_uuid %s"
        "  ORDER BY created_at DESC LIMIT 100", uuid_filter);

    DEBUG_PRINT("[%s DEBUG %s]: [%sLOAD_HISTORY_TO_GEMINI%s]: %sQuery %s%s%s\n",
	lt_pl, nml, cy,nml, yl, gr, query, nml);
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

            // Choose the XML tag locally. Do not modify the shared
            // app->xml.type field from an AI worker thread.
            TagType row_type;
            if (strcmp(role, "model") == 0) {
               row_type = TAG_HISTORY;
            } else if (strcmp(role, "user") == 0) {
               row_type = TAG_MEMORY;
            } else {
               row_type = TAG_LOG_DUMP;
            }
            char *wrapped_content = xml_wrap_with_type(app, data, row_type);
            g_free(data);

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
    DEBUG_PRINT("[%s DEBUG %s]: [%sLOAD_HISTORY_TO_GEMINI%s]:%s Sent [%s%d%s] rows to AI%s\n",
	lt_pl, nml, cy, nml, yl, red, count, yl, nml);
    pthread_mutex_unlock(&app->access.db_mutex);
    DEBUG_PRINT("[%s DEBUG %s]: [%sLOAD_HISTORY_TO_GEMINI%s]:%s Unlocked DB Mutex%s\n",
	lt_pl, nml, cy, nml, yl, nml);

    g_free(lt_pl);
    g_free(cy);
    g_free(yl);
    g_free(gr);
    g_free(red);
    g_free(nml);

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

    // 2. Try OpenAI Chat Completions format (choices -> message -> content)
    struct json_object *choices = NULL;
    if (json_object_object_get_ex(root, "choices", &choices) &&
        json_object_get_type(choices) == json_type_array &&
        json_object_array_length(choices) > 0) {
        struct json_object *choice = json_object_array_get_idx(choices, 0);
        struct json_object *message = NULL;
        struct json_object *content_obj = NULL;
        if (choice && json_object_object_get_ex(choice, "message", &message) &&
            json_object_object_get_ex(message, "content", &content_obj) &&
            json_object_get_type(content_obj) == json_type_string) {
            char *result = g_strdup(json_object_get_string(content_obj));
            json_object_put(root);
            return result;
        }
    }

    // 3. Try OpenAI/Internal Responses format (output array)
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

    // 4. Cleanup if no matches found
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

    /* cleaned_ai_text is still needed by keyword extraction.  Do not free it
     * until after extract_and_save_keywords() returns. */
    pthread_t thread_id;
    if (pthread_create(&thread_id, NULL, db_worker_thread, data) != 0) {
        DEBUG_PRINT("[ DEBUG ]: [WORKER] pthread_create failed for history save\n");
        g_free(data->user_text);
        g_free(data->ai_text);
        g_free(data->session_uuid);
        free(data);
    } else {
        pthread_detach(thread_id);
    }

    // This part stays in the main thread
    extract_and_save_keywords(global_app, cleaned_ai_text); // Use cleaned text for keywords too
    g_free(cleaned_user_text);
    g_free(cleaned_ai_text);
}

void save_tee_to_history(const char *terminal_text, const char *ai_analysis) {
    extern AppContext *global_app;

    char *lt_pl   = g_strdup(global_app->ansi.lt_purple);
    char *cy      = g_strdup(global_app->ansi.cyan);
    char *yl      = g_strdup(global_app->ansi.yellow);
    char *gr      = g_strdup(global_app->ansi.green);
    char *red     = g_strdup(global_app->ansi.red);
    char *nml     = g_strdup(global_app->ansi.normal);

    if (!global_app || !terminal_text || !ai_analysis || !global_app->session.session_uuid) {
        DEBUG_PRINT("[ %sDEBUG%s ]: [%sTEE_SAVE%s]%s WARNING: %sInvalid inputs detected. Aborting.%s\n",
		lt_pl, nml, cy, nml, red, yl, nml);
        return;
    }

    /* Build the cleaned copies in this thread.  DBWorkerData takes ownership
     * of these exact allocations and the worker frees them exactly once. */
    char *terminal_output = noise_filter_apply(global_app, terminal_text);
    if (!terminal_output) terminal_output = g_strdup("");

    DEBUG_PRINT("[%sMEMDBG%s ]: [%sTEE_SAVE%s] %safter noise_filter terminal_output=%s%p%s\n", 
	lt_pl, nml, cy, nml, yl, red, (void*)terminal_output, nml);

    char *cleaned_terminal_output = strip_blank_lines(terminal_output);
    char *cleaned_ai_analysis = strip_blank_lines(ai_analysis);

    DEBUG_PRINT("[%sMEMDBG%s ]: [%sTEE_SAVE%s] %scleaned terminal=%s%p%s ai=%s%p%s\n",
	lt_pl, nml, cy, nml, yl,
	red, (void*)cleaned_terminal_output, yl,
	red, (void*)cleaned_ai_analysis, nml);

    g_free(terminal_output);

    if (!cleaned_terminal_output) cleaned_terminal_output = g_strdup("");
    if (!cleaned_ai_analysis) cleaned_ai_analysis = g_strdup("");

    DEBUG_PRINT("[ %sDEBUG%s ]: [%sTEE_SAVE%s]%s Data cleaned. Allocating DBWorkerData.%s\n",
	lt_pl, nml, cy, nml, yl, nml);

    DBWorkerData *data = calloc(1, sizeof(*data));
    if (!data) {
        DEBUG_PRINT("[ DEBUG ]: [TEE_SAVE] FATAL: Malloc failed for DBWorkerData\n");
        g_free(cleaned_terminal_output);
        g_free(cleaned_ai_analysis);
        return;
    }

    data->terminal_output = cleaned_terminal_output;
    data->ai_analysis = cleaned_ai_analysis;
    data->session_uuid = g_strdup(global_app->session.session_uuid);
    data->sequence_id = global_app->database.sequence_id++;
    DEBUG_PRINT("[%sMEMDBG%s ]: [%sTEE_SAVE%s]%s DBWorkerData=%s%p%s terminal=%s%p%s ai=%s%p%s uuid=%s%p%s seq=%s%d%s\n",
		lt_pl, nml, cy, nml, yl,
                red, (void*)data, yl,
		red, (void*)data->terminal_output, yl,
		red, (void*)data->ai_analysis, yl,
                red, (void*)data->session_uuid, yl,
		red, data->sequence_id, nml);
    data->is_tee = 1;

    DEBUG_PRINT("[ %sDEBUG%s ]: [%sTEE_SAVE%s] %sLaunching thread for sequence_id: %s%d%s\n",
		lt_pl, nml, cy, nml, yl,
                red, data->sequence_id, nml);

    pthread_t thread_id;
    if (pthread_create(&thread_id, NULL, db_worker_thread, data) != 0) {
        DEBUG_PRINT("[ DEBUG ]: [TEE_SAVE] FATAL: pthread_create failed!\n");
        g_free(data->terminal_output);
        g_free(data->ai_analysis);
        g_free(data->session_uuid);
        free(data);
        return;
    }

    g_free(cy);
    g_free(yl);
    g_free(gr);
    g_free(red);
    g_free(nml);

    pthread_detach(thread_id);
}


// updated 0.7.4-delta to use global mysql connection
void load_history_to_api(struct json_object *messages_array) {
    extern AppContext *global_app;
    if (!global_app || !global_app->database.global_db_conn) return;

    mysql_thread_init();
    pthread_mutex_lock(&global_app->access.db_mutex);

    DEBUG_PRINT("[ DEBUG ] LOAD_HISTORY_TO_API: Locked DB Mutex\n");
    char *uuid_filter = get_uuid_filter(global_app);
    const char *query = g_strdup_printf(
        "SELECT role, content FROM ("
        "  SELECT role, content FROM aiterm_history "
        "  WHERE session_uuid %s AND is_tee = 0 AND sequence_id > %d"
        "  ORDER BY sequence_id DESC LIMIT 100"
        ") AS sub ORDER BY created_at ASC", uuid_filter, global_app->session.last_sent_db_id);

    DEBUG_PRINT("[ DEBUG ]: LOAD_HISTORY_TO_API: Query %s\n", query);

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
    DEBUG_PRINT("[ DEBUG ] LOAD_HISTORY_TO_API: Unlocked DB Mutex\n");
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
    char *start = (char *)strstr(text, "<cmd>");
    char *end = (char *)strstr(text, "</cmd>");
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

     char *lt_pl   = g_strdup(global_app->ansi.lt_purple);
     char *cy      = g_strdup(global_app->ansi.cyan);
     char *yl      = g_strdup(global_app->ansi.yellow);
     char *gr      = g_strdup(global_app->ansi.green);
     char *red     = g_strdup(global_app->ansi.red);
     char *nml     = g_strdup(global_app->ansi.normal);

    long in_len = strlen(input_text);
    char *noise_result = noise_filter_apply(global_app, input_text);
    DEBUG_PRINT("[%sMEMDBG %s]: [%sBLANK_LINES%s]%s noise_result=%s%p%s len=%s%zu%s input=%s%p%s\n",
                lt_pl, nml, cy, nml, yl, gr, (void*)noise_result,
		yl, gr, noise_result ? strlen(noise_result) : 0,
		yl, gr, (void*)input_text, nml);

    char *input_string = g_strdup(noise_result ? noise_result : "");
    DEBUG_PRINT("[%sMEMDBG %s]: [%sBLANK_LINES%s]%s input_string copy=%s%p%s\n", 
		lt_pl, nml, cy, nml, yl, red, (void*)input_string, nml);
    DEBUG_PRINT("[%sMEMDBG %s]: [%sBLANK_LINES%s]%s FREE noise_result=%s%p%s\n",
		lt_pl, nml, cy, nml, yl, gr,  (void*)noise_result, nml);
    g_free(noise_result);
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
    DEBUG_PRINT("[%s DEBUG %s]: [%sBlank Lines%s]%s Input length: [%s%ld%s] bytes, Output length: [%s%ld%s] bytes. Stripped [%s%d%s] blank lines%s\n",
	lt_pl, nml, cy, nml, yl, red, in_len, yl,
	red, output_buffer->len, yl, red, trimmed_count, yl, nml);

    DEBUG_PRINT("[%sMEMDBG %s]: [%sBLANK_LINES%s]%s FREE input_string=%s%p%s\n",
	lt_pl, nml, cy, nml, yl, gr, (void*)input_string, nml);
    g_free(input_string);
    char *result = g_string_free(output_buffer, FALSE);
    DEBUG_PRINT("[%sMEMDBG %s]: [%sBLANK_LINES%s]%s RETURN result=[%s%p%s] len=[%s%zu%s]%s\n",
	lt_pl, nml, cy,nml, yl, red, (void*)result, yl, red, result ? strlen(result) : 0, yl, nml);

    g_free(cy);
    g_free(yl);
    g_free(gr);
    g_free(red);
    g_free(nml);

    return result;
}

void print_version(AppContext *app) {
    printf("aiterm version:\t%s%-16s%s\n", app->ansi.cyan, AITERM_VERSION, app->ansi.normal);
    printf("Build ID:\t%s%s%s\n", app->ansi.cyan, AITERM_BUILDID, app->ansi.normal);
    printf("Build Time:\t%s%s%s\n", app->ansi.cyan, AITERM_BUILD_TIME, app->ansi.normal);
}


// Updated 0.9.6-omega
void on_initialization_complete(AppContext *app) {

    char *lt_pl   = g_strdup(global_app->ansi.lt_purple);
    char *cy      = g_strdup(global_app->ansi.cyan);
    char *yl      = g_strdup(global_app->ansi.yellow);
    char *gr      = g_strdup(global_app->ansi.green);
    char *red     = g_strdup(global_app->ansi.red);
    char *nml     = g_strdup(global_app->ansi.normal);

    if (app->sys.is_initializing) {
        app->sys.is_initializing = false;
        DEBUG_PRINT("[ %sDEBUG%s ]: [%sInitialization%s]: %scomplete. Normal session active.%s\n",
		lt_pl, nml, cy, nml, yl, nml);
        fflush(stderr);
    }

    g_free(cy);
    g_free(yl);
    g_free(gr);
    g_free(red);
    g_free(nml);

}

// Updated 0.9.6-omega
gboolean on_app_startup_prime(gpointer user_data) {
    AppContext *app = (AppContext *)user_data;
    if (!app) return G_SOURCE_REMOVE;

    char *lt_pl   = g_strdup(global_app->ansi.lt_purple);
    char *cy      = g_strdup(global_app->ansi.cyan);
    char *yl      = g_strdup(global_app->ansi.yellow);
    char *gr      = g_strdup(global_app->ansi.green);
    char *red     = g_strdup(global_app->ansi.red);
    char *nml     = g_strdup(global_app->ansi.normal);

    DEBUG_PRINT("[ %sDEBUG%s ]: [%sINIT%s]: %sPriming selected AI provider after GTK loop start...%s\n",
	lt_pl, nml, cy, nml, yl, nml);
    
    /* Provider-neutral startup request.  This used to call Gemini directly,
     * which meant changing provider still caused a Gemini request here. */
    char *prime_response = ai_provider_send(
        app, "System Initialized. Acknowledge readiness in 1 sentence.");
    if (prime_response) {
        g_free(prime_response);
    }
    on_initialization_complete(app);

    g_free(cy);
    g_free(yl);
    g_free(gr);
    g_free(red);
    g_free(nml);

    return G_SOURCE_REMOVE;
}

