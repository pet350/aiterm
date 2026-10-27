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
#include "ai_provider.h"
#include "session_manager.h"
#include "noisefilter.h"
#include "ai_retry.h"
#include "auto_chunk.h"
#include "xml_tagging.h"

typedef struct {
    AppContext *app;
    const char *url;
} GeminiHttpData;


extern const char *GENERAL_DIRECTIVES;

static char *gemini_http_single_attempt(const char *payload, long *out_http_code, void *user_data);

/* Gemini Interactions API support. The directive interaction is a stable
 * parent for normal Gemini requests. AITerm keeps its own MariaDB history,
 * so each request points back to the same directive-only interaction rather
 * than chaining user turns together on Google's server. */
static gboolean gemini_directive_interaction_valid(AppContext *app) {
    const ProviderConfig *provider = ai_provider_current_config(app);
    const char *directive_id = ai_provider_current_directive_id(app);
    const char *directive_model = ai_provider_current_directive_model(app);
    if (!app || !directive_id || !directive_model || !provider || !provider->model)
        return FALSE;

    return strcmp(directive_model, provider->model) == 0;
}

void gemini_directive_invalidate(AppContext *app) {
    if (!app) return;

    char *lt_pl   = g_strdup(app->ansi.lt_purple);
    char *cy      = g_strdup(app->ansi.cyan);
    char *yl      = g_strdup(app->ansi.yellow);
    char *gr      = g_strdup(app->ansi.green);
    char *red     = g_strdup(app->ansi.red);
    char *nml     = g_strdup(app->ansi.normal);

    if (app->gemini_cache.directive_interaction_id) {
        DEBUG_PRINT("[ %sDEBUG%s ]: [%sDIRECTIVES%s] %sInvalidating Gemini interaction [%s%s%s]%s.\n",
		lt_pl, nml, cy, nml, gr,
                red, app->gemini_cache.directive_interaction_id, gr, nml);
    }

    g_clear_pointer(&app->gemini_cache.directive_interaction_id, g_free);
    g_clear_pointer(&app->gemini_cache.directive_model, g_free);
    app->gemini_cache.directive_created_at = 0;
    app->gemini_cache.directive_active = FALSE;

    g_free(cy);
    g_free(yl);
    g_free(gr);
    g_free(red);
    g_free(nml);

}

static char *gemini_interactions_url(AppContext *app) {
    if (!app) return NULL;

    const ProviderConfig *provider = ai_provider_current_config(app);
    const char *base_url = provider->base_url && *provider->base_url
        ? provider->base_url
        : "https://generativelanguage.googleapis.com/v1beta";
    const char *query_key = provider->query_key_name && *provider->query_key_name
        ? provider->query_key_name : "key";
    const char *api_key = provider->api_key ? provider->api_key : "";

    return g_strdup_printf("%s/interactions?%s=%s",
                           base_url, query_key, api_key);
}

static char *gemini_interaction_to_generate_json(const char *raw) {
    if (!raw) return NULL;

    struct json_object *source = json_tokener_parse(raw);
    if (!source) return NULL;

    struct json_object *steps = NULL;
    if (!json_object_object_get_ex(source, "steps", &steps) ||
        json_object_get_type(steps) != json_type_array) {
        json_object_put(source);
        return NULL;
    }

    const char *answer = NULL;
    for (int i = json_object_array_length(steps) - 1; i >= 0; --i) {
        struct json_object *step = json_object_array_get_idx(steps, i);
        struct json_object *type = NULL, *content = NULL;
        if (!step ||
            !json_object_object_get_ex(step, "type", &type) ||
            strcmp(json_object_get_string(type), "model_output") != 0 ||
            !json_object_object_get_ex(step, "content", &content) ||
            json_object_get_type(content) != json_type_array)
            continue;

        for (int j = 0; j < json_object_array_length(content); ++j) {
            struct json_object *part = json_object_array_get_idx(content, j);
            struct json_object *text = NULL;
            if (part && json_object_object_get_ex(part, "text", &text)) {
                answer = json_object_get_string(text);
                if (answer) break;
            }
        }
        if (answer) break;
    }

    if (!answer) {
        json_object_put(source);
        return NULL;
    }

    struct json_object *out = json_object_new_object();
    struct json_object *candidates = json_object_new_array();
    struct json_object *candidate = json_object_new_object();
    struct json_object *content = json_object_new_object();
    struct json_object *parts = json_object_new_array();
    struct json_object *part = json_object_new_object();

    json_object_object_add(part, "text", json_object_new_string(answer));
    json_object_array_add(parts, part);
    json_object_object_add(content, "parts", parts);
    json_object_object_add(content, "role", json_object_new_string("model"));
    json_object_object_add(candidate, "content", content);
    json_object_object_add(candidate, "finishReason", json_object_new_string("STOP"));
    json_object_object_add(candidate, "index", json_object_new_int(0));
    json_object_array_add(candidates, candidate);
    json_object_object_add(out, "candidates", candidates);

    struct json_object *usage = NULL;
    if (json_object_object_get_ex(source, "usage", &usage)) {
        struct json_object *meta = json_object_new_object();
        struct json_object *in = NULL, *outtok = NULL, *total = NULL, *thoughts = NULL;
        if (json_object_object_get_ex(usage, "total_input_tokens", &in))
            json_object_object_add(meta, "promptTokenCount", json_object_new_int64(json_object_get_int64(in)));
        if (json_object_object_get_ex(usage, "total_output_tokens", &outtok))
            json_object_object_add(meta, "candidatesTokenCount", json_object_new_int64(json_object_get_int64(outtok)));
        if (json_object_object_get_ex(usage, "total_tokens", &total))
            json_object_object_add(meta, "totalTokenCount", json_object_new_int64(json_object_get_int64(total)));
        if (json_object_object_get_ex(usage, "total_thought_tokens", &thoughts))
            json_object_object_add(meta, "thoughtsTokenCount", json_object_new_int64(json_object_get_int64(thoughts)));
        json_object_object_add(out, "usageMetadata", meta);
    }

    const char *json = json_object_to_json_string(out);
    char *result = json ? g_strdup(json) : NULL;
    json_object_put(out);
    json_object_put(source);
    return result;
}

