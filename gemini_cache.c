// part of aiterm project
// gemini_cache.c
// Implementation of Gemini Context Caching
// By: Peter Talbott
// Assisted by: Gemini
// May 2026 - August 2026

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include <json-c/json.h>

#include "gemini_cache.h"
#include "gui.h"
#include "gemini.h"
#include "update.h"
#include "utils.h"

char *get_cache_dir(void) {
    const char *user_cache = g_get_user_cache_dir();
    char *dir = g_build_filename(user_cache, "aiterm", "gemini", NULL);
    g_mkdir_with_parents(dir, 0700);
    return dir;
}

char *gemini_cache_lookup(const char *prompt_hash) {
    if (!prompt_hash || strlen(prompt_hash) == 0) {
        return NULL;
    }

    char *dir = get_cache_dir();
    char *filename = g_strdup_printf("%s.json", prompt_hash);
    char *filepath = g_build_filename(dir, filename, NULL);
    g_free(filename);
    g_free(dir);

    char *content = NULL;
    gsize length = 0;

    if (g_file_get_contents(filepath, &content, &length, NULL)) {
        g_free(filepath);
        return content;
    }

    g_free(filepath);
    return NULL;
}

void gemini_cache_store(const char *prompt_hash, const char *response_json) {
    if (!prompt_hash || !response_json || strlen(prompt_hash) == 0) {
        return;
    }

    char *dir = get_cache_dir();
    char *filename = g_strdup_printf("%s.json", prompt_hash);
    char *filepath = g_build_filename(dir, filename, NULL);
    g_free(filename);
    g_free(dir);

    g_file_set_contents(filepath, response_json, -1, NULL);
    g_free(filepath);
}

// Safely initialize the caching sub-structure inside AppContext
void gemini_cache_init(AppContext *app) {
    if (!app) return;
    app->gemini_cache.id = NULL;
    app->gemini_cache.created_at = 0;
    app->gemini_cache.turn_count = 0;
    if (!app->gemini_cache.min_token_threshold) {
        app->gemini_cache.min_token_threshold = GEMINI_CACHE_MIN_TOKEN_FLOOR;
    }
    DEBUG_PRINT("[DEBUG]: [Smart Cache] initialized. Minimum threshold set to %ld tokens.\n", 
                app->gemini_cache.min_token_threshold);
}

// Clear active caching parameters and release dynamic memory
void gemini_cache_clear(AppContext *app) {
    if (!app) return;
    if (app->gemini_cache.id) {
        g_free(app->gemini_cache.id);
        app->gemini_cache.id = NULL;
    }
    app->gemini_cache.created_at = 0;
    app->gemini_cache.turn_count = 0;
}

// Validate active cache lifetime and history integrity
gboolean gemini_cache_is_valid(AppContext *app, int current_history_turns) {
    if (!app || app->gemini_cache.id == NULL) {
        return FALSE;
    }

    time_t now = time(NULL);

    // Invalidate if cache age exceeds 50 minutes (3000s) or history turn count dropped
    if ((now - app->gemini_cache.created_at) >= (GEMINI_CACHE_DEFAULT_TTL_SEC - GEMINI_CACHE_TTL_MARGIN_SEC) || 
        current_history_turns < app->gemini_cache.turn_count) {
        DEBUG_PRINT("[DEBUG]: Invalidating old, expired, or mismatched Gemini Context Cache.\n");
        gemini_cache_clear(app);
        return FALSE;
    }

    return TRUE;
}

