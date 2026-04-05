#include <stdlib.h>
#include <string.h>
#include <json-c/json.h>
#include "utils.h"
#include "gui.h"
#include "openai.h"

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

char* extract_ai_text(const char *json_str)
{
    struct json_object *root = json_tokener_parse(json_str);
    if (!root) return NULL;

    struct json_object *output_array;
    if (!json_object_object_get_ex(root, "output", &output_array)) {
        json_object_put(root);
        return NULL;
    }

    int len = json_object_array_length(output_array);

    for (int i = 0; i < len; i++) {
        struct json_object *item = json_object_array_get_idx(output_array, i);

        struct json_object *type_obj;
        if (!json_object_object_get_ex(item, "type", &type_obj))
            continue;

        const char *type = json_object_get_string(type_obj);

        if (strcmp(type, "message") == 0) {
            struct json_object *content_array;
            if (!json_object_object_get_ex(item, "content", &content_array))
                continue;

            int clen = json_object_array_length(content_array);

            for (int j = 0; j < clen; j++) {
                struct json_object *content_item = json_object_array_get_idx(content_array, j);

                struct json_object *text_obj;
                if (json_object_object_get_ex(content_item, "text", &text_obj)) {
                    const char *text = json_object_get_string(text_obj);
                    char *result = strdup(text);
                    json_object_put(root);
                    return result;
                }
            }
        }
    }

    json_object_put(root);
    return NULL;
}

void load_config(AppContext *app) {
    app->provider = strdup("openai"); // Default

    FILE *fp = fopen("aiterm.conf", "r");
    if (fp) {
        char line[128];
        while (fgets(line, sizeof(line), fp)) {
            if (strncmp(line, "provider=", 9) == 0) {
                free(app->provider);
                char *val = line + 9;
                val[strcspn(val, "\n")] = 0; // Strip newline
                app->provider = strdup(val);
            }
        }
        fclose(fp);
    }
}

void save_to_history(const char *user_text, const char *ai_text) {
    char path[256];
    snprintf(path, sizeof(path), "%s/.aiterm_history", getenv("HOME"));

    FILE *fp = fopen(path, "a"); // Append mode
    if (fp) {
        // Using a simple delimiter like ::: so it's easy to parse later
        fprintf(fp, "USER:%s\nAI:%s\n---\n", user_text, ai_text);
        fclose(fp);
    }
}

void load_history_to_api(struct json_object *messages_array) {
    char path[256];
    snprintf(path, sizeof(path), "%s/.aiterm_history", getenv("HOME"));

    FILE *fp = fopen(path, "r");
    if (!fp) return;

    char line[1024];
    // This is a simple version; in a full 'tail' you'd seek to the end,
    // but for 0.3, reading the last few lines works.
    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "USER:", 5) == 0) {
            struct json_object *msg = json_object_new_object();
            json_object_object_add(msg, "role", json_object_new_string("user"));
            json_object_object_add(msg, "content", json_object_new_string(line + 5));
            json_object_array_add(messages_array, msg);
        } else if (strncmp(line, "AI:", 3) == 0) {
            struct json_object *msg = json_object_new_object();
            json_object_object_add(msg, "role", json_object_new_string("assistant"));
            json_object_object_add(msg, "content", json_object_new_string(line + 3));
            json_object_array_add(messages_array, msg);
        }
    }
    fclose(fp);
}