static char *gemini_create_directive_interaction(AppContext *app) {
    const ProviderConfig *provider = ai_provider_current_config(app);
    if (!app || !GENERAL_DIRECTIVES || !*GENERAL_DIRECTIVES ||
        !provider || !provider->api_key || !provider->model)
        return NULL;

    char *url = gemini_interactions_url(app);
    if (!url) return NULL;

    char *lt_pl   = g_strdup(app->ansi.lt_purple);
    char *cy      = g_strdup(app->ansi.cyan);
    char *yl      = g_strdup(app->ansi.yellow);
    char *gr      = g_strdup(app->ansi.green);
    char *red     = g_strdup(app->ansi.red);
    char *nml     = g_strdup(app->ansi.normal);

    struct json_object *root = json_object_new_object();
    char *wrapped_directives = xml_wrap_with_type(app, GENERAL_DIRECTIVES, TAG_SYSTEM);
    struct json_object *input = json_object_new_string(
        wrapped_directives ? wrapped_directives : GENERAL_DIRECTIVES);
    g_free(wrapped_directives);

    json_object_object_add(root, "model", json_object_new_string(provider->model));
    json_object_object_add(root, "input", input);
    json_object_object_add(root, "system_instruction",
                           json_object_new_string("Treat the AITerm directives in the input as persistent operating instructions for this AITerm context."));

    const char *post_data = json_object_to_json_string(root);
    GeminiHttpData http_data = { .app = app, .url = url };
    long http_code = 0;
    char *raw = ai_retry_execute_with_retry(app, gemini_http_single_attempt,
                                             post_data, &http_data, &http_code);

    if (raw && http_code == 200) {
        struct json_object *response = json_tokener_parse(raw);
        struct json_object *id = NULL;
        if (response && json_object_object_get_ex(response, "id", &id)) {
            char *new_id = g_strdup(json_object_get_string(id));
            if (ai_provider_snapshot_is_current(app)) {
                gemini_directive_invalidate(app);
                app->gemini_cache.directive_interaction_id = g_strdup(new_id);
                app->gemini_cache.directive_model = g_strdup(provider->model);
                app->gemini_cache.directive_created_at = time(NULL);
                app->gemini_cache.directive_active = TRUE;
            }

            DEBUG_PRINT("[ %sDEBUG%s ]: [%sDIRECTIVES%s] %sGemini interaction parent created [%s%s%s] for model [%s%s%s].%s\n",
		lt_pl, nml, cy, nml, gr,
                red, new_id, gr,
                red, provider->model, gr, nml);
            g_free(new_id);
        }
        if (response) json_object_put(response);
    } else if (raw) {
        DEBUG_PRINT("[ %sDEBUG%s ]: [%sDIRECTIVES%s] %sGemini interaction creation failed HTTP [%s%ld%s]: %s%s%s\n",
		lt_pl, nml, cy, nml, gr,
                red, http_code, gr,
		red, raw, nml);
    }

    json_object_put(root);
    g_free(url);

    g_free(cy);
    g_free(yl);
    g_free(gr);
    g_free(red);
    g_free(nml);

    return raw;
}

