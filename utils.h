#ifndef UTILS_H
#define UTILS_H
#include <json-c/json.h>
#include "gui.h"

char* extract_ai_text(const char *json);
extern int debug_mode;

#define DEBUG_PRINT(fmt, ...) \
    do { if (debug_mode) fprintf(stderr, fmt, ##__VA_ARGS__); } while (0)

typedef struct {
    char *user_text;
    char *ai_text;
} HistoryEntry;

// Instead of HistoryEntry history[5];
extern HistoryEntry history[5];
extern int history_count;
extern const char* AITERM_VERSION;

size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp);
void load_history_to_api(struct json_object *messages_array);
void save_to_history(const char *user_text, const char *ai_text);
char* strip_ansi(const char *input); // Helpful for goal #2
char* strip_prompt(const char *input);
void load_config(AppContext *app);
void init_remote_db(AppContext *app); // Add this line
void save_tee_to_history(const char *terminal_output, const char *ai_analysis);
void load_history_to_gemini(struct json_object *contents_array);
void append_to_view(GtkWidget *view, const char *prefix, const char *text);

#endif
