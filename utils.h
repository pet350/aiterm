// part of the aiterm project
// utils.h
// Header file for utilities used in aiterm
// By: Peter Talbott
// Assisted by: Gemini and OpenAI
// aiterm The terminal emulator with an AI Pane
// April 2026, MMay 2026, June 2026

#ifndef UTILS_H
#define UTILS_H

#include <stdio.h>
#include <time.h>
#include <pthread.h>
#include <vte/vte.h>
#include <json-c/json.h>
#include <sys/prctl.h>
#include "gui.h"
#include "xml_tagging.h"

// ANSI Codes for VTE coloring
#define ANSI_CYAN  "\033[1;36m"
#define ANSI_RESET "\033[0m"
#define ANSI_NORMAL "\033[0m"
#define ANSI_BLACK "\033[0;30m"
#define ANSI_RED "\033[0;31m"
#define ANSI_GREEN "\033[0;32m"
#define ANSI_ORANGE "\033[0;33m"
#define ANSI_BLUE "\033[0;34m"
#define ANSI_PURPLE "\033[0;35m"
#define ANSI_LT_GRAY "\033[0;37m"
#define ANSI_DK_GRAY "\033[1;30m"
#define ANSI_LT_RED "\033[1;31m"
#define ANSI_LT_GREEN "\033[1;32m"
#define ANSI_YELLOW "\033[1;33m"
#define ANSI_LT_BLUE "\033[1;34m"
#define ANSI_LT_PURPLE "\033[1;35m"
#define ANSI_LT_CYAN "\033[1;36m"
#define ANSI_WHITE "\033[1;37m"

// Global Seesion UUID
#define GLOBAL_SESSION_UUID "00000000-0000-0000-0000-000000000000"

char* extract_ai_text(const char *json);

extern AppContext *global_app;
extern int debug_mode;
extern int tee_enabled;

extern const char* AITERM_VERSION;
extern const char* AITERM_BUILDID;
extern const char* AITERM_BUILD_TIME;
extern const char* CONFIG_FILE;
extern const char* GENERAL_DIRECTIVES;

#define DEBUG_PRINT(fmt, ...) \
    do { \
        if (global_app && global_app->sys.debug_mode) { \
            struct timespec ts; \
            clock_gettime(CLOCK_MONOTONIC, &ts); \
            fprintf(stderr, "[%s%5ld.%06ld%s] " fmt, \
               ANSI_YELLOW, (long)ts.tv_sec, (long)(ts.tv_nsec / 1000), ANSI_NORMAL, ##__VA_ARGS__); \
        } \
    } while (0)

#define DBG_PRINT(fmt, ...) \
     do { if (global_app && global_app->sys.debug_mode) fprintf(stderr, fmt, ##__VA_ARGS__); } while (0)

#define SET_THREAD_NAME(name) prctl(PR_SET_NAME, name, 0, 0, 0)

typedef struct {
    char *user_text;
    char *ai_text;
} HistoryEntry;

typedef struct MemoryStruct {
    char *memory;
    size_t size;
} MemoryStruct;

typedef enum {
    JOB_SAVE_HISTORY,
    JOB_SAVE_TEE,
    JOB_SAVE_KEYWORDS,
    JOB_LOAD_HISTORY_SMART,
    JOB_LOAD_HISTORY_GEMINI
} JobType;

// Structure to pass data to the background DB worker
typedef struct {
    JobType type;
    AppContext *app;
    struct json_object *target_array;

    char *terminal_output;
    char *ai_analysis;
    char *user_text;
    char *ai_text;
    char *session_uuid;
    char *history_role;       /* terminal, snmp, etc. */

    int sequence_id;
    int is_tee;
    // We copy these strings so the main thread can keep moving
} DBWorkerData;

// function prototype
extern HistoryEntry history[5];
extern int history_count;
extern const char* AITERM_VERSION;
extern const char* AITERM_BUILDID;
extern const char* CONFIG_FILE;

size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp);
int init_remote_db(AppContext *app);
int execute_sql_file(MYSQL *conn, const char *filepath);
int init_db_from_directory(MYSQL *conn, const char *dirpath);
const char* get_sql_init_dir(void);

const char* get_config_filename(void);

char* get_uuid_filter(AppContext *app);
char* build_delta_sync_query(AppContext *app);
char* extract_ai_text(const char *json);
char* strip_ansi(const char *input); // Helpful for goal #2
char* read_file_to_string(const char *path);
char* extract_cmd_name(const char *input);
char* extract_ai_command(const char *text);
char* strip_blank_lines(const char *input_text);
char* xml_wrap(AppContext *app, const char *input);

void* init_db_thread_worker(void *data);
void* db_worker_thread(void *arg);

void check_for_root(void);
void init_config_pointer(AppContext *app);
void print_version(AppContext *app);
void init_provider_config(AppContext *app);
void free_provider_config(ProviderConfig *provider);
void init_provider_key_store(AppContext *app);
void clear_provider_key_store(AppContext *app);
const char *get_provider_api_key(AppContext *app, const char *provider_name);
void set_provider_api_key(AppContext *app, const char *provider_name, const char *api_key);
char *provider_key_env_name(const char *provider_name);
void initialize_booleans(AppContext *app);
void append_to_view(GtkWidget *view, const char *prefix, const char *text);
void load_history_to_gemini(AppContext *app, struct json_object *contents_array, const char *current_prompt);
void load_history_to_api(struct json_object *messages_array);
void save_to_history(const char *user_text, const char *ai_text);
void save_tee_to_history(const char *terminal_text, const char *ai_analysis, const char *history_role);
void display_all_history(AppContext *app);
void tee_handle_output(AppContext *app, const char *text) ;
void tee_flush_timed(AppContext *app);
void feed_terminal_header(VteTerminal *terminal, const char *msg);
void on_initialization_complete(AppContext *app);
void init_runtime_queues(AppContext *app);
void check_debug_tty(AppContext *app);
void init_colors(AppContext *app);
void cleanup_colors(AppContext *app);

gboolean check_network_availability(AppContext *app);
gboolean is_ai_command(const char *text);
gboolean on_app_startup_prime(gpointer user_data);

#endif

// End of utils.h


