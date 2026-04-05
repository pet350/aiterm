#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include <json-c/json.h>
#include "openai.h" // We can reuse the MemoryStruct definition
#include "update.h"
#include "utils.h"

char* send_to_gemini(const char *api_key, const char *prompt) {
    CURL *curl_handle;
    CURLcode res; // <--- ADD THIS LINE
    struct MemoryStruct chunk = { malloc(1), 0 };

    // Gemini URL includes the API Key in the query string
    char url[512];
    snprintf(url, sizeof(url), 
        "https://generativelanguage.googleapis.com/v1beta/models/gemini-1.5-flash:generateContent?key=%s", 
        api_key);

    struct json_object *root = json_object_new_object();
    struct json_object *contents = json_object_new_array();
    struct json_object *content_obj = json_object_new_object();
    struct json_object *parts = json_object_new_array();
    struct json_object *part_obj = json_object_new_object();

    json_object_object_add(part_obj, "text", json_object_new_string(prompt));
    json_object_array_add(parts, part_obj);
    json_object_object_add(content_obj, "parts", parts);
    json_object_array_add(contents, content_obj);
    json_object_object_add(root, "contents", contents);

    const char *payload = json_object_to_json_string(root);

    curl_handle = curl_easy_init();
    if(curl_handle) {
        struct curl_slist *headers = NULL;
        headers = curl_slist_append(headers, "Content-Type: application/json");

        curl_easy_setopt(curl_handle, CURLOPT_URL, url);
        curl_easy_setopt(curl_handle, CURLOPT_POSTFIELDS, payload);
        curl_easy_setopt(curl_handle, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
        curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)&chunk);

        curl_easy_perform(curl_handle);

	// ... inside if(curl_handle) ...
        res = curl_easy_perform(curl_handle);
        if(res != CURLE_OK) {
            fprintf(stderr, "Gemini curl failed: %s\n", curl_easy_strerror(res));
        }
        curl_easy_cleanup(curl_handle);
        curl_slist_free_all(headers);
    }

    json_object_put(root);
    return chunk.memory; // THIS was likely missing, causing the "control reaches end" warning
}