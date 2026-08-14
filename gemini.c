// part of aiterm project
// gemini.c
// functions for sending/recieving data to and from Gemini
// By: Peter Talbott
// Assisted by: Gemini
// March 2026 - August 2026

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include <json-c/json.h>
#include <mariadb/mysql.h>
#include <vte/vte.h>

#include "gemini.h"
#include "gui.h"
#include "gemini_cache.h"
#include "utils.h"
#include "update.h"
#include "openai.h"
#include "session_manager.h"
#include "noisefilter.h"
#include "ai_retry.h"

typedef struct {
    AppContext *app;
    const char *url;
} GeminiHttpData;


extern const char *GENERAL_DIRECTIVES;

// Single-attempt HTTP dispatch function compatible with ai_retry_execute_with_retry
static char *gemini_http_single_attempt(const char *payload, long *out_http_code, void *user_data) {
    GeminiHttpData *data = (GeminiHttpData *)user_data;
    if (!data || !data->app || !data->url) return NULL;

    CURL *curl_handle;
    CURLcode res;
    struct MemoryStruct chunk;
    chunk.memory = malloc(1);
    chunk.size = 0;
    long http_code = 0;

    curl_handle = curl_easy_init();
    if (curl_handle) {
        struct curl_slist *headers = curl_slist_append(NULL, "Content-Type: application/json");
        curl_easy_setopt(curl_handle, CURLOPT_URL, data->url);
        curl_easy_setopt(curl_handle, CURLOPT_POSTFIELDS, payload);
        curl_easy_setopt(curl_handle, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
        curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)&chunk);

        // Fail-safe boundaries
        curl_easy_setopt(curl_handle, CURLOPT_CONNECTTIMEOUT, 10L);  
        curl_easy_setopt(curl_handle, CURLOPT_TIMEOUT, 30L);         
        curl_easy_setopt(curl_handle, CURLOPT_TCP_KEEPALIVE, 1L);    

        res = curl_easy_perform(curl_handle);
        if (res == CURLE_OK) {
            curl_easy_getinfo(curl_handle, CURLINFO_RESPONSE_CODE, &http_code);
        } else {
            DEBUG_PRINT("[DEBUG]: CURL Error: %s\n", curl_easy_strerror(res));
            http_code = 0;
        }

        curl_slist_free_all(headers);
        curl_easy_cleanup(curl_handle);
    }

    if (out_http_code) {
        *out_http_code = http_code;
    }

    return chunk.memory;
}

