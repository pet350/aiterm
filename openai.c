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
#include "ai_provider.h"

extern const char *GENERAL_DIRECTIVES;
#include "ai_retry.h"

typedef struct {
    AppContext *app;
    const char *url;
} OpenAIHttpData;

/*
 * Execute exactly one OpenAI-compatible HTTP request.
 * The retry subsystem owns the retry loop.  This callback only performs
 * one attempt and returns the provider's raw response body.
 */
static char *openai_http_single_attempt(const char *payload,
                                        long *out_http_code,
                                        void *user_data) {
    OpenAIHttpData *data = (OpenAIHttpData *)user_data;
    if (!data || !data->app || !data->url || !payload)
        return NULL;

    struct MemoryStruct chunk = { malloc(1), 0 };
    CURL *curl_handle = curl_easy_init();

    if (!curl_handle) {
        if (out_http_code)
            *out_http_code = 0;
        return chunk.memory;
    }

    struct curl_slist *headers = NULL;
    const ProviderConfig *provider = ai_provider_current_config(data->app);

    headers = curl_slist_append(headers, "Content-Type: application/json");

    if (provider->api_key && provider->auth_header) {
        char auth_header[1024];

        if (provider->auth_scheme && strlen(provider->auth_scheme) > 0) {
            snprintf(auth_header, sizeof(auth_header), "%s: %s %s",
                     provider->auth_header,
                     provider->auth_scheme,
                     provider->api_key);
        } else {
            snprintf(auth_header, sizeof(auth_header), "%s: %s",
                     provider->auth_header,
                     provider->api_key);
        }

        headers = curl_slist_append(headers, auth_header);
    }

    long http_code = 0;

    curl_easy_setopt(curl_handle, CURLOPT_URL, data->url);
    curl_easy_setopt(curl_handle, CURLOPT_POSTFIELDS, payload);
    curl_easy_setopt(curl_handle, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)&chunk);
    curl_easy_setopt(curl_handle, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl_handle, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl_handle, CURLOPT_TCP_KEEPALIVE, 1L);

    CURLcode res = curl_easy_perform(curl_handle);

    if (res == CURLE_OK) {
        curl_easy_getinfo(curl_handle, CURLINFO_RESPONSE_CODE, &http_code);
    } else {
        DEBUG_PRINT("[ DEBUG ]: CURL OpenAI-compatible Error: %s\n",
                    curl_easy_strerror(res));
        http_code = 0;
    }

    if (out_http_code)
        *out_http_code = http_code;

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl_handle);

    return chunk.memory;
}

