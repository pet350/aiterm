// part of aiterm project
// openai.c
// Functions for sending/receiving data from OpenAI
// By: Peter Talbott
// Assisted by: Gemini
// May 2026

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include <json-c/json.h>
#include <mariadb/mysql.h>
#include "openai.h"
#include "gemini.h"  // <--- Ensure this exists and is included
#include "utils.h"

char* send_to_openai(AppContext *app, const char *prompt) {
    if (!app || !prompt) return NULL;

    ProviderConfig *provider = &app->provider_config;
    CURL *curl_handle;
    CURLcode res;
    struct MemoryStruct chunk = { malloc(1), 0 };

    const char *base_url = provider->base_url ? provider->base_url : "https://api.openai.com/v1";
    const char *endpoint = provider->endpoint ? provider->endpoint : "chat/completions";
    char *url = g_strdup_printf("%s/%s", base_url, endpoint);

    struct json_object *root = json_object_new_object();
    json_object_object_add(root, "model", json_object_new_string(provider->model ? provider->model : OPENAI_MODEL));

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
        headers = curl_slist_append(headers, "Content-Type: application/json");

        if (provider->api_key && provider->auth_header) {
            char auth_header[1024];
            if (provider->auth_scheme && strlen(provider->auth_scheme) > 0) {
                snprintf(auth_header, sizeof(auth_header), "%s: %s %s",
                         provider->auth_header, provider->auth_scheme, provider->api_key);
            } else {
                snprintf(auth_header, sizeof(auth_header), "%s: %s",
                         provider->auth_header, provider->api_key);
            }
            headers = curl_slist_append(headers, auth_header);
        }

        curl_easy_setopt(curl_handle, CURLOPT_URL, url);
        curl_easy_setopt(curl_handle, CURLOPT_POSTFIELDS, payload);
        curl_easy_setopt(curl_handle, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
        curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)&chunk);
        curl_easy_setopt(curl_handle, CURLOPT_CONNECTTIMEOUT, 10L);
        curl_easy_setopt(curl_handle, CURLOPT_TIMEOUT, 30L);

        res = curl_easy_perform(curl_handle);
        if (res != CURLE_OK) {
            DEBUG_PRINT("[DEBUG]: CURL OpenAI Error: %s\n", curl_easy_strerror(res));
        }

        curl_easy_cleanup(curl_handle);
        curl_slist_free_all(headers);
    }

    g_free(url);
    json_object_put(root);
    return chunk.memory;
}