// --- 1. The Core API Logic ---
// UI-neutral execution wrapped with AI Retry handler
char* perform_gemini_request(AppContext *app, const char *prompt) {
    char url[1024];

    char *screen_text = NULL;
    if (VTE_IS_TERMINAL(app->gui.terminal_view)) {
        VteTerminal *vte = VTE_TERMINAL(app->gui.terminal_view);
        long row, col;
        vte_terminal_get_cursor_position(vte, &col, &row);
        long context_depth = 1000;
        long start_row = (row > context_depth) ? (row - context_depth) : 0;
        screen_text = vte_terminal_get_text_range(vte, start_row, 0, row, col, NULL, NULL, NULL);
    }

    // Wrap the screen text in our session-aware XML tag
    char *tee_chunk = session_create_tee_chunk(app, screen_text ? screen_text : "None");

    struct json_object *root = json_object_new_object();
    struct json_object *contents = json_object_new_array();

    // 1. System Instruction Injection (Root Level)
    if (GENERAL_DIRECTIVES && GENERAL_DIRECTIVES[0] != '\0') {
        struct json_object *sys_instruction = json_object_new_object();
        struct json_object *sys_parts = json_object_new_array();
        struct json_object *sys_part = json_object_new_object();

        json_object_object_add(sys_part, "text", json_object_new_string(GENERAL_DIRECTIVES));
        json_object_array_add(sys_parts, sys_part);
        json_object_object_add(sys_instruction, "parts", sys_parts);
        json_object_object_add(root, "system_instruction", sys_instruction);
    } else {
        DEBUG_PRINT("[DEBUG]: [Send to Gemini] GENERAL_DIRECTIVES is NULL or empty.\n");
    }

    // 2. Load History from Database (Only when not initializing)
    if (!app->sys.is_initializing) {
        DEBUG_PRINT("[DEBUG]: [Perform Gemini Request] Not Initializing, Loading History\n");
        load_history_to_gemini(app, contents, prompt);
    } else {
        DEBUG_PRINT("[DEBUG]: [Init Phase] Bypassing history retrieval.\n");
    }

    // Compute prompt hash for cache lookup/storage
    char *prompt_hash = g_compute_checksum_for_string(G_CHECKSUM_SHA256, prompt, -1);

    // Check local cache first
    char *cached_res = gemini_cache_lookup(prompt_hash);
    if (cached_res != NULL) {
        g_free(prompt_hash);
        g_free(tee_chunk);
        if (screen_text) g_free(screen_text);
        json_object_put(root);
        return cached_res; 
    }

    int total_history_turns = json_object_array_length(contents);

    // Slice history array if using active server-side cache
    if (app->sys.smart_cache_enabled && app->gemini_cache.id != NULL) {
        struct json_object *trimmed_contents = json_object_new_array();
        for (int i = app->gemini_cache.turn_count; i < total_history_turns; i++) {
            struct json_object *turn = json_object_array_get_idx(contents, i);
            json_object_get(turn);
            json_object_array_add(trimmed_contents, turn);
        }
        json_object_put(contents);
        contents = trimmed_contents;

        json_object_object_add(root, "cachedContent", json_object_new_string(app->gemini_cache.id));
    }

    // 3. Append SINGLE Final User Turn (Terminal Screen + Prompt)
    struct json_object *user_msg = json_object_new_object();
    struct json_object *user_parts = json_object_new_array();
    struct json_object *user_part = json_object_new_object();

    char *full_prompt = g_strdup_printf("%s\n\nUSER INSTRUCTION: %s", tee_chunk, prompt);

    json_object_object_add(user_part, "text", json_object_new_string(full_prompt));
    json_object_array_add(user_parts, user_part);
    json_object_object_add(user_msg, "role", json_object_new_string("user"));
    json_object_object_add(user_msg, "parts", user_parts);
    
    json_object_array_add(contents, user_msg);
    json_object_object_add(root, "contents", contents);

    const char *post_data = json_object_to_json_string(root);

    ProviderConfig *provider = &app->provider_config;
    const char *base_url = provider->base_url ? provider->base_url : "https://generativelanguage.googleapis.com/v1beta";
    const char *endpoint = provider->endpoint ? provider->endpoint : "models/%s:generateContent";
    const char *model = provider->model ? provider->model : "gemini-3.1-flash-lite";
    const char *query_key = provider->query_key_name ? provider->query_key_name : "key";
    const char *api_key = provider->api_key ? provider->api_key : app->security.api_key;

    char endpoint_path[512];
    snprintf(endpoint_path, sizeof(endpoint_path), endpoint, model);
    snprintf(url, sizeof(url), "%s/%s?%s=%s", base_url, endpoint_path, query_key, api_key ? api_key : "");

    if (app->sys.ratelimit_enabled) {
        ratelimit_wait_if_needed(&app->limiter);
    }

    // Delegate HTTP request execution and backoff logic to AI Retry subsystem
    GeminiHttpData http_data = {
        .app = app,
        .url = url
    };

    long final_http_code = 0;
    char *raw_json = ai_retry_execute_with_retry(app, gemini_http_single_attempt, post_data, &http_data, &final_http_code);

    if (raw_json != NULL) {
        DEBUG_PRINT("[DEBUG]: \n--- RAW GEMINI RESPONSE (HTTP %ld) ---\n%s\n--------------------------\n", final_http_code, raw_json);
        
        // Cache successful responses
        if (final_http_code == 200) {
            gemini_cache_store(prompt_hash, raw_json);
        }

        struct json_object *root_obj = json_tokener_parse(raw_json);
        if (root_obj) {
            struct json_object *usage_meta;
            if (json_object_object_get_ex(root_obj, "usageMetadata", &usage_meta)) {
                struct json_object *total_toks = NULL;
                struct json_object *cand_toks = NULL;
                
                if (json_object_object_get_ex(usage_meta, "totalTokenCount", &total_toks)) {
                    app->tokens.current = json_object_get_int64(total_toks);
                }
                if (json_object_object_get_ex(usage_meta, "candidatesTokenCount", &cand_toks)) {
                    app->tokens.last = json_object_get_int64(cand_toks);
                }
                
                extern gboolean refresh_token_display(gpointer data);
                g_idle_add(refresh_token_display, app);
            }
            json_object_put(root_obj); 
        }
    }

    g_free(prompt_hash);
    g_free(full_prompt);
    g_free(tee_chunk);
    if (screen_text) g_free(screen_text);
    json_object_put(root);

    return raw_json;
}

