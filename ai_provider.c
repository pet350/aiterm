// part of aiterm project
// ai_provider.c - Provider abstraction layer
// By: Peter Talbott
// 0.9.11-beta-tee-fix

#include <string.h>
#include <json-c/json.h>

#include "ai_provider.h"
#include "gui.h"
#include "gemini.h"
#include "openai.h"
#include "utils.h"
#include "auto_chunk.h"
#include "ratelimit.h"

static gboolean provider_is_gemini(const AppContext *app) {
    return app && app->provider_config.kind == PROVIDER_KIND_GEMINI_GENERATE;
}

extern int debug_mode;

static char *provider_send_direct_types(AppContext *app, const char *prompt,
                                        const char *terminal_context,
                                        gboolean include_history,
                                        TagType prompt_type, TagType context_type) {
    if (!app || !prompt) return NULL;

    if (provider_is_gemini(app))
        return perform_gemini_request_ex_types(app, prompt, terminal_context, include_history, prompt_type, context_type);

    return send_to_openai_ex_types(app, prompt, terminal_context, include_history, prompt_type, context_type);
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

        DEBUG_PRINT("[ AUTOCHUNK ] Ingesting chunk %u/%u (%zu bytes).\n",
                    i + 1, chunk_count, strlen(chunk_text));

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

    DEBUG_PRINT("[ AUTOCHUNK ] Every source chunk was dispatched: %u/%u successful ingestion passes.\n",
                successful, chunk_count);

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
    DEBUG_PRINT("[ AUTOCHUNK ] Final synthesis using %u/%u chunk digests (%zu bytes).\n",
                successful, chunk_count, final_context_len);

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
                red, app->provider_config.model ? app->provider_config.model : "default",
                gr, nml);
    } else {
        DBG_PRINT("OpenAI-compatible %s%s / %s%s\n",
                red, app->provider_config.provider ? app->provider_config.provider : "openai",
                app->provider_config.model ? app->provider_config.model : "default", nml);
    }

    gsize context_len = terminal_context ? strlen(terminal_context) : 0;
    gsize request_budget = auto_chunk_request_budget(app);
    gsize fixed_budget = strlen(prompt) + 1024U;
    gsize available_budget = request_budget > fixed_budget
        ? request_budget - fixed_budget : 0;
    gsize terminal_budget = (available_budget * 3U) / 4U;

    DEBUG_PRINT("[ %sDEBUG%s ] [%sProvider%s] %sterminal_context=%s%zu%s bytes%s\n",
        lt_pl, nml, cy, nml, gr, red, context_len, gr, nml);
    DEBUG_PRINT("[ %sDEBUG%s ]: [%sAUTOCHUNK%s] %sprovider=[%s%s%s] request_budget=[%s%zu%s] fixed=[%s%zu%s] initial_terminal_budget=[%s%zu%s]%s\n",
	lt_pl, nml, cy, nml, gr,
        red, app->provider_config.provider ? app->provider_config.provider : "openai", gr,
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

const char *ai_provider_protocol_name(const AppContext *app) {
    if (!app) return "unknown";
    return provider_is_gemini(app) ? "gemini-generateContent" : "openai-chat-completions";
}
