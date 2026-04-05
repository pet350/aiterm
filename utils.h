#ifndef UTILS_H
#define UTILS_H
#include <json-c/json.h>
#include "gui.h"


char* extract_ai_text(const char *json);

typedef struct {
    char *user_text;
    char *ai_text;
} HistoryEntry;

// Instead of HistoryEntry history[5];
extern HistoryEntry history[5];
extern int history_count;

void load_history_to_api(struct json_object *messages_array);
void save_to_history(const char *user_text, const char *ai_text);
char* strip_ansi(const char *input); // Helpful for goal #2
char* strip_prompt(const char *input);

#endif