// --- 2. Background Thread Worker ---
gpointer ai_thread_func(gpointer data) {
    AIThreadData *td = (AIThreadData *)data;
    if (!td) return NULL;

    char *raw_json = NULL;

    if (td->app->provider_config.kind == PROVIDER_KIND_GEMINI_GENERATE) {
        raw_json = perform_gemini_request(td->app, td->prompt);
    } else {
        raw_json = send_to_openai(td->app, td->prompt);
    }

    char *final_text = NULL;
    if (raw_json) {
        struct json_object *root_obj = json_tokener_parse(raw_json);
        if (root_obj) {
            extern gboolean refresh_token_display(gpointer data);

            if (td->app->provider_config.kind == PROVIDER_KIND_GEMINI_GENERATE) {
                struct json_object *usage_meta;
                if (json_object_object_get_ex(root_obj, "usageMetadata", &usage_meta)) {
                    struct json_object *total_toks = NULL;
                    struct json_object *cand_toks = NULL;
                    
                    if (json_object_object_get_ex(usage_meta, "totalTokenCount", &total_toks)) {
                        td->app->tokens.current = json_object_get_int64(total_toks);
                    }
                    if (json_object_object_get_ex(usage_meta, "candidatesTokenCount", &cand_toks)) {
                        td->app->tokens.last = json_object_get_int64(cand_toks);
                    }
                    g_idle_add(refresh_token_display, td->app);
                }
            } else {
                struct json_object *usage_obj;
                if (json_object_object_get_ex(root_obj, "usage", &usage_obj)) {
                    struct json_object *total_toks = NULL;
                    struct json_object *comp_toks = NULL;
                    
                    if (json_object_object_get_ex(usage_obj, "total_tokens", &total_toks)) {
                        td->app->tokens.current = json_object_get_int64(total_toks);
                    }
                    if (json_object_object_get_ex(usage_obj, "completion_tokens", &comp_toks)) {
                        td->app->tokens.last = json_object_get_int64(comp_toks);
                    }
                    g_idle_add(refresh_token_display, td->app);
                }
            }
            json_object_put(root_obj); 
        }

        final_text = extract_ai_text(raw_json);
        if (final_text) {
            save_to_history(td->prompt, final_text);
        }
        free(raw_json);
    }

    AIResponseData *rd = g_malloc0(sizeof(AIResponseData));
    rd->app = td->app;
    rd->response_text = final_text;
    rd->original_prompt = g_strdup(td->prompt);

    g_idle_add((GSourceFunc)update_gui_with_response, rd);

    g_free(td->prompt);
    g_free(td);
    return NULL;
}

// Legacy compatibility wrapper
char* send_to_gemini(AppContext *app, const char *prompt) {
    if (app->sys.ratelimit_enabled) {
        ratelimit_wait_if_needed(&app->limiter);
    }
    if (app->sys.ai_busy) {
        DEBUG_PRINT("[DEBUG] SEND_TO_GEMINI: ai_busy flag set not executing perform_gemini_request\n");
        return NULL;
    }

    char *output = g_strdup(noise_filter_apply(app, prompt));
    app->sys.ai_busy = TRUE;
    DEBUG_PRINT("[DEBUG]: [SEND_TO_GEMINI] set ai_busy flag TRUE\n");
    char *data = perform_gemini_request(app, output);
    app->sys.ai_busy = FALSE;
    DEBUG_PRINT("[DEBUG]: [SEND_TO_GEMINI] Cleared ai_busy flag, returning response\n");
    g_free(output);
    return data;
}

