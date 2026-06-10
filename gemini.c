// part of aiterm project
// gemini.c
// functions for sending/recieving data to and from Gemini
// By: Peter Talbott
// Assisted by: Gemini
// May 2026

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include <json-c/json.h>
#include <mariadb/mysql.h>
#include <vte/vte.h>

#include "gemini.h"
#include "gui.h"
#include "utils.h"
#include "update.h"
#include "openai.h"
#include "session_manager.h"

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
    if (VTE_IS_TERMINAL(app->terminal_view)) {
        VteTerminal *vte = VTE_TERMINAL(app->terminal_view);
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

    // Inject history from MariaDB
    load_history_to_gemini(app, contents, prompt);

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
    json_object_array_add(contents, content_obj);
    json_object_object_add(root, "contents", contents);

    const char *post_data = json_object_to_json_string(root);

    snprintf(url, sizeof(url),
        "https://generativelanguage.googleapis.com/v1beta/models/%s:generateContent?key=%s",
        app->model ? app->model : "gemini-3.1-flash-lite", app->api_key);

    if (app->ratelimit_enabled) {
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

        // --- 0.7.5-DELTA FAIL-SAFE BOUNDARIES ---
        curl_easy_setopt(curl_handle, CURLOPT_CONNECTTIMEOUT, 10L);  // Max 10 seconds to connect
        curl_easy_setopt(curl_handle, CURLOPT_TIMEOUT, 30L);         // Max 30 seconds for entire transfer
        curl_easy_setopt(curl_handle, CURLOPT_TCP_KEEPALIVE, 1L);    // Keep the socket fresh

        res = curl_easy_perform(curl_handle);
        if (res != CURLE_OK) {
            DEBUG_PRINT("CURL Error: %s\n", curl_easy_strerror(res));
        } else {
            DEBUG_PRINT("\n--- RAW GEMINI RESPONSE ---\n%s\n--------------------------\n", chunk.memory);
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
    if (td->app->provider && strcasecmp(td->app->provider, "gemini") == 0) {
        raw_json = perform_gemini_request(td->app, td->prompt);
    } else {
        // Assuming OpenAI also returns raw JSON or needs similar wrapping
        raw_json = send_to_openai(td->app->api_key, td->prompt);
    }

    char *final_text = NULL;
    if (raw_json) {
        final_text = extract_ai_text(raw_json);
        if (final_text) {
            // Save to MariaDB while still in background thread (Non-blocking!)
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
    if (app->ratelimit_enabled) {
        ratelimit_wait_if_needed(&app->limiter);
    }
    return perform_gemini_request(app, prompt);
}


char* gemini_list_models(AppContext *app) {
    CURL *curl;
    CURLcode res;
    struct MemoryStruct chunk;
    struct json_object *root = NULL;
    struct json_object *models_array = NULL;
    GString *model_output_str = g_string_new("");

    chunk.memory = malloc(1);  // will be grown as needed by the realloc above
    chunk.size = 0;    // no data at this point

    curl_global_init(CURL_GLOBAL_ALL);
    curl = curl_easy_init();

    if (curl) {
        // Construct the URL for listing models
        const char *url = "https://generativelanguage.googleapis.com/v1beta/models";

        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "libcurl-agent/1.0");

        // Set API key in header (Gemini often uses X-Goog-Api-Key)
        struct curl_slist *headers = NULL;
        char api_key_header[256];
        snprintf(api_key_header, sizeof(api_key_header), "X-Goog-Api-Key: %s", app->api_key);
        headers = curl_slist_append(headers, api_key_header);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

        DEBUG_PRINT("DEBUG: [Gemini Models] Fetching models from: %s\n", url);
        // snprintf(out, sizeof(out), "[Gemini Model List]\n");

        res = curl_easy_perform(curl);

        if (res != CURLE_OK) {
            DEBUG_PRINT("DEBUG: [Gemini Models] curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
            g_string_append_printf(model_output_str, "Error: Failed to fetch models from Gemini API: %s\n", curl_easy_strerror(res));
	    //snprintf(out, sizeof(out), "curl list failed!\n");
        } else {
            DEBUG_PRINT("DEBUG: [Gemini Models] Raw response: %s\n", chunk.memory);
            root = json_tokener_parse(chunk.memory);
            if (root == NULL) {
                g_string_append(model_output_str, "Error: Failed to parse Gemini API response (invalid JSON).\n");
            } else if (json_object_object_get_ex(root, "models", &models_array) && json_object_get_type(models_array) == json_type_array) {
                int num_models = json_object_array_length(models_array);
                g_string_append_printf(model_output_str, "Found %d Gemini Models:\n\n", num_models);
		//snprintf(out, sizeof(out), "Found %d Gemini Models:\n\n", num_models);
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
			// snprintf(out, sizeof(out), "Name: %s\n", name ? name : "N/A");
			// snprintf(out, sizeof(out), "Display Name: %s\n", display_name ? display_name : "N/A");
			// snprintf(out, sizeof(out), "Version: %s\n", version ? version : "N/A");
			// snprintf(out, sizeof(out), "Description: %s\n", description ? description : "N/A");
			// snprintf(out, sizeof(out), "-----------------------------------\n");
                    }
                }
            } else {
                 // Check for API errors reported in the root object (e.g., {"error": {"message": "..."}})
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

    if (root) json_object_put(root); // Free json-c object
    free(chunk.memory); // Free cURL response memory
    curl_global_cleanup();

    return g_string_free(model_output_str, FALSE); // Return the C string
}




//    chunk.memory = malloc(1);
//    chunk.size = 0;

//    char *screen_text = NULL;
//    if (VTE_IS_TERMINAL(app->terminal_view)) {
//        VteTerminal *vte = VTE_TERMINAL(app->terminal_view);
//        long row, col;
//        vte_terminal_get_cursor_position(vte, &col, &row);
//        long context_depth = 1000;
//        long start_row = (row > context_depth) ? (row - context_depth) : 0;
//        screen_text = vte_terminal_get_text_range(vte, start_row, 0, row, col, NULL, NULL, NULL);
//    }

//    struct json_object *root = json_object_new_object();
//    struct json_object *contents = json_object_new_array();

    // Inject history from MariaDB
//    load_history_to_gemini(app, contents, prompt);

//    struct json_object *content_obj = json_object_new_object();
//    struct json_object *parts = json_object_new_array();
//    struct json_object *part_text = json_object_new_object();

//    char *full_prompt = g_strdup_printf(
//        "CURRENT TERMINAL OUTPUT:\n%s\n\nUSER INSTRUCTION: %s",
//        screen_text ? screen_text : "None", prompt);

//    json_object_object_add(part_text, "text", json_object_new_string(full_prompt));
//    json_object_array_add(parts, part_text);
//    json_object_object_add(content_obj, "parts", parts);
//    json_object_array_add(contents, content_obj);
//    json_object_object_add(root, "contents", contents);
//
//    const char *post_data = json_object_to_json_string(root);
//
//    snprintf(url, sizeof(url),
