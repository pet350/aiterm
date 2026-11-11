// part of aiterm project
// ai_provider.c - Provider abstraction layer
// By: Peter Talbott
// 0.9.11-beta-tee-fix

#include <string.h>
#include <json-c/json.h>

#include "appcontext.h"
#include "ai_provider.h"
#include "gemini.h"
#include "openai.h"
#include "utils.h"
#include "auto_chunk.h"
#include "ratelimit.h"

static gboolean provider_is_gemini(AppContext *app) {
    const ProviderConfig *provider = ai_provider_current_config(app);
    return provider && provider->kind == PROVIDER_KIND_GEMINI_GENERATE;
}

extern int debug_mode;


typedef struct {
    AppContext *app;
    ProviderConfig config;
    char *directive_interaction_id;
    char *directive_model;
    guint64 generation;
} AIProviderThreadSnapshot;

static void provider_snapshot_free(gpointer data) {
    AIProviderThreadSnapshot *snap = data;
    if (!snap) return;
    free_provider_config(&snap->config);
    g_free(snap->directive_interaction_id);
    g_free(snap->directive_model);
    g_free(snap);
}

static GPrivate provider_snapshot_private = G_PRIVATE_INIT(provider_snapshot_free);

void ai_provider_snapshot_begin(AppContext *app) {
    if (!app) return;

    AIProviderThreadSnapshot *old = g_private_get(&provider_snapshot_private);
    if (old && old->app == app)
        return;

    AIProviderThreadSnapshot *snap = g_malloc0(sizeof(*snap));
    snap->app = app;

    g_mutex_lock(&app->provider_runtime.mutex);
    snap->generation = app->provider_runtime.generation;
    ProviderConfig *src = &app->provider_config;
    snap->config.name = g_strdup(src->name);
    snap->config.model = g_strdup(src->model);
    snap->config.api_key = g_strdup(src->api_key);
    snap->config.base_url = g_strdup(src->base_url);
    snap->config.endpoint = g_strdup(src->endpoint);
    snap->config.auth_header = g_strdup(src->auth_header);
    snap->config.auth_scheme = g_strdup(src->auth_scheme);
    snap->config.query_key_name = g_strdup(src->query_key_name);
    snap->config.provider = g_strdup(src->provider);
    snap->config.kind = src->kind;
    snap->config.api_key_in_query = src->api_key_in_query;
    snap->directive_interaction_id = g_strdup(app->gemini_cache.directive_interaction_id);
    snap->directive_model = g_strdup(app->gemini_cache.directive_model);
    g_mutex_unlock(&app->provider_runtime.mutex);

    g_private_set(&provider_snapshot_private, snap);
}

void ai_provider_snapshot_end(void) {
    g_private_set(&provider_snapshot_private, NULL);
}

const ProviderConfig *ai_provider_current_config(const AppContext *app) {
    AIProviderThreadSnapshot *snap = g_private_get(&provider_snapshot_private);
    if (snap && snap->app == app)
        return &snap->config;
    return app ? &app->provider_config : NULL;
}

const char *ai_provider_current_directive_id(AppContext *app) {
    AIProviderThreadSnapshot *snap = g_private_get(&provider_snapshot_private);
    if (snap && snap->app == app)
        return snap->directive_interaction_id;
    return app ? app->gemini_cache.directive_interaction_id : NULL;
}

const char *ai_provider_current_directive_model(AppContext *app) {
    AIProviderThreadSnapshot *snap = g_private_get(&provider_snapshot_private);
    if (snap && snap->app == app)
        return snap->directive_model;
    return app ? app->gemini_cache.directive_model : NULL;
}

guint64 ai_provider_current_generation(AppContext *app) {
    AIProviderThreadSnapshot *snap = g_private_get(&provider_snapshot_private);
    if (snap && snap->app == app)
        return snap->generation;
    if (!app) return 0;
    g_mutex_lock((GMutex *)&app->provider_runtime.mutex);
    guint64 generation = app->provider_runtime.generation;
    g_mutex_unlock((GMutex *)&app->provider_runtime.mutex);
    return generation;
}

