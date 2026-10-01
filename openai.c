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
#include "auto_chunk.h"
#include "xml_tagging.h"

char* send_to_openai_ex_types(AppContext *app, const char *prompt, const char *terminal_context, gboolean include_history, TagType prompt_type, TagType context_type) {
    if (!app || !prompt) return NULL;

    char *lt_pl   = g_strdup(app->ansi.lt_purple);
    char *cy      = g_strdup(app->ansi.cyan);
    char *yl      = g_strdup(app->ansi.yellow);
    char *gr      = g_strdup(app->ansi.green);
    char *red     = g_strdup(app->ansi.red);
    char *nml     = g_strdup(app->ansi.normal);

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

    /*
     * Build one bounded request.  The previous implementation chunked only
     * terminal_context and then appended every chunk to the same HTTP request.
     * That still produced an oversized request because history was already in
     * messages_array.  Gamma-4 budgets the complete logical request first.
     */
    const gsize request_budget = auto_chunk_request_budget(app);
    const gsize fixed_budget = strlen(prompt) + 1024;
    gsize available_budget = request_budget > fixed_budget
                           ? request_budget - fixed_budget : 0;
    const gsize context_len = terminal_context ? strlen(terminal_context) : 0;

    /* Prefer current terminal/TEE data, while reserving up to 25%% for history. */
    gsize terminal_budget = context_len;
    const gsize terminal_cap = (available_budget * 3) / 4;
    if (terminal_budget > terminal_cap)
        terminal_budget = terminal_cap;

    gsize history_budget = available_budget > terminal_budget
                         ? available_budget - terminal_budget : 0;
    if (history_budget > 12000U)
        history_budget = 12000U;

    /* If the terminal payload is small, give unused space back to history. */
    if (context_len < terminal_cap) {
        gsize spare = terminal_cap - context_len;
        gsize expanded = history_budget + spare;
        history_budget = expanded > 12000U ? 12000U : expanded;
    }

    DEBUG_PRINT("[ %sDEBUG%s ]: [%sAUTOCHUNK%s] %sprovider=[%s%s%s] request_budget=[%s%zu%s] fixed=[%s%zu%s] history_budget=[%s%zu%s] terminal_budget=[%s%zu%s]%s\n",
		lt_pl, nml, cy, nml, gr,
                red, app->provider_config.provider ? app->provider_config.provider : "openai", gr,
                red, request_budget, gr, 
		red, fixed_budget, gr, 
		red, history_budget, gr, 
		red, terminal_budget, gr, nml);

    if (include_history)
        load_history_to_api(app, messages_array, history_budget);

    if (terminal_context && *terminal_context && terminal_budget > 0) {
        DEBUG_PRINT("[ %sDEBUG%s ]: [%sOPENAI%s] %sterminal_context=[%s%zu%s] bytes%s\n", 
		lt_pl, nml, cy, nml, gr, 
		red, context_len, gr, nml);

        GPtrArray *context_chunks = auto_chunk_payload_with_limit(app, terminal_context, terminal_budget);
        guint chunk_count = context_chunks ? context_chunks->len : 0;

        DEBUG_PRINT("[ %sDEBUG%s ]: [%sAUTOCHUNK%s] %sOpenAI-compatible context requires [%s%u%s] chunk(s) at [%s%zu%s] byte request budget.%s\n",
		lt_pl, nml, cy, nml, gr,
                red, chunk_count, gr, 
		red, terminal_budget, gr, nml);

        /* A single request still receives one bounded chunk.  Oversized captures
         * are handled one level above this function by the provider orchestration
         * layer, which sends every chunk through a separate ingestion pass and
         * then performs one final synthesis request. */
        if (context_chunks && context_chunks->len > 0) {
            const char *chunk_text = g_ptr_array_index(context_chunks, 0);
            char *wrapped_context = xml_wrap_with_type(
                app, chunk_text ? chunk_text : "", context_type);
            struct json_object *context_msg = json_object_new_object();
            json_object_object_add(context_msg, "role", json_object_new_string("user"));

            char *context_prompt = g_strdup_printf(
                "AITERM TERMINAL/TEE CONTEXT (request chunk 1 of %u):\n"
                "----- BEGIN TERMINAL CONTEXT -----\n%s\n----- END TERMINAL CONTEXT -----",
                chunk_count, wrapped_context ? wrapped_context : "");

            json_object_object_add(context_msg, "content",
                                   json_object_new_string(context_prompt));
            json_object_array_add(messages_array, context_msg);
            g_free(context_prompt);
            g_free(wrapped_context);
        }

        if (chunk_count > 1)
            DEBUG_PRINT("[ %sDEBUG%s ]: [%sAUTOCHUNK%s] %sDirect OpenAI-compatible request is bounded to chunk %s1/%u%s; provider orchestration handles remaining chunks.%s\n",
		lt_pl, nml, cy, nml, gr, 
                red, chunk_count, gr, nml);

        if (context_chunks)
            g_ptr_array_unref(context_chunks);
    } else {
        DEBUG_PRINT("[ OPENAI ] terminal_context=0 bytes or no remaining request budget\n");
    }

    // Current user prompt
    char *wrapped_prompt = xml_wrap_with_type(app, prompt, prompt_type);
    struct json_object *user_msg = json_object_new_object();
    json_object_object_add(user_msg, "role", json_object_new_string("user"));
    json_object_object_add(user_msg, "content", json_object_new_string(wrapped_prompt ? wrapped_prompt : prompt));
    g_free(wrapped_prompt);
    json_object_array_add(messages_array, user_msg);

    json_object_object_add(root, "messages", messages_array);
    const char *payload = json_object_to_json_string(root);
    DEBUG_PRINT("[ AUTOCHUNK ] Final OpenAI-compatible JSON payload=%zu bytes (budget=%zu)\n",
                payload ? strlen(payload) : 0UL, request_budget);

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
            DEBUG_PRINT("[ DEBUG ]: CURL OpenAI Error: %s\n", curl_easy_strerror(res));
        }

        curl_easy_cleanup(curl_handle);
        curl_slist_free_all(headers);
    }

    g_free(url);

    g_free(lt_pl);
    g_free(cy);
    g_free(yl);
    g_free(gr);
    g_free(red);
    g_free(nml);

    json_object_put(root);
    return chunk.memory;
}


char* send_to_openai_ex(AppContext *app, const char *prompt, const char *terminal_context, gboolean include_history) {
    return send_to_openai_ex_types(app, prompt, terminal_context, include_history, TAG_USER, TAG_LOG_DUMP);
}

char* send_to_openai(AppContext *app, const char *prompt, const char *terminal_context) {
    return send_to_openai_ex_types(app, prompt, terminal_context, TRUE, TAG_USER, TAG_LOG_DUMP);
}
