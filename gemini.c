// part of aiterm project
// gemini.c
// functions for sending/recieving data to and from Gemini
// By: Peter Talbott
// Assisted by: Gemini
// July 2026

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

// --- 1. The Core API Logic ---
// This function returns the RAW JSON string from Gemini.
// It is "UI Neutral" so it can run safely in a background thread.
char* perform_gemini_request(AppContext *app, const char *prompt) {
    CURL *curl_handle;
    CURLcode res;
    char url[1024];
    struct MemoryStruct chunk;

    chunk.memory = malloc(1);
    chunk.size = 0;

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

    // 1. Inject baseline history from MariaDB into the contents array first
    load_history_to_gemini(app, contents, prompt);

    // 2. Now we can safely capture the turn count baseline
    int total_history_turns = json_object_array_length(contents);

    // 3. Check if we already have a valid cache matching the current history depth
    gboolean use_cache = gemini_cache_is_valid(app, total_history_turns);

    // 4. If no valid cache exists, request a new cache using the unified AppContext helper
    if (!use_cache && total_history_turns > 0) {
        if (gemini_cache_create(app, contents)) {
            use_cache = TRUE;
        }
    }

    // 5. If using a valid cache, slice the history array to only transmit the delta
    if (use_cache && app->gemini_cache.id != NULL) {
        struct json_object *trimmed_contents = json_object_new_array();
        for (int i = app->gemini_cache.turn_count; i < total_history_turns; i++) {
            struct json_object *turn = json_object_array_get_idx(contents, i);
            json_object_get(turn); // Increment reference count safely
            json_object_array_add(trimmed_contents, turn);
        }
        json_object_put(contents);   // Drop full base history array cleanly
        contents = trimmed_contents; // Swap delta back into place

        // Inject the server-side cache reference handle to the root layout
        json_object_object_add(root, "cachedContent", json_object_new_string(app->gemini_cache.id));
    }

    // 6. Construct and append the immediate foreground prompt transaction payload
    struct json_object *content_obj = json_object_new_object();
    struct json_object *parts = json_object_new_array();
    struct json_object *part_text = json_object_new_object();

    // Build the full prompt with the wrapped TEE chunk
    char *full_prompt = g_strdup_printf(
        "%s\n\nUSER INSTRUCTION: %s",
        tee_chunk, prompt);

    json_object_object_add(part_text, "text", json_object_new_string(full_prompt));
    json_object_array_add(parts, part_text);
    json_object_object_add(content_obj, "parts", parts);
    
    // Append current conversation turn to the end of our history array/delta
    json_object_array_add(contents, content_obj);
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

    curl_handle = curl_easy_init();
    if (curl_handle) {
        struct curl_slist *headers = curl_slist_append(NULL, "Content-Type: application/json");
        curl_easy_setopt(curl_handle, CURLOPT_URL, url);
        curl_easy_setopt(curl_handle, CURLOPT_POSTFIELDS, post_data);
        curl_easy_setopt(curl_handle, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
        curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)&chunk);

        // --- Fail-safe boundaries ---
        curl_easy_setopt(curl_handle, CURLOPT_CONNECTTIMEOUT, 10L);  
        curl_easy_setopt(curl_handle, CURLOPT_TIMEOUT, 30L);         
        curl_easy_setopt(curl_handle, CURLOPT_TCP_KEEPALIVE, 1L);    

        res = curl_easy_perform(curl_handle);
        if (res != CURLE_OK) {
            DEBUG_PRINT("[DEBUG]: CURL Error: %s\n", curl_easy_strerror(res));
        } else {
            DEBUG_PRINT("[DEBUG]: \n--- RAW GEMINI RESPONSE ---\n%s\n--------------------------\n", chunk.memory);
            
            struct json_object *root_obj = json_tokener_parse(chunk.memory);
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
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl_handle);
    }

    g_free(full_prompt);
    g_free(tee_chunk);
    if (screen_text) g_free(screen_text);
    json_object_put(root);

    return chunk.memory; // Return the raw JSON (Caller must free)
}

// --- 2. The Background Thread Worker ---
gpointer ai_thread_func(gpointer data) {
    AIThreadData *td = (AIThreadData *)data;
    if (!td) return NULL;

    char *raw_json = NULL;

    // Route to correct provider
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

    // Hand back to the Main UI Thread
    g_idle_add((GSourceFunc)update_gui_with_response, rd);

    g_free(td->prompt);
    g_free(td);
    return NULL;
}

// Compatibility wrapper for tee_handler.c and other legacy calls
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
    DEBUG_PRINT("[DEBUG] SEND_TO_GEMINI: set ai_busy flag TRUE\n");
    char *data = g_strdup(perform_gemini_request(app, output));
    app->sys.ai_busy = FALSE;
    DEBUG_PRINT("[DEBUG] SEND_TO_GEMINI: Cleared ai_busy flag, returning %ld bytes\n", sizeof(data));
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
