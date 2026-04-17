#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include <json-c/json.h>
#include "openai.h" // We can reuse the MemoryStruct definition
#include "update.h"
#include "utils.h"


char* send_to_gemini(const char *api_key, const char *model, const char *prompt) {
    CURL *curl_handle;
    CURLcode res;

    struct MemoryStruct chunk;
    chunk.memory = malloc(1); 
    chunk.size = 0;
    char url[512];

    // ... inside send_to_gemini ...
    struct json_object *root = json_object_new_object();
    struct json_object *contents = json_object_new_array();

    // 1. Load historical context from MariaDB on naboo
    load_history_to_gemini(contents);


	// Use the model passed from the config, fallback to a safe default if NULL
    const char *active_model = (model && strlen(model) > 0) ? model : "gemini-2.5-flash";

    snprintf(url, sizeof(url), 
        "https://generativelanguage.googleapis.com/v1beta/models/%s:generateContent?key=%s", 
        active_model, api_key);

    // Build the correct Gemini JSON payload
//    struct json_object *root = json_object_new_object();
//    struct json_object *contents = json_object_new_array();
    struct json_object *content_obj = json_object_new_object();
    struct json_object *parts = json_object_new_array();
    struct json_object *part_obj = json_object_new_object();

    json_object_object_add(part_obj, "text", json_object_new_string(prompt));
    json_object_array_add(parts, part_obj);

    // Gemini 1.5 often expects a 'user' role even for single prompts
    json_object_object_add(content_obj, "role", json_object_new_string("user"));
    json_object_object_add(content_obj, "parts", parts);
    json_object_array_add(contents, content_obj);
    json_object_object_add(root, "contents", contents);

    const char *payload = json_object_to_json_string(root);

    curl_handle = curl_easy_init();
    if(curl_handle) {
        struct curl_slist *headers = NULL;
        headers = curl_slist_append(headers, "Content-Type: application/json");

	curl_easy_setopt(curl_handle, CURLOPT_ACCEPT_ENCODING, ""); // Tells CURL to handle decompression automatically
        curl_easy_setopt(curl_handle, CURLOPT_URL, url);
        curl_easy_setopt(curl_handle, CURLOPT_POSTFIELDS, payload);
        curl_easy_setopt(curl_handle, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
        curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)&chunk);

        // REMOVED the extra curl_easy_perform here!
        res = curl_easy_perform(curl_handle);

        if(res != CURLE_OK) {
            fprintf(stderr, "Gemini curl failed: %s\n", curl_easy_strerror(res));
        } else {
            // Add a debug print to see the RAW response in the terminal
            DEBUG_PRINT("DEBUG: Gemini Raw Response: %s\n", chunk.memory);
        }

        curl_easy_cleanup(curl_handle);
        curl_slist_free_all(headers);
    }

    json_object_put(root);
    return chunk.memory;
}