gboolean ai_provider_snapshot_is_current(AppContext *app) {
    AIProviderThreadSnapshot *snap = g_private_get(&provider_snapshot_private);
    if (!snap || snap->app != app || !app)
        return FALSE;

    g_mutex_lock(&app->provider_runtime.mutex);
    gboolean current = (snap->generation == app->provider_runtime.generation);
    g_mutex_unlock(&app->provider_runtime.mutex);
    return current;
}

gboolean ai_provider_snapshot_active(AppContext *app) {
    AIProviderThreadSnapshot *snap = g_private_get(&provider_snapshot_private);
    return snap && snap->app == app;
}

static char *provider_send_direct_types(AppContext *app, const char *prompt,
                                        const char *terminal_context,
                                        gboolean include_history,
                                        TagType prompt_type, TagType context_type) {
    if (!app || !prompt) return NULL;

    if (provider_is_gemini(app))
        return perform_gemini_request_ex_types(app, prompt, terminal_context, include_history, prompt_type, context_type);

    return send_to_openai_ex_types(app, prompt, terminal_context, include_history, prompt_type, context_type);
}

void ai_provider_config_lock(AppContext *app) {
    if (app) g_mutex_lock(&app->provider_runtime.mutex);
}

void ai_provider_config_unlock(AppContext *app) {
    if (app) g_mutex_unlock(&app->provider_runtime.mutex);
}

typedef struct {
    AppContext *app;
    char *prompt;
    char *terminal_context;
    gboolean directives;
    AIProviderAsyncCallback callback;
    gpointer user_data;
} AIProviderAsyncJob;

typedef struct {
    AppContext *app;
    char *raw_response;
    AIProviderAsyncCallback callback;
    gpointer user_data;
} AIProviderAsyncResult;

static gboolean ai_provider_async_complete(gpointer data) {
    AIProviderAsyncResult *result = data;
    if (!result)
        return G_SOURCE_REMOVE;

    if (result->callback) {
        result->callback(result->app, result->raw_response, result->user_data);
        result->raw_response = NULL;
    }

    if (result->raw_response)
        free(result->raw_response);

    g_free(result);
    return G_SOURCE_REMOVE;
}

static gpointer ai_provider_async_worker(gpointer data) {
    AIProviderAsyncJob *job = data;
    if (!job || !job->app)
        return NULL;

    SET_THREAD_NAME("aiterm-provider");

    char *raw_response = NULL;

    /* A provider-change directive refresh has priority over normal requests,
     * but waiting happens only on this worker. GTK never waits for it. */
    if (!job->directives) {
        g_mutex_lock(&job->app->provider_runtime.mutex);
        while (job->app->provider_runtime.directives_pending)
            g_cond_wait(&job->app->provider_runtime.cond, &job->app->provider_runtime.mutex);
        g_mutex_unlock(&job->app->provider_runtime.mutex);
    }

    ai_provider_snapshot_begin(job->app);

    if (job->directives) {
        raw_response = ai_provider_send_directives_locked(job->app);
    } else {
        raw_response = ai_provider_send_with_context_types(
            job->app, job->prompt, job->terminal_context, TAG_USER, TAG_LOG_DUMP);
    }

    if (job->directives) {
        guint64 worker_generation = ai_provider_current_generation(job->app);
        g_mutex_lock(&job->app->provider_runtime.mutex);
        if (worker_generation == job->app->provider_runtime.generation) {
            job->app->provider_runtime.directives_pending = FALSE;
            g_cond_broadcast(&job->app->provider_runtime.cond);
        }
        g_mutex_unlock(&job->app->provider_runtime.mutex);
    }

    ai_provider_snapshot_end();

    AIProviderAsyncResult *result = g_malloc0(sizeof(*result));
    result->app = job->app;
    result->raw_response = raw_response;
    result->callback = job->callback;
    result->user_data = job->user_data;

    g_main_context_invoke(NULL, ai_provider_async_complete, result);

    g_free(job->prompt);
    g_free(job->terminal_context);
    g_free(job);
    return NULL;
}