static char* send_to_openai_ex_types_internal(AppContext *app, const char *prompt, const char *terminal_context, gboolean include_history, TagType prompt_type, TagType context_type, gboolean include_directives) {
    if (!app || !prompt) return NULL;

    char *lt_pl   = g_strdup(app->ansi.lt_purple);
    char *cy      = g_strdup(app->ansi.cyan);
    char *yl      = g_strdup(app->ansi.yellow);
    char *gr      = g_strdup(app->ansi.green);
    char *red     = g_strdup(app->ansi.red);
    char *nml     = g_strdup(app->ansi.normal);

    const ProviderConfig *provider = ai_provider_current_config(app);

    const char *base_url = provider->base_url ? provider->base_url : "https://api.openai.com/v1";
    const char *endpoint = provider->endpoint ? provider->endpoint : "chat/completions";
    char *url = g_strdup_printf("%s/%s", base_url, endpoint);

    struct json_object *root = json_object_new_object();
    json_object_object_add(root, "model", json_object_new_string(provider->model ? provider->model : OPENAI_MODEL));

    struct json_object *messages_array = json_object_new_array();

    // System message. GENERAL_DIRECTIVES are included only for the explicit
    // initialization/provider-change directive request. Normal requests keep
    // the lightweight provider system message and do not resend directives.
    struct json_object *sys_msg = json_object_new_object();
    json_object_object_add(sys_msg, "role", json_object_new_string("system"));
    if (include_directives && GENERAL_DIRECTIVES && GENERAL_DIRECTIVES[0] != '\0') {
        json_object_object_add(sys_msg, "content", json_object_new_string(GENERAL_DIRECTIVES));
    } else {
        json_object_object_add(sys_msg, "content", json_object_new_string("You are a Linux Expert."));
    }
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
                red, ai_provider_current_config(app)->provider ? ai_provider_current_config(app)->provider : "openai", gr,
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
        DEBUG_PRINT("[ %sDEBUG%s ]: [%sOPENAI%s] %sterminal_context=[%s0%s] bytes or no remaining request budget%s\n",
		lt_pl, nml, cy, nml, gr, red, gr, nml);
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
    DEBUG_PRINT("[ %sDEBUG%s ]: [%sAUTOCHUNK%s] %sFinal OpenAI-compatible JSON payload=[%s%zu%s] bytes (budget=%s%zu%s)%s\n",
		lt_pl, nml, cy, nml, gr,
                red, payload ? strlen(payload) : 0UL, gr,
		red, request_budget, gr, nml);

    /*
     * OpenAI-compatible providers such as Groq share the same retry engine
     * as Gemini.  This is important for provider-side 429/TPM responses:
     * return the raw final response, but retry transient rate-limit failures
     * before exposing them to the caller.
     */
    OpenAIHttpData http_data = {
        .app = app,
        .url = url
    };

    long final_http_code = 0;
    char *raw_response = ai_retry_execute_with_retry(
        app,
        openai_http_single_attempt,
        payload,
        &http_data,
        &final_http_code
    );

    if (raw_response) {
        DEBUG_PRINT("[ %sDEBUG%s ]: [%sOPENAI%s] %sFinal HTTP status=[%s%ld%s]%s\n",
                    lt_pl, nml, cy, nml, gr, red, final_http_code, gr, nml);
    }

    g_free(url);

    g_free(lt_pl);
    g_free(cy);
    g_free(yl);
    g_free(gr);
    g_free(red);
    g_free(nml);

    json_object_put(root);
    return raw_response;
}


char* send_to_openai_ex_types(AppContext *app, const char *prompt, const char *terminal_context, gboolean include_history, TagType prompt_type, TagType context_type) {
    return send_to_openai_ex_types_internal(
        app, prompt, terminal_context, include_history, prompt_type, context_type, FALSE);
}

char* send_to_openai_send_directives(AppContext *app) {
    if (!app || !GENERAL_DIRECTIVES || GENERAL_DIRECTIVES[0] == '\0')
        return NULL;

    return send_to_openai_ex_types_internal(
        app, "Acknowledge these AITerm directives in one short sentence.",
        NULL, FALSE, TAG_SYSTEM, TAG_NONE, TRUE);
}

char* send_to_openai_ex(AppContext *app, const char *prompt, const char *terminal_context, gboolean include_history) {
    return send_to_openai_ex_types(app, prompt, terminal_context, include_history, TAG_USER, TAG_LOG_DUMP);
}

char* send_to_openai(AppContext *app, const char *prompt, const char *terminal_context) {
    char *lt_pl   = g_strdup(app->ansi.lt_purple);
    char *cy      = g_strdup(app->ansi.cyan);
    char *yl      = g_strdup(app->ansi.yellow);
    char *gr      = g_strdup(app->ansi.green);
    char *red     = g_strdup(app->ansi.red);
    char *nml     = g_strdup(app->ansi.normal);

    DEBUG_PRINT("[ %sDEBUG%s ]: [%sSend to OpenAI%s] %sPrompt=[%s%ld%s] Bytes, Terminal=[%s%ld%s] Bytes.%s\n",
	lt_pl, nml, cy, nml, gr,
	red, strlen(prompt), gr,
	red, strlen(terminal_context), gr, nml);

    g_free(lt_pl);
    g_free(cy);
    g_free(yl);
    g_free(gr);
    g_free(red);
    g_free(nml);

    return send_to_openai_ex_types(app, prompt, terminal_context, TRUE, TAG_USER, TAG_LOG_DUMP);
}