static struct json_object *gemini_contents_to_interaction_input(struct json_object *contents) {
    if (!contents || json_object_get_type(contents) != json_type_array) return NULL;

    struct json_object *input = json_object_new_array();
    for (int i = 0; i < json_object_array_length(contents); ++i) {
        struct json_object *turn = json_object_array_get_idx(contents, i);
        struct json_object *role = NULL, *parts = NULL;
        if (!turn || !json_object_object_get_ex(turn, "role", &role) ||
            !json_object_object_get_ex(turn, "parts", &parts))
            continue;

        const char *role_name = json_object_get_string(role);
        const char *step_type = strcmp(role_name, "model") == 0
            ? "model_output" : "user_input";

        struct json_object *step = json_object_new_object();
        struct json_object *content = json_object_new_array();
        for (int j = 0; j < json_object_array_length(parts); ++j) {
            struct json_object *part = json_object_array_get_idx(parts, j);
            struct json_object *text = NULL;
            if (!part || !json_object_object_get_ex(part, "text", &text)) continue;

            struct json_object *item = json_object_new_object();
            json_object_object_add(item, "type", json_object_new_string("text"));
            json_object_object_add(item, "text", json_object_new_string(json_object_get_string(text)));
            json_object_array_add(content, item);
        }

        json_object_object_add(step, "type", json_object_new_string(step_type));
        json_object_object_add(step, "content", content);
        json_object_array_add(input, step);
    }
    return input;
}

// Single-attempt HTTP dispatch function compatible with ai_retry_execute_with_retry
static char *gemini_http_single_attempt(const char *payload, long *out_http_code, void *user_data) {
    GeminiHttpData *data = (GeminiHttpData *)user_data;
    if (!data || !data->app || !data->url) return NULL;

    char *lt_pl   = g_strdup(global_app->ansi.lt_purple);
    char *cy      = g_strdup(global_app->ansi.cyan);
    char *yl      = g_strdup(global_app->ansi.yellow);
    char *gr      = g_strdup(global_app->ansi.green);
    char *red     = g_strdup(global_app->ansi.red);
    char *nml     = g_strdup(global_app->ansi.normal);

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
            DEBUG_PRINT("[ %sDEBUG%s ]: [%sCURL%s] %sError: %s%s%s\n", 
		lt_pl, nml, cy, nml, red, yl, curl_easy_strerror(res), nml);
            http_code = 0;
        }

        curl_slist_free_all(headers);
        curl_easy_cleanup(curl_handle);
    }

    if (out_http_code) {
        *out_http_code = http_code;
    }

    g_free(cy);
    g_free(yl);
    g_free(gr);
    g_free(red);
    g_free(nml);

    return chunk.memory;
}

/* Did this failed cachedContent request fail because of the cache itself?
 * 403/404 are not ordinary payload errors, so they always count; a 400 only
 * counts when the error body mentions the cache.  Heuristic: it avoids
 * deleting a healthy cache over an unrelated bad request. */
static gboolean gemini_error_is_cache_miss(long http_code, const char *body) {
    if (http_code == 403 || http_code == 404)
        return TRUE;
    if (http_code != 400 || !body)
        return FALSE;
    char *lower = g_ascii_strdown(body, -1);
    gboolean mentions = strstr(lower, "cache") != NULL;
    g_free(lower);
    return mentions;
}