// Generate physical cache handle on the Gemini server side
gboolean gemini_cache_create(AppContext *app, struct json_object *contents) {
    if (!app || !contents) {
        return FALSE;
    }

    long pending_cache_tokens = GEMINI_CACHE_MIN_TOKEN_FLOOR;
    if (app->tokens.current) {
        pending_cache_tokens = app->tokens.current;
        DEBUG_PRINT("[DEBUG] [Cache Create] Pending Cache Tokens %ld\n", pending_cache_tokens);
    } else {
        DEBUG_PRINT("[DEBUG] [Cache Create] Set Default Pending Cache Tokens %ld\n", pending_cache_tokens);
    }

    // Safeguard 1: Don't cache if below local threshold floor
    if (pending_cache_tokens < app->gemini_cache.min_token_threshold) {
        DEBUG_PRINT("[DEBUG]: Cache creation bypassed. Current tokens (%ld) below floor (%ld).\n",
                    pending_cache_tokens, app->gemini_cache.min_token_threshold);
        return FALSE;
    }

    // Safeguard 2: Threshold < 0 disables explicit caching overhead entirely
    if (app->gemini_cache.min_token_threshold < 0) {
        DEBUG_PRINT("[DEBUG]: Cache bypassed. Explicit caching disabled via threshold setting.\n");
        return FALSE;
    }

    gemini_cache_clear(app);

    int turn_count = json_object_array_length(contents);
    if (turn_count == 0) {
        return FALSE;
    }

    ProviderConfig *api = &app->provider_config;
    const char *model = (api->model && strlen(api->model) > 0) ? api->model : "gemini-1.5-flash-lite";
    const char *base_url = (api->base_url && strlen(api->base_url) > 0) ? api->base_url : "https://generativelanguage.googleapis.com/v1beta";
    const char *query_key = (api->query_key_name && strlen(api->query_key_name) > 0) ? api->query_key_name : "key";
    const char *api_key = api->api_key;

    char model_buf[256];
    if (strncmp(model, "models/", 7) == 0) {
        snprintf(model_buf, sizeof(model_buf), "%s", model);
    } else {
        snprintf(model_buf, sizeof(model_buf), "models/%s", model);
    }

    struct json_object *root = json_object_new_object();
    json_object_object_add(root, "model", json_object_new_string(model_buf));
    json_object_object_add(root, "contents", json_object_get(contents));
    json_object_object_add(root, "ttl", json_object_new_string("3600s"));
    json_object_object_add(root, "displayName", json_object_new_string("aiterm_history_cache"));

    const char *post_data = json_object_to_json_string(root);

    char url[1024];
    snprintf(url, sizeof(url), "%s/cachedContents?%s=%s", base_url, query_key, api_key ? api_key : "");

    struct {
        char *memory;
        size_t size;
    } chunk;
    chunk.memory = malloc(1);
    chunk.size = 0;

    CURL *curl_handle = curl_easy_init();
    gboolean success = FALSE;

    if (curl_handle) {
        struct curl_slist *headers = curl_slist_append(NULL, "Content-Type: application/json");
        curl_easy_setopt(curl_handle, CURLOPT_URL, url);
        curl_easy_setopt(curl_handle, CURLOPT_POSTFIELDS, post_data);
        curl_easy_setopt(curl_handle, CURLOPT_HTTPHEADER, headers);

        extern size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp);
        curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
        curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)&chunk);

        curl_easy_setopt(curl_handle, CURLOPT_CONNECTTIMEOUT, 10L);
        curl_easy_setopt(curl_handle, CURLOPT_TIMEOUT, 30L);

        CURLcode res = curl_easy_perform(curl_handle);
        if (res == CURLE_OK) {
            struct json_object *resp_root = json_tokener_parse(chunk.memory);
            if (resp_root) {
                struct json_object *name_obj = NULL;
                if (json_object_object_get_ex(resp_root, "name", &name_obj)) {
                    app->gemini_cache.id = g_strdup(json_object_get_string(name_obj));
                    app->gemini_cache.created_at = time(NULL);
                    app->gemini_cache.turn_count = turn_count;
                    success = TRUE;
                    DEBUG_PRINT("[DEBUG]: Gemini Context Cache Created! ID: %s, Cached Turns: %d\n", 
                                app->gemini_cache.id, app->gemini_cache.turn_count);
                } else {
                    struct json_object *error_obj = NULL, *msg_obj = NULL;
                    if (json_object_object_get_ex(resp_root, "error", &error_obj) &&
                        json_object_object_get_ex(error_obj, "message", &msg_obj)) {
                        DEBUG_PRINT("[DEBUG]: Gemini Cache Creation Skipped: %s\n", 
                                    json_object_get_string(msg_obj));
                    } else {
                        DEBUG_PRINT("[DEBUG]: Gemini Cache Creation Failed: Unexpected JSON format.\n");
                    }
                }
                json_object_put(resp_root);
            }
        } else {
            DEBUG_PRINT("[DEBUG]: CURL error during Cache Creation: %s\n", curl_easy_strerror(res));
        }

        curl_slist_free_all(headers);
        curl_easy_cleanup(curl_handle);
    }

    json_object_put(root);
    free(chunk.memory);
    return success;
}

// Build query payload using json-c
char *gemini_cache_build_query_payload(AppContext *app, const char *new_prompt_text) {
    if (!app || !new_prompt_text) return NULL;

    struct json_object *root = json_object_new_object();

    if (app->gemini_cache.id && gemini_cache_is_valid(app, app->gemini_cache.turn_count)) {
        json_object_object_add(root, "cachedContent", json_object_new_string(app->gemini_cache.id));
    }

    struct json_object *contents_arr = json_object_new_array();
    struct json_object *turn_obj = json_object_new_object();
    json_object_object_add(turn_obj, "role", json_object_new_string("user"));

    struct json_object *parts_arr = json_object_new_array();
    struct json_object *part_obj = json_object_new_object();
    json_object_object_add(part_obj, "text", json_object_new_string(new_prompt_text));

    json_object_array_add(parts_arr, part_obj);
    json_object_object_add(turn_obj, "parts", parts_arr);
    json_object_array_add(contents_arr, turn_obj);

    json_object_object_add(root, "contents", contents_arr);

    const char *json_str = json_object_to_json_string(root);
    char *result = json_str ? g_strdup(json_str) : NULL;

    json_object_put(root);
    return result;
}

// Invalidate active cache and release allocated handle
void gemini_cache_invalidate(AppContext *app) {
    if (!app) return;

    DEBUG_PRINT("[DEBUG]: Manual cache invalidation triggered.\n");

    if (app->gemini_cache.id && app->provider_config.api_key) {
        ProviderConfig *api = &app->provider_config;
        const char *base_url = (api->base_url && strlen(api->base_url) > 0) ? api->base_url : "https://generativelanguage.googleapis.com/v1beta";
        const char *query_key = (api->query_key_name && strlen(api->query_key_name) > 0) ? api->query_key_name : "key";

        char url[1024];
        snprintf(url, sizeof(url), "%s/%s?%s=%s", base_url, app->gemini_cache.id, query_key, api->api_key);

        CURL *curl_handle = curl_easy_init();
        if (curl_handle) {
            curl_easy_setopt(curl_handle, CURLOPT_URL, url);
            curl_easy_setopt(curl_handle, CURLOPT_CUSTOMREQUEST, "DELETE");
            curl_easy_setopt(curl_handle, CURLOPT_TIMEOUT, 5L);
            
            curl_easy_perform(curl_handle);
            curl_easy_cleanup(curl_handle);
        }
    }

    gemini_cache_clear(app);
}


