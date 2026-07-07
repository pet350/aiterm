// part of the aiterm project
// utils.h
// Header file for utilities used in aiterm
// By: Peter Talbott
// Assisted by: Gemini and OpenAI
// aiterm The terminal emulator with an AI Pane
// April 2026, MMay 2026, June 2026

#ifndef UTILS_H
#define UTILS_H
#include <pthread.h>
#include <vte/vte.h>
#include <json-c/json.h>
#include "gui.h"

// ANSI Codes for VTE coloring
#define ANSI_CYAN  "\033[1;36m"
#define ANSI_RESET "\033[0m"

// Global Seesion UUID
#define GLOBAL_SESSION_UUID "00000000-0000-0000-0000-000000000000"

char* extract_ai_text(const char *json);

extern AppContext *global_app;
extern int debug_mode;
extern int tee_enabled;

#define DEBUG_PRINT(fmt, ...) \
    do { if (global_app->debug_mode) fprintf(stderr, fmt, ##__VA_ARGS__); } while (0)

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

char* get_uuid_filter(AppContext *app);
char* build_delta_sync_query(AppContext *app);
void init_provider_config(AppContext *app);
void free_provider_config(ProviderConfig *provider);
char* extract_ai_text(const char *json);
char* strip_ansi(const char *input); // Helpful for goal #2
char* read_file_to_string(const char *path);
char* extract_cmd_name(const char *input);
char* extract_ai_command(const char *text);
char* strip_blank_lines(const char *input_string);

int init_remote_db(AppContext *app); // Add this line

void initialize_booleans(AppContext *app);
void* init_db_thread_worker(void *data);
void append_to_view(GtkWidget *view, const char *prefix, const char *text);
void* db_worker_thread(void *arg);
void load_history_to_gemini(AppContext *app, struct json_object *contents_array, const char *current_prompt);
void load_history_to_api(struct json_object *messages_array);
void save_to_history(const char *user_text, const char *ai_text);
void save_tee_to_history(const char *terminal_output, const char *ai_analysis);
void display_all_history(AppContext *app);
void tee_handle_output(AppContext *app, const char *text) ;
void tee_flush_timed(AppContext *app);
void feed_terminal_header(VteTerminal *terminal, const char *msg);

gboolean is_ai_command(const char *text);

#endif


// End of utils.h