static gboolean ai_provider_queue_async(AppContext *app,
                                         const char *prompt,
                                         const char *terminal_context,
                                         gboolean directives,
                                         AIProviderAsyncCallback callback,
                                         gpointer user_data) {
    if (!app)
        return FALSE;

    AIProviderAsyncJob *job = g_malloc0(sizeof(*job));
    job->app = app;
    job->prompt = prompt ? g_strdup(prompt) : NULL;
    job->terminal_context = terminal_context ? g_strdup(terminal_context) : NULL;
    job->directives = directives;
    job->callback = callback;
    job->user_data = user_data;

    GThread *thread = g_thread_new("aiterm-provider", ai_provider_async_worker, job);
    if (!thread) {
        g_free(job->prompt);
        g_free(job->terminal_context);
        g_free(job);
        return FALSE;
    }

    g_thread_unref(thread);
    return TRUE;
}

gboolean ai_provider_send_with_context_async(AppContext *app, const char *prompt,
                                              const char *terminal_context,
                                              AIProviderAsyncCallback callback,
                                              gpointer user_data) {
    if (!app || !prompt)
        return FALSE;

    return ai_provider_queue_async(app, prompt, terminal_context, FALSE, callback, user_data);
}

gboolean ai_provider_send_directives_async_locked(AppContext *app,
                                                   AIProviderAsyncCallback callback,
                                                   gpointer user_data) {
    if (!app)
        return FALSE;

    app->provider_runtime.directives_pending = TRUE;

    if (!ai_provider_queue_async(app, NULL, NULL, TRUE, callback, user_data)) {
        app->provider_runtime.directives_pending = FALSE;
        g_cond_broadcast(&app->provider_runtime.cond);
        return FALSE;
    }

    return TRUE;
}

gboolean ai_provider_send_directives_async(AppContext *app,
                                            AIProviderAsyncCallback callback,
                                            gpointer user_data) {
    if (!app)
        return FALSE;

    g_mutex_lock(&app->provider_runtime.mutex);
    gboolean queued = ai_provider_send_directives_async_locked(app, callback, user_data);
    g_mutex_unlock(&app->provider_runtime.mutex);
    return queued;
}

void ai_provider_invalidate_directives(AppContext *app) {
    if (!app) return;
    gemini_directive_invalidate(app);
}

char *ai_provider_send_directives_locked(AppContext *app) {
    if (!app) return NULL;

    gemini_directive_invalidate(app);

    DEBUG_PRINT("[ %sDEBUG%s ]: [%sDIRECTIVES%s] %sSending GENERAL_DIRECTIVES to provider [%s%s%s].%s\n",
        app->ansi.lt_purple, app->ansi.normal,
        app->ansi.cyan, app->ansi.normal,
        app->ansi.yellow, app->ansi.red,
        ai_provider_current_config(app)->provider ? ai_provider_current_config(app)->provider : "unknown",
        app->ansi.green, app->ansi.normal);

    if (provider_is_gemini(app))
        return perform_gemini_send_directives(app);

    return send_to_openai_send_directives(app);
}

char *ai_provider_send_directives(AppContext *app) {
    if (!app) return NULL;
    gboolean owns_snapshot = !ai_provider_snapshot_active(app);
    if (owns_snapshot)
        ai_provider_snapshot_begin(app);
    char *result = ai_provider_send_directives_locked(app);
    if (owns_snapshot)
        ai_provider_snapshot_end();
    return result;
}

char *provider_send_direct(AppContext *app, const char *prompt,
                                  const char *terminal_context,
                                  gboolean include_history) {
    return provider_send_direct_types(app, prompt, terminal_context, include_history, TAG_USER, TAG_LOG_DUMP);
}

/*
 * Large terminal captures cannot be placed into one provider request when the
 * configured conservative request budget is smaller than the capture.  This
 * routine therefore performs a map/reduce pass:
 *
 *   1. Split the complete capture into bounded chunks.
 *   2. Send EVERY chunk to the selected provider in a context-ingestion pass.
 *   3. Keep a compact factual digest from each pass.
 *   4. Send those digests, plus normal conversation history, to one final
 *      synthesis request using the user's original question.
 *
 * This keeps the provider request within its normal budget while ensuring the
 * model actually receives every source chunk.  The intermediate passes are
 * intentionally not written to user/assistant history.
 */