// --- 1. The Core API Logic ---
// UI-neutral execution wrapped with AI Retry handler
static char* perform_gemini_request_ex_types_internal(AppContext *app, const char *prompt, const char *terminal_context, gboolean include_history, TagType prompt_type, TagType context_type, gboolean include_directives) {
    char url[1024];

    char *lt_pl   = g_strdup(global_app->ansi.lt_purple);
    char *cy      = g_strdup(global_app->ansi.cyan);
    char *yl      = g_strdup(global_app->ansi.yellow);
    char *gr      = g_strdup(global_app->ansi.green);
    char *red     = g_strdup(global_app->ansi.red);
    char *nml     = g_strdup(global_app->ansi.normal);

    /* GTK/VTE objects belong to the main thread.  The caller captures the
     * terminal context before launching this worker.  Never query VTE here. */
    const char *screen_text = terminal_context ? terminal_context : "None";

    /* XML wrapping is applied at the final provider-payload boundary, after
     * chunk selection, so every provider path receives the same metadata. */
    char *tee_chunk = xml_wrap_with_type(app, screen_text, context_type);

    const ProviderConfig *provider = ai_provider_current_config(app);
    if (!provider) return NULL;

    struct json_object *root = json_object_new_object();
    struct json_object *contents = json_object_new_array();

    // GENERAL_DIRECTIVES are intentionally NOT injected into normal requests.
    // They are sent only by perform_gemini_send_directives() during AITerm
    // initialization and after a provider change. This prevents the directive
    // block from consuming Gemini input tokens on every request.
    if (include_directives && GENERAL_DIRECTIVES && GENERAL_DIRECTIVES[0] != '\0') {
        struct json_object *sys_instruction = json_object_new_object();
        struct json_object *sys_parts = json_object_new_array();
        struct json_object *sys_part = json_object_new_object();

        json_object_object_add(sys_part, "text", json_object_new_string(GENERAL_DIRECTIVES));
        json_object_array_add(sys_parts, sys_part);
        json_object_object_add(sys_instruction, "parts", sys_parts);
        json_object_object_add(root, "system_instruction", sys_instruction);
    }

    // 2. Load history within the same total request budget used for the
    // terminal/TEE payload.  Gamma-4 no longer gives history an independent
    // 100-row allowance that can silently consume the provider context.
    /* TRUE when a server-side Smart Cache covers this request's history. */
    gboolean cache_in_use = FALSE;

    const gsize request_budget = auto_chunk_request_budget(app);
    const gsize fixed_budget = strlen(prompt) + 1024;
    gsize available_budget = request_budget > fixed_budget
                           ? request_budget - fixed_budget : 0;
    const gsize context_len = tee_chunk ? strlen(tee_chunk) : 0;
    const gsize terminal_cap = (available_budget * 3) / 4;
    gsize terminal_budget = context_len > terminal_cap ? terminal_cap : context_len;
    gsize history_budget = available_budget > terminal_budget
                         ? available_budget - terminal_budget : 0;
    if (history_budget > 12000U)
        history_budget = 12000U;
    if (context_len < terminal_cap) {
        gsize spare = terminal_cap - context_len;
        gsize expanded = history_budget + spare;
        history_budget = expanded > 12000U ? 12000U : expanded;
    }

    DEBUG_PRINT("[ %sDEBUG%s ]: [%sAUTOCHUNK%s] %sGemini provider=[%s%s%s] request_budget=[%s%zu%s] fixed=[%s%zu%s] history_budget=[%s%zu%s] terminal_budget=[%s%zu%s]%s\n",
	lt_pl, nml, cy, nml, gr,
        red, provider->provider ? provider->provider : "gemini", gr,
        red, request_budget, gr,
	red, fixed_budget, gr,
	red, history_budget, gr,
	red, terminal_budget, gr, nml);

    if (include_history && !app->sys.is_initializing) {
        DEBUG_PRINT("[ %sDEBUG%s ]: [%sPerform Gemini Request%s]%s Not Initializing, Loading History%s\n",
		lt_pl, nml, cy, nml, yl, nml );
        /* Smart Cache only applies to plain generateContent requests: cachedContent
         * cannot be combined with a per-request system_instruction. */
        if (!include_directives)
            cache_in_use = gemini_cache_prepare_history(app, contents, history_budget);
        if (!cache_in_use)
            load_history_to_gemini(app, contents, prompt, history_budget);
    } else {
        DEBUG_PRINT("[ %sDEBUG%s ]: [%sInit Phase%s] %sBypassing history retrieval.%s\n",
		lt_pl, nml, cy, nml, yl, nml);
    }

    /* gemini_cache_prepare_history() already put only the not-yet-cached turns
     * into `contents`; the cached prefix is referenced by handle. */
    if (cache_in_use && app->gemini_cache.id)
        json_object_object_add(root, "cachedContent", json_object_new_string(app->gemini_cache.id));

    // 3. Append Final User Turn (Terminal Screen + Prompt)
    // The complete terminal context is chunked before provider-specific JSON
    // construction. Only the newest bounded chunk enters this request; additional
    // chunks remain persisted in MariaDB rather than exceeding the API budget.
    struct json_object *user_msg = json_object_new_object();
    struct json_object *user_parts = json_object_new_array();

    GPtrArray *context_chunks = auto_chunk_payload_with_limit(app, tee_chunk, terminal_budget);
    guint chunk_count = context_chunks ? context_chunks->len : 0;

    DEBUG_PRINT("[ %sDEBUG%s ]: [%sAUTOCHUNK%s] %sGemini context requires [%s%u%s] chunk(s) at [%s%zu%s] byte request budget.%s\n",
	lt_pl, nml, cy, nml, gr,
        red, chunk_count, gr,
	red, terminal_budget, gr, nml);

    // Direct Gemini requests are bounded to one chunk. Oversized captures are
    // orchestrated above this function as separate ingestion passes.
    GString *cache_prompt = g_string_new(NULL);

    if (context_chunks && context_chunks->len > 0) {
        const char *chunk_text = g_ptr_array_index(context_chunks, 0);
        if (chunk_text) {
            g_string_append(cache_prompt, chunk_text);

            struct json_object *chunk_part = json_object_new_object();
            json_object_object_add(chunk_part, "text", json_object_new_string(chunk_text));
            json_object_array_add(user_parts, chunk_part);
        }
    }

    if (chunk_count > 1)
        DEBUG_PRINT("[ %sDEBUG%s ]: [%sAUTOCHUNK%s] %sDirect Gemini request is bounded to chunk %s1/%u%s; provider orchestration handles remaining chunks.%s\n",
		lt_pl, nml, cy, nml, gr,
                red, chunk_count, gr, nml);

    g_string_append_printf(cache_prompt, "\n\nUSER INSTRUCTION: %s", prompt);

    /* Terminal context is part of the effective Gemini request. Include the
     * assembled logical prompt in the local cache key so a context-dependent
     * question cannot reuse a response produced without that terminal snapshot. */
    char *prompt_hash = g_compute_checksum_for_string(
        G_CHECKSUM_SHA256, cache_prompt->str, -1);

    // Check local cache only after the terminal-aware prompt has been assembled.
    char *cached_res = gemini_cache_lookup(prompt_hash);
    if (cached_res != NULL) {
        g_free(prompt_hash);
        g_string_free(cache_prompt, TRUE);
        if (context_chunks)
            g_ptr_array_unref(context_chunks);
        g_free(tee_chunk);
        json_object_put(root);
        return cached_res;
    }

    struct json_object *instruction_part = json_object_new_object();
    char *wrapped_prompt = xml_wrap_with_type(app, prompt, prompt_type);
    char *instruction = g_strdup_printf("USER INSTRUCTION: %s", wrapped_prompt ? wrapped_prompt : prompt);
    json_object_object_add(instruction_part, "text", json_object_new_string(instruction));
    g_free(wrapped_prompt);
    json_object_array_add(user_parts, instruction_part);
    g_free(instruction);

    json_object_object_add(user_msg, "role", json_object_new_string("user"));
    json_object_object_add(user_msg, "parts", user_parts);

    json_object_array_add(contents, user_msg);
    json_object_object_add(root, "contents", contents);

    g_string_free(cache_prompt, TRUE);
    if (context_chunks)
        g_ptr_array_unref(context_chunks);

    /* When the directive parent exists, use Google's stateful Interactions API.
     * The parent contains GENERAL_DIRECTIVES exactly once. AITerm still sends
     * its normal MariaDB history explicitly, but never resends the directives. */
    if (!cache_in_use && gemini_directive_interaction_valid(app)) {
        struct json_object *interaction_root = json_object_new_object();
        struct json_object *interaction_input =
            gemini_contents_to_interaction_input(contents);

        if (interaction_input && json_object_array_length(interaction_input) > 0) {
            json_object_object_add(interaction_root, "model",
                                   json_object_new_string(provider->model));
            json_object_object_add(interaction_root, "previous_interaction_id",
                                   json_object_new_string(ai_provider_current_directive_id(app)));
            json_object_object_add(interaction_root, "input", interaction_input);
            json_object_object_add(interaction_root, "store", json_object_new_boolean(FALSE));

            const char *interaction_data = json_object_to_json_string(interaction_root);
            char *interaction_url = gemini_interactions_url(app);
            GeminiHttpData interaction_http = { .app = app, .url = interaction_url };
            long interaction_code = 0;
            char *interaction_raw = ai_retry_execute_with_retry(
                app, gemini_http_single_attempt, interaction_data,
                &interaction_http, &interaction_code);

            if (interaction_raw && interaction_code == 200) {
                char *normalized = gemini_interaction_to_generate_json(interaction_raw);
                if (normalized) {
                    gemini_cache_store(prompt_hash, normalized);
                    DEBUG_PRINT("[ %sDEBUG%s ]: [%sDIRECTIVES%s] %sGemini request used persistent directive interaction [%s%s%s].%s\n",
			lt_pl, nml, cy,nml, gr, 
                        red, ai_provider_current_directive_id(app), gr, nml);
                    free(interaction_raw);
                    g_free(interaction_url);
                    json_object_put(interaction_root);
                    g_free(prompt_hash);
                    g_free(tee_chunk);
                    g_free(cy);
                    g_free(yl);
                    g_free(gr);
                    g_free(red);
                    g_free(nml);
                    json_object_put(root);
                    return normalized;
                }
            }

            if (interaction_raw) {
                DEBUG_PRINT("[ %sDEBUG%s ]: [DIRECTIVES] Gemini interaction request failed HTTP [%ld]; falling back to generateContent for this request.\n",
                    lt_pl, nml, interaction_code);
                free(interaction_raw);
            }
            g_free(interaction_url);
        } else if (interaction_input) {
            json_object_put(interaction_input);
        }
        json_object_put(interaction_root);
    }

    const char *post_data = json_object_to_json_string(root);
    DEBUG_PRINT("[ %sDEBUG%s ]: [%sAUTOCHUNK%s] %sFinal Gemini JSON payload=[%s%zu%s] bytes (budget=%s%zu%s)%s\n",
	lt_pl, nml, cy, nml, gr,
        red, post_data ? strlen(post_data) : 0UL, gr,
	red, request_budget, gr, nml);

    const char *base_url = provider->base_url ? provider->base_url : "https://generativelanguage.googleapis.com/v1beta";
    const char *endpoint = provider->endpoint ? provider->endpoint : "models/%s:generateContent";
    const char *model = provider->model ? provider->model : "gemini-3.1-flash-lite";
    const char *query_key = provider->query_key_name ? provider->query_key_name : "key";
    const char *api_key = provider->api_key;

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

    /* A cache deleted or expired server-side fails the request.  Drop the local
     * handle and retry ONCE without the cache: full budgeted history plus the
     * directives (which lived inside the cache) sent inline. */
    if (cache_in_use && final_http_code != 200 &&
        gemini_error_is_cache_miss(final_http_code, raw_json)) {
        DEBUG_PRINT("[ %sDEBUG%s ]: [%sCache%s] %sCached request failed (HTTP %s%ld%s); retrying once without the cache.%s\n",
            lt_pl, nml, cy, nml, yl, red, final_http_code, yl, nml);
        gemini_cache_invalidate(app);

        struct json_object *full = json_object_new_array();
        load_history_to_gemini(app, full, prompt, history_budget);

        /* The final user turn (terminal context + instruction) is the last
         * element of the current contents; carry it over. */
        int cn = json_object_array_length(contents);
        if (cn > 0) {
            struct json_object *final_turn = json_object_array_get_idx(contents, cn - 1);
            json_object_get(final_turn);
            json_object_array_add(full, final_turn);
        }

        json_object_object_del(root, "cachedContent");
        json_object_object_add(root, "contents", full);   /* releases the old array */
        contents = full;

        if (GENERAL_DIRECTIVES && GENERAL_DIRECTIVES[0] != '\0') {
            struct json_object *sys_instruction = json_object_new_object();
            struct json_object *sys_parts = json_object_new_array();
            struct json_object *sys_part = json_object_new_object();
            json_object_object_add(sys_part, "text", json_object_new_string(GENERAL_DIRECTIVES));
            json_object_array_add(sys_parts, sys_part);
            json_object_object_add(sys_instruction, "parts", sys_parts);
            json_object_object_add(root, "system_instruction", sys_instruction);
        }

        post_data = json_object_to_json_string(root);
        if (raw_json) {
            free(raw_json);
            raw_json = NULL;
        }
        if (app->sys.ratelimit_enabled)
            ratelimit_wait_if_needed(&app->limiter);
        final_http_code = 0;
        raw_json = ai_retry_execute_with_retry(app, gemini_http_single_attempt, post_data, &http_data, &final_http_code);
        cache_in_use = FALSE;
    }

    if (raw_json != NULL) {
        DEBUG_PRINT("[ %sDEBUG%s ]:%s --- %sRAW GEMINI RESPONSE%s --- %s\n",
		lt_pl, nml, yl, cy, yl, nml); 
        DEBUG_PRINT("%s(HTTP %s%ld%s)%s ---\n%s%s%s\n--------------------------%s\n", 
		gr, red, final_http_code, gr, yl, red, raw_json, yl, nml);
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
    g_free(tee_chunk);

    g_free(cy);
    g_free(yl);
    g_free(gr);
    g_free(red);
    g_free(nml);

    /* terminal_context is borrowed here; AIThreadData owns and frees it. */
    json_object_put(root);

    return raw_json;
}

// --- 2. Background Thread Worker ---
gpointer ai_thread_func(gpointer data) {
    AIThreadData *td = (AIThreadData *)data;
    if (!td) return NULL;

    // Set the thread name
    SET_THREAD_NAME("aiterm-ai");

    char *raw_json = NULL;

    /* Provider-neutral dispatch.  The worker does not need to know whether
     * the selected backend is Gemini, OpenAI, Groq, OpenRouter, Mistral,
     * Ollama, or another OpenAI-compatible endpoint. */
    raw_json = ai_provider_send_with_context(td->app, td->prompt, td->terminal_context);

    char *final_text = NULL;
    char *requested_tab_id = NULL;
    char *requested_inspect_tab_id = NULL;
    if (raw_json) {
        ai_provider_extract_usage(td->app, raw_json);
        final_text = ai_provider_extract_text(raw_json);
        if (final_text) {
            /* Only honor a final-line action marker with a valid stable UUID.
             * The worker parses text only; all terminal/GTK access is deferred
             * to update_gui_with_response() on the GTK main loop. */
            const char *markers[] = {
                "[[AITERM_ACTION: change_tab ",
                "[[AITERM_ACTION: inspect_tab "
            };
            char *actions[] = { NULL, NULL };
            for (guint marker_index = 0; marker_index < G_N_ELEMENTS(markers); marker_index++) {
                char *action = strstr(final_text, markers[marker_index]);
                if (!action)
                    continue;
                char *id_start = action + strlen(markers[marker_index]);
                char *marker_end = strstr(id_start, "]] ");
                if (!marker_end) marker_end = strstr(id_start, "]]\n");
                if (!marker_end) marker_end = strstr(id_start, "]]\r");
                if (!marker_end) marker_end = strstr(id_start, "]]" );
                if (!marker_end)
                    continue;

                char *id_text = g_strndup(id_start, (gsize)(marker_end - id_start));
                g_strstrip(id_text);
                char *after_marker = marker_end + 2;
                while (*after_marker && g_ascii_isspace(*after_marker))
                    after_marker++;
                if (g_uuid_string_is_valid(id_text) && *after_marker == '\0') {
                    actions[marker_index] = g_strdup(id_text);
                    memmove(action, after_marker, strlen(after_marker) + 1);
                    g_strchomp(final_text);
                }
                g_free(id_text);
                break;
            }
            requested_tab_id = actions[0];
            requested_inspect_tab_id = actions[1];
            save_to_history(td->original_prompt ? td->original_prompt : td->prompt, final_text);
        }
        free(raw_json);
    }

    AIResponseData *rd = g_malloc0(sizeof(AIResponseData));
    rd->app = td->app;
    rd->response_text = final_text;
    rd->original_prompt = g_strdup(td->original_prompt ? td->original_prompt : td->prompt);
    rd->requested_tab_id = requested_tab_id;
    rd->requested_inspect_tab_id = requested_inspect_tab_id;
    rd->terminal_context = td->terminal_context;
    td->terminal_context = NULL;
    rd->investigation_step = td->investigation_step;

    g_idle_add((GSourceFunc)update_gui_with_response, rd);

    g_free(td->terminal_context);
    g_free(td->original_prompt);
    g_free(td->prompt);
    g_free(td);
    return NULL;
}

// Legacy compatibility wrapper
char* send_to_gemini(AppContext *app, const char *prompt) {
    if (app->sys.ratelimit_enabled) {
        ratelimit_wait_if_needed(&app->limiter);
    }

    char *lt_pl   = g_strdup(global_app->ansi.lt_purple);
    char *cy      = g_strdup(global_app->ansi.cyan);
    char *yl      = g_strdup(global_app->ansi.yellow);
    char *gr      = g_strdup(global_app->ansi.green);
    char *red     = g_strdup(global_app->ansi.red);
    char *nml     = g_strdup(global_app->ansi.normal);

    if (g_atomic_int_get(&app->sys.ai_busy)) {
        DEBUG_PRINT("[ %sDEBUG%s ]: [%sSEND_TO_GEMINI%s]: %sai_busy flag set not executing perform_gemini_request%s\n",
		lt_pl, nml, cy, nml, gr, nml);
        return NULL;
    }

    char *output = g_strdup(noise_filter_apply(app, prompt));
    g_atomic_int_set(&app->sys.ai_busy, 1);
    DEBUG_PRINT("[ %sDEBUG%s ]: [%sSEND_TO_GEMINI%s] %sset ai_busy flag TRUE%s\n",
	lt_pl, nml, cy, nml, gr, nml);

    char *data = perform_gemini_request_ex(app, output, NULL, TRUE);
    g_atomic_int_set(&app->sys.ai_busy, 0);

    DEBUG_PRINT("[ %sDEBUG%s ]: [%sSEND_TO_GEMINI%s] %sCleared ai_busy flag, returning response%s\n",
	lt_pl, nml, cy, nml, gr, nml);

    g_free(output);

    g_free(cy);
    g_free(yl);
    g_free(gr);
    g_free(red);
    g_free(nml);

    return data;
}

char* gemini_list_models(AppContext *app) {
    CURL *curl;
    CURLcode res;
    struct MemoryStruct chunk;
    struct json_object *root = NULL;
    struct json_object *models_array = NULL;
    GString *model_output_str = g_string_new("");

    char *lt_pl   = g_strdup(global_app->ansi.lt_purple);
    char *cy      = g_strdup(global_app->ansi.cyan);
    char *yl      = g_strdup(global_app->ansi.yellow);
    char *gr      = g_strdup(global_app->ansi.green);
    char *red     = g_strdup(global_app->ansi.red);
    char *nml     = g_strdup(global_app->ansi.normal);

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
        snprintf(api_key_header, sizeof(api_key_header), "X-Goog-Api-Key: %s", get_provider_api_key(app, "gemini") ? get_provider_api_key(app, "gemini") : "");
        headers = curl_slist_append(headers, api_key_header);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

        DEBUG_PRINT("[ %sDEBUG%s ]: [%sGemini Models%s] %sFetching models from: %s%s%s\n",
		lt_pl, nml, cy, nml, gr,
		red, url, nml );

        res = curl_easy_perform(curl);

        if (res != CURLE_OK) {
            DEBUG_PRINT("[ %sDEBUG%s ]: [%sGemini Models%s] %scurl_easy_perform() %sfailed%s: %s%s%s\n", 
		lt_pl, nml, cy, nml, gr, 
		red, gr, 
		red, curl_easy_strerror(res), nml);

            g_string_append_printf(model_output_str, "Error: Failed to fetch models from Gemini API: %s\n", curl_easy_strerror(res));
        } else {
            DEBUG_PRINT("[ %sDEBUG%s ]: [%sGemini Models%s] %sRaw response: %s%s%s\n", 
		lt_pl, nml, cy, nml, gr, 
		red, chunk.memory, nml);

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

    g_free(cy);
    g_free(yl);
    g_free(gr);
    g_free(red);
    g_free(nml);

    return g_string_free(model_output_str, FALSE); 
}



char* perform_gemini_request_ex_types(AppContext *app, const char *prompt, const char *terminal_context, gboolean include_history, TagType prompt_type, TagType context_type) {
    return perform_gemini_request_ex_types_internal(
        app, prompt, terminal_context, include_history, prompt_type, context_type, FALSE);
}

char* perform_gemini_send_directives(AppContext *app) {
    if (!app || !GENERAL_DIRECTIVES || GENERAL_DIRECTIVES[0] == '\0')
        return NULL;

    /* Gemini's Interactions API gives AITerm a server-side parent interaction.
     * The directives are included in this initialization call only.  Normal
     * requests reference the resulting interaction ID instead of resending the
     * directive block.  This avoids the per-request token cost while keeping
     * the directives available to Gemini. */
    const ProviderConfig *provider = ai_provider_current_config(app);
    if (provider && provider->api_key &&
        provider->model &&
        provider->kind == PROVIDER_KIND_GEMINI_GENERATE) {
        char *raw = gemini_create_directive_interaction(app);
        if (raw) {
            char *normalized = gemini_interaction_to_generate_json(raw);
            free(raw);
            if (normalized) return normalized;
        }
    }

    /* Fallback for non-Google/custom Gemini endpoints that do not expose the
     * Interactions API.  This remains an initialization/provider-change call,
     * and normal requests still do not inject GENERAL_DIRECTIVES. */
    return perform_gemini_request_ex_types_internal(
        app, "Acknowledge these AITerm directives in one short sentence.",
        NULL, FALSE, TAG_SYSTEM, TAG_NONE, TRUE);
}

char* perform_gemini_request_ex(AppContext *app, const char *prompt, const char *terminal_context, gboolean include_history) {
    return perform_gemini_request_ex_types(app, prompt, terminal_context, include_history, TAG_USER, TAG_LOG_DUMP);
}

char* perform_gemini_request(AppContext *app, const char *prompt, const char *terminal_context) {
    return perform_gemini_request_ex_types(app, prompt, terminal_context, TRUE, TAG_USER, TAG_LOG_DUMP);
}
