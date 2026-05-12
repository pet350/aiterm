#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include <json-c/json.h>
#include <mariadb/mysql.h>
#include <vte/vte.h>
#include "gui.h"
#include "utils.h"
#include "update.h"
#include "openai.h"

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

    struct json_object *root = json_object_new_object();
    struct json_object *contents = json_object_new_array();
    
    // Inject history from MariaDB
    load_history_to_gemini(app, contents, prompt);

    struct json_object *content_obj = json_object_new_object();
    struct json_object *parts = json_object_new_array();
    struct json_object *part_text = json_object_new_object();

    char *full_prompt = g_strdup_printf(
        "CURRENT TERMINAL OUTPUT:\n%s\n\nUSER INSTRUCTION: %s",
        screen_text ? screen_text : "None", prompt);

    json_object_object_add(part_text, "text", json_object_new_string(full_prompt));
    json_object_array_add(parts, part_text);
    json_object_object_add(content_obj, "parts", parts);
    json_object_array_add(contents, content_obj);
    json_object_object_add(root, "contents", contents);

    const char *post_data = json_object_to_json_string(root);

    snprintf(url, sizeof(url),
        "https://generativelanguage.googleapis.com/v1beta/models/%s:generateContent?key=%s",
        app->model ? app->model : "gemini-1.5-flash", app->api_key);

    curl_handle = curl_easy_init();
    if (curl_handle) {
        struct curl_slist *headers = curl_slist_append(NULL, "Content-Type: application/json");
        curl_easy_setopt(curl_handle, CURLOPT_URL, url);
        curl_easy_setopt(curl_handle, CURLOPT_POSTFIELDS, post_data);
        curl_easy_setopt(curl_handle, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
        curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)&chunk);

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
    return perform_gemini_request(app, prompt);
}