static char *provider_send_chunked_context(AppContext *app,
                                           const char *prompt,
                                           const char *terminal_context,
                                           gsize terminal_budget,
                                           TagType prompt_type, TagType context_type) {
    if (!app || !prompt || !terminal_context || !*terminal_context)
        return provider_send_direct_types(app, prompt, terminal_context, TRUE, prompt_type, context_type);

    char *lt_pl   = g_strdup(app->ansi.lt_purple);
    char *cy      = g_strdup(app->ansi.cyan);
    char *yl      = g_strdup(app->ansi.yellow);
    char *gr      = g_strdup(app->ansi.green);
    char *red     = g_strdup(app->ansi.red);
    char *nml     = g_strdup(app->ansi.normal);

    GPtrArray *chunks = auto_chunk_payload_with_limit(
        app, terminal_context, terminal_budget);

    if (!chunks || chunks->len <= 1) {
        if (chunks) g_ptr_array_unref(chunks);
        return provider_send_direct_types(app, prompt, terminal_context, TRUE, prompt_type, context_type);
    }

    const guint chunk_count = chunks->len;
    gsize final_digest_budget = (terminal_budget * 3U) / 4U;
    if (final_digest_budget < chunk_count * 64U)
        final_digest_budget = chunk_count * 64U;

    /* Reserve room for the per-chunk labels/markers, then divide the remaining
     * content allowance evenly so every chunk can contribute to synthesis. */
    gsize marker_budget = (gsize)chunk_count * 64U;
    gsize digest_content_budget = final_digest_budget > marker_budget
        ? final_digest_budget - marker_budget : 0U;
    gsize per_digest = chunk_count > 0
        ? digest_content_budget / chunk_count : 0U;

    GString *digests = g_string_new(NULL);
    guint successful = 0;

    DEBUG_PRINT("[ %sDEBUG%s ]: [%sAUTOCHUNK%s] %sOversized context=[%s%zu%s] bytes requires [%s%u%s] provider ingestion passes.%s\n",
	lt_pl,  nml, cy, nml, gr,
        red, strlen(terminal_context), gr,
	red, chunk_count, gr, nml);

    for (guint i = 0; i < chunk_count; i++) {
        const char *chunk_text = g_ptr_array_index(chunks, i);
        if (!chunk_text) continue;

        char *chunk_prompt = g_strdup_printf(
            "AITERM CONTEXT INGESTION PASS %u OF %u.\n"
            "The user will ultimately ask this question:\n%s\n\n"
            "Read the COMPLETE terminal/TEE chunk below. Extract concrete "
            "facts, exact values, errors, filenames, code details, sequence "
            "information, and other evidence relevant to that question. "
            "This is an intermediate pass, not the final answer. Do not "
            "invent information that is not present. Return a compact "
            "factual digest for a later synthesis pass. The source chunk is "
            "attached separately as AITERM terminal context.\n",
            i + 1, chunk_count, prompt);

        if (app->sys.ratelimit_enabled)
            ratelimit_wait_if_needed(&app->limiter);

        DEBUG_PRINT("[ %sDEBUG%s ]: [%sAUTOCHUNK%s] %sIngesting chunk [%s%u%s/%s%u%s] (%s%zu%s bytes).%s\n",
		lt_pl, nml, cy, nml, gr,
                red, i + 1, gr, 
		red, chunk_count, gr,
		red, strlen(chunk_text), gr, nml);

        char *raw = provider_send_direct_types(app, chunk_prompt, chunk_text, FALSE, TAG_SYSTEM, context_type);
        g_free(chunk_prompt);

        char *digest = raw ? ai_provider_extract_text(raw) : NULL;
        if (raw)
            free(raw);

        if (digest && *digest) {
            successful++;

            /* Bound each digest so the final synthesis request can contain
             * every chunk result, even with conservative providers such as
             * Groq. */
            gsize digest_len = strlen(digest);
            if (digest_len > per_digest) {
                char *short_digest = g_strndup(digest, per_digest);
                g_string_append_printf(digests,
                    "\n[Source chunk %u/%u digest, truncated to %zu bytes]\n%s\n",
                    i + 1, chunk_count, per_digest, short_digest);
                g_free(short_digest);
            } else {
                g_string_append_printf(digests,
                    "\n[Source chunk %u/%u digest]\n%s\n",
                    i + 1, chunk_count, digest);
            }
            g_free(digest);
        } else {
            g_string_append_printf(digests,
                "\n[Source chunk %u/%u digest unavailable: ingestion request failed]\n",
                i + 1, chunk_count);
        }
    }

    DEBUG_PRINT("[ %sDEBUG%s ]: [%sAUTOCHUNK%s] %sEvery source chunk was dispatched: [%s%u%s/%s%u%s] successful ingestion passes.%s\n",
		lt_pl, nml, cy, nml, gr,
                red, successful, gr, 
		red, chunk_count, gr, nml);

    /* Preserve aggregate chunk state after the individual provider calls reset
     * their local chunk statistics. */
    app->chunk.original_size = strlen(terminal_context);
    app->chunk.total_bytes = app->chunk.original_size;
    app->chunk.chunk_count = chunk_count;
    app->chunk.current_chunk = chunk_count;
    app->chunk.current_chunk_size = chunk_count > 0
        ? strlen((char *)g_ptr_array_index(chunks, chunk_count - 1)) : 0;
    app->chunk.bytes_processed = app->chunk.total_bytes;
    app->chunk.was_chunked = TRUE;
    app->chunk.processing = FALSE;
    app->chunk.complete = TRUE;

    g_ptr_array_unref(chunks);

    char *final_prompt = g_strdup_printf(
        "%s\n\n"
        "AITERM has processed the complete terminal/TEE capture in %u "
        "separate source chunks. The terminal context supplied with this "
        "request contains a factual digest from EVERY chunk. Synthesize "
        "those digests into the final answer. If a digest says information "
        "was unavailable, do not fill the gap from guesswork.",
        prompt, chunk_count);

    gsize final_context_len = digests->len;
    DEBUG_PRINT("[ %sDEBUG%s ]: [%sAUTOCHUNK%s] %sFinal synthesis using [%s%u%s/%s%u%s] chunk digests (%s%zu%s bytes).%s\n",
		lt_pl, nml, cy, nml, gr,
                red, successful, gr,
		red, chunk_count, gr,
		red, final_context_len, gr, nml);

    if (app->sys.ratelimit_enabled)
        ratelimit_wait_if_needed(&app->limiter);

    char *final_raw = provider_send_direct_types(app, final_prompt, digests->str, TRUE, prompt_type, TAG_MEMORY);
    g_free(final_prompt);
    g_string_free(digests, TRUE);

    g_free(lt_pl);
    g_free(cy);
    g_free(yl);
    g_free(gr);
    g_free(red);
    g_free(nml);

    return final_raw;
}

