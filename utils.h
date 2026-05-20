/*  utils.h
*  Header file for utilities used in aiterm
*  By: Peter Talbott
*  Assisted by: Gemini and OpenAI
*  aiterm The terminal emulator with an AI Pane
*/

#ifndef UTILS_H
#define UTILS_H
#include <pthread.h>
#include <json-c/json.h>
#include "gui.h"

char* extract_ai_text(const char *json);
extern int debug_mode;
extern int tee_enabled;

#define DEBUG_PRINT(fmt, ...) \
    do { if (debug_mode) fprintf(stderr, fmt, ##__VA_ARGS__); } while (0)

typedef struct {
    char *user_text;
    char *ai_text;
} HistoryEntry;

typedef struct MemoryStruct {
    char *memory;
    size_t size;
} MemoryStruct;

// Structure to pass data to the background DB worker
typedef struct {
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
extern const char* CONFIG_FILE;
size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp);
char* extract_ai_text(const char *json);
char* strip_ansi(const char *input); // Helpful for goal #2
char* strip_prompt(const char *input);
char* read_file_to_string(const char *path);
int init_remote_db(AppContext *app); // Add this line
void append_to_view(GtkWidget *view, const char *prefix, const char *text);
void* db_worker_thread(void *arg);
void load_config(AppContext *app);
void load_smart_history(AppContext *app, struct json_object *target_array, const char *current_prompt, int is_gemini);
void load_history_to_gemini(AppContext *app, struct json_object *contents_array, const char *current_prompt);
void load_history_to_api(struct json_object *messages_array);
void save_config(AppContext *app);
void save_to_history(const char *user_text, const char *ai_text);
void save_tee_to_history(const char *terminal_output, const char *ai_analysis);
void display_all_history(AppContext *app);
void display_status(AppContext *app);
void tee_handle_output(AppContext *app, const char *text) ;
void tee_flush_timed(AppContext *app);

#endif


// End of utils.h
