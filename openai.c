#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include <json-c/json.h>
#include <mariadb/mysql.h>
#include "openai.h"
#include "gemini.h"  // <--- Ensure this exists and is included
#include "utils.h"

char* send_to_openai(const char *api_key, const char *prompt) {
    CURL *curl_handle;
    CURLcode res; // Added for 0.7.5-delta error tracking
    struct MemoryStruct chunk = { malloc(1), 0 }; // Now compiler knows what this is
    const char *url = "https://api.openai.com/v1/chat/completions";

    struct json_object *root = json_object_new_object();
    json_object_object_add(root, "model", json_object_new_string("gpt-4o-mini"));

    struct json_object *messages_array = json_object_new_array();

    // System message
    struct json_object *sys_msg = json_object_new_object();
    json_object_object_add(sys_msg, "role", json_object_new_string("system"));
    json_object_object_add(sys_msg, "content", json_object_new_string("You are a Linux Expert."));
    json_object_array_add(messages_array, sys_msg);

    // History
    load_history_to_api(messages_array); 

    // Current user prompt
    struct json_object *user_msg = json_object_new_object();
    json_object_object_add(user_msg, "role", json_object_new_string("user"));
    json_object_object_add(user_msg, "content", json_object_new_string(prompt));
    json_object_array_add(messages_array, user_msg);

    json_object_object_add(root, "messages", messages_array);
    const char *payload = json_object_to_json_string(root);

    curl_handle = curl_easy_init();
    if(curl_handle) {
        struct curl_slist *headers = NULL;
        char auth_header[512];
        snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", api_key);
        headers = curl_slist_append(headers, "Content-Type: application/json");
        headers = curl_slist_append(headers, auth_header);

        curl_easy_setopt(curl_handle, CURLOPT_URL, url);
        curl_easy_setopt(curl_handle, CURLOPT_POSTFIELDS, payload);
        curl_easy_setopt(curl_handle, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
        curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)&chunk);

        // --- 0.7.5-DELTA FAIL-SAFE BOUNDARIES ---
        curl_easy_setopt(curl_handle, CURLOPT_CONNECTTIMEOUT, 10L); // Max 10 seconds to connect
        curl_easy_setopt(curl_handle, CURLOPT_TIMEOUT, 30L);        // Max 30 seconds for entire transfer

        res = curl_easy_perform(curl_handle);
        if (res != CURLE_OK) {
            DEBUG_PRINT("CURL OpenAI Error: %s\n", curl_easy_strerror(res));
        }

        curl_easy_cleanup(curl_handle);
        curl_slist_free_all(headers);
    }

    json_object_put(root);
    return chunk.memory; // Fixes "control reaches end of non-void function"
}