char *ai_provider_send_with_context_types(AppContext *app, const char *prompt,
                                          const char *terminal_context,
                                          TagType prompt_type, TagType context_type) {
    if (!app || !prompt) return NULL;

    gboolean owns_snapshot = !ai_provider_snapshot_active(app);
    if (owns_snapshot)
        ai_provider_snapshot_begin(app);

    char *lt_pl   = g_strdup(app->ansi.lt_purple);
    char *cy      = g_strdup(app->ansi.cyan);
    char *yl      = g_strdup(app->ansi.yellow);
    char *gr      = g_strdup(app->ansi.green);
    char *red     = g_strdup(app->ansi.red);
    char *nml     = g_strdup(app->ansi.normal);

    char *RV = NULL;

    DEBUG_PRINT("[ %sDEBUG%s ]: [%sProvider%s] %sdispatch ->%s",
        lt_pl, nml, cy, nml, yl, gr);
    if (provider_is_gemini(app)) {
        DBG_PRINT("Gemini [%s%s%s]%s\n",
                red, ai_provider_current_config(app)->model ? ai_provider_current_config(app)->model : "default",
                gr, nml);
    } else {
        DBG_PRINT("OpenAI-compatible %s%s / %s%s\n",
                red, ai_provider_current_config(app)->provider ? ai_provider_current_config(app)->provider : "openai",
                ai_provider_current_config(app)->model ? ai_provider_current_config(app)->model : "default", nml);
    }

    gsize context_len = terminal_context ? strlen(terminal_context) : 0;
    gsize request_budget = auto_chunk_request_budget(app);
    gsize fixed_budget = strlen(prompt) + 1024U;
    gsize available_budget = request_budget > fixed_budget
        ? request_budget - fixed_budget : 0;
    gsize terminal_budget = (available_budget * 3U) / 4U;

    DEBUG_PRINT("[ %sDEBUG%s ]: [%sProvider%s] %sterminal_context=%s%zu%s bytes%s\n",
        lt_pl, nml, cy, nml, gr, red, context_len, gr, nml);
    DEBUG_PRINT("[ %sDEBUG%s ]: [%sAUTOCHUNK%s] %sprovider=[%s%s%s] request_budget=[%s%zu%s] fixed=[%s%zu%s] initial_terminal_budget=[%s%zu%s]%s\n",
	lt_pl, nml, cy, nml, gr,
        red, ai_provider_current_config(app)->provider ? ai_provider_current_config(app)->provider : "openai", gr,
        red, request_budget, gr, 
	red, fixed_budget, gr, 
	red, terminal_budget, gr, nml);

    if (terminal_context && *terminal_context && terminal_budget > 0 &&
        context_len > terminal_budget) {
        RV = provider_send_chunked_context(app, prompt, terminal_context, terminal_budget, prompt_type, context_type);
    } else {
        RV = provider_send_direct_types(app, prompt, terminal_context, TRUE, prompt_type, context_type);
    }

    g_free(lt_pl);
    g_free(cy);
    g_free(yl);
    g_free(gr);
    g_free(red);
    g_free(nml);

    if (owns_snapshot)
        ai_provider_snapshot_end();
    return RV;
}