char* gemini_list_models(AppContext *app) {
    CURL *curl;
    CURLcode res;
    struct MemoryStruct chunk;
    struct json_object *root = NULL;
    struct json_object *models_array = NULL;
    GString *model_output_str = g_string_new("");

    chunk.memory = malloc(1);  
    chunk.size = 0;    

    curl_global_init(CURL_GLOBAL_ALL);
    curl = curl_easy_init();

    if (curl) {
        const char *url = "https://generativelanguage.googleapis.com/v1beta/models";

        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "libcurl-agent/1.0");

        struct curl_slist *headers = NULL;
        char api_key_header[256];
        snprintf(api_key_header, sizeof(api_key_header), "X-Goog-Api-Key: %s", app->security.api_key);
        headers = curl_slist_append(headers, api_key_header);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

        DEBUG_PRINT("[DEBUG]: [Gemini Models] Fetching models from: %s\n", url);

        res = curl_easy_perform(curl);

        if (res != CURLE_OK) {
            DEBUG_PRINT("[DEBUG]: [Gemini Models] curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
            g_string_append_printf(model_output_str, "Error: Failed to fetch models from Gemini API: %s\n", curl_easy_strerror(res));
        } else {
            DEBUG_PRINT("[DEBUG]: [Gemini Models] Raw response: %s\n", chunk.memory);
            root = json_tokener_parse(chunk.memory);
            if (root == NULL) {
                g_string_append(model_output_str, "Error: Failed to parse Gemini API response (invalid JSON).\n");
            } else if (json_object_object_get_ex(root, "models", &models_array) && json_object_get_type(models_array) == json_type_array) {
                int num_models = json_object_array_length(models_array);
                g_string_append_printf(model_output_str, "Found %d Gemini Models:\n\n", num_models);
                for (int i = 0; i < num_models; i++) {
                    struct json_object *model_obj = json_object_array_get_idx(models_array, i);
                    if (model_obj) {
                        struct json_object *name_obj, *display_name_obj, *version_obj, *description_obj;
                        const char *name = NULL, *display_name = NULL, *version = NULL, *description = NULL;

                        if (json_object_object_get_ex(model_obj, "name", &name_obj))
                            name = json_object_get_string(name_obj);
                        if (json_object_object_get_ex(model_obj, "displayName", &display_name_obj))
                            display_name = json_object_get_string(display_name_obj);
                        if (json_object_object_get_ex(model_obj, "version", &version_obj))
                            version = json_object_get_string(version_obj);
                        if (json_object_object_get_ex(model_obj, "description", &description_obj))
                            description = json_object_get_string(description_obj);

                        g_string_append_printf(model_output_str, "Name: %s\n", name ? name : "N/A");
                        g_string_append_printf(model_output_str, "Display Name: %s\n", display_name ? display_name : "N/A");
                        g_string_append_printf(model_output_str, "Version: %s\n", version ? version : "N/A");
                        g_string_append_printf(model_output_str, "Description: %s\n", description ? description : "N/A");
                        g_string_append(model_output_str, "-----------------------------------\n");
                    }
                }
            } else {
                struct json_object *error_obj, *message_obj;
                if (json_object_object_get_ex(root, "error", &error_obj) &&
                    json_object_object_get_ex(error_obj, "message", &message_obj)) {
                    g_string_append_printf(model_output_str, "Gemini API Error: %s\n", json_object_get_string(message_obj));
                } else {
                    g_string_append(model_output_str, "Error: Unexpected Gemini API response format.\n");
                }
            }
        }

        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
    } else {
        g_string_append(model_output_str, "Error: Could not initialize libcurl.\n");
    }

    if (root) json_object_put(root); 
    free(chunk.memory); 
    curl_global_cleanup();

    return g_string_free(model_output_str, FALSE); 
}