char *ai_provider_send_with_context(AppContext *app, const char *prompt,
                                    const char *terminal_context) {
    return ai_provider_send_with_context_types(app, prompt, terminal_context, TAG_USER, TAG_LOG_DUMP);
}

char *ai_provider_send(AppContext *app, const char *prompt) {
    return ai_provider_send_with_context_types(app, prompt, NULL, TAG_USER, TAG_NONE);
}

char *ai_provider_extract_text(const char *raw_json) {
    return extract_ai_text(raw_json);
}

void ai_provider_extract_usage(AppContext *app, const char *raw_json) {
    if (!app || !raw_json) return;

    struct json_object *root = json_tokener_parse(raw_json);
    if (!root) return;

    struct json_object *usage = NULL;
    struct json_object *total = NULL;
    struct json_object *output = NULL;

    if (provider_is_gemini(app)) {
        if (json_object_object_get_ex(root, "usageMetadata", &usage)) {
            if (json_object_object_get_ex(usage, "totalTokenCount", &total))
                app->tokens.current = json_object_get_int64(total);
            if (json_object_object_get_ex(usage, "candidatesTokenCount", &output))
                app->tokens.last = json_object_get_int64(output);
        }
    } else {
        if (json_object_object_get_ex(root, "usage", &usage)) {
            /* OpenAI-compatible APIs normally use total_tokens and completion_tokens. */
            if (json_object_object_get_ex(usage, "total_tokens", &total))
                app->tokens.current = json_object_get_int64(total);
            if (json_object_object_get_ex(usage, "completion_tokens", &output))
                app->tokens.last = json_object_get_int64(output);
        }
    }

    if (usage) {
        extern gboolean refresh_token_display(gpointer data);
        g_idle_add(refresh_token_display, app);
    }

    json_object_put(root);
}

const char *ai_provider_protocol_name(AppContext *app) {
    if (!app) return "unknown";
    return provider_is_gemini(app) ? "gemini-generateContent" : "openai-chat-completions";
}
