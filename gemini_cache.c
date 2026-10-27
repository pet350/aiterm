// part of aiterm project
// gemini_cache.c
// Implementation of Gemini Context Caching
// By: Peter Talbott
// Assisted by: Gemini
// May 2026 - August 2026

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include <json-c/json.h>

#include "gemini_cache.h"
#include "gui.h"
#include "gemini.h"
#include "update.h"
#include "utils.h"
#include "xml_tagging.h"
#include "ai_provider.h"

char *get_cache_dir(void) {
    const char *user_cache = g_get_user_cache_dir();
    char *dir = g_build_filename(user_cache, "aiterm", "gemini", NULL);
    g_mkdir_with_parents(dir, 0700);
    return dir;
}

char *gemini_cache_lookup(const char *prompt_hash) {
    if (!prompt_hash || strlen(prompt_hash) == 0) {
        return NULL;
    }

    char *dir = get_cache_dir();
    char *filename = g_strdup_printf("%s.json", prompt_hash);
    char *filepath = g_build_filename(dir, filename, NULL);
    g_free(filename);
    g_free(dir);

    char *content = NULL;
    gsize length = 0;

    if (g_file_get_contents(filepath, &content, &length, NULL)) {
        g_free(filepath);
        return content;
    }

    g_free(filepath);
    return NULL;
}

void gemini_cache_store(const char *prompt_hash, const char *response_json) {
    if (!prompt_hash || !response_json || strlen(prompt_hash) == 0) {
        return;
    }

    char *dir = get_cache_dir();
    char *filename = g_strdup_printf("%s.json", prompt_hash);
    char *filepath = g_build_filename(dir, filename, NULL);
    g_free(filename);
    g_free(dir);

    g_file_set_contents(filepath, response_json, -1, NULL);
    g_free(filepath);
}

// Safely initialize the caching sub-structure inside AppContext
void gemini_cache_init(AppContext *app) {
    if (!app) return;

    char *lt_pl   = g_strdup(global_app->ansi.lt_purple);
    char *cy      = g_strdup(global_app->ansi.cyan);
    char *yl      = g_strdup(global_app->ansi.yellow);
    char *gr      = g_strdup(global_app->ansi.green);
    char *red     = g_strdup(global_app->ansi.red);
    char *nml     = g_strdup(global_app->ansi.normal);

    app->gemini_cache.id = NULL;
    app->gemini_cache.created_at = 0;
    app->gemini_cache.turn_count = 0;
    app->gemini_cache.cache_model = NULL;
    app->gemini_cache.prefix_fingerprint[0] = '\0';
    app->gemini_cache.create_retry_after = 0;
    app->gemini_cache.directive_interaction_id = NULL;
    app->gemini_cache.directive_model = NULL;
    app->gemini_cache.directive_created_at = 0;
    app->gemini_cache.directive_active = FALSE;
    if (!app->gemini_cache.min_token_threshold) {
        app->gemini_cache.min_token_threshold = GEMINI_CACHE_MIN_TOKEN_FLOOR;
    }
    DEBUG_PRINT("[ %sDEBUG%s ]: [%sSmart Cache%s] %sinitialized. Minimum threshold set to [%s%ld%s] tokens.%s\n", 
	lt_pl, nml, cy, nml, yl,
        red, app->gemini_cache.min_token_threshold, yl, nml);

    g_free(cy);
    g_free(yl);
    g_free(gr);
    g_free(red);
    g_free(nml);

}

// Clear active caching parameters and release dynamic memory
void gemini_cache_clear(AppContext *app) {
    if (!app) return;
    if (app->gemini_cache.id) {
        g_free(app->gemini_cache.id);
        app->gemini_cache.id = NULL;
    }
    g_clear_pointer(&app->gemini_cache.cache_model, g_free);
    app->gemini_cache.prefix_fingerprint[0] = '\0';
    app->gemini_cache.created_at = 0;
    app->gemini_cache.turn_count = 0;
}

// Validate active cache lifetime and history integrity
gboolean gemini_cache_is_valid(AppContext *app, int current_history_turns) {
    if (!app || app->gemini_cache.id == NULL) {
        return FALSE;
    }

    time_t now = time(NULL);

    // Invalidate if cache age exceeds 50 minutes (3000s) or history turn count dropped
    if ((now - app->gemini_cache.created_at) >= (GEMINI_CACHE_DEFAULT_TTL_SEC - GEMINI_CACHE_TTL_MARGIN_SEC) || 
        current_history_turns < app->gemini_cache.turn_count) {
        DEBUG_PRINT("[ DEBUG ]: Invalidating old, expired, or mismatched Gemini Context Cache.\n");
        gemini_cache_clear(app);
        return FALSE;
    }

    return TRUE;
}

// Generate physical cache handle on the Gemini server side.
//
// `contents` must be chronological (oldest first).  `est_tokens` is the
// caller's estimate of the frozen prefix and is checked against
// min_token_threshold.  `system_text` (optional) is pinned into the cache:
// the API does not accept system_instruction on requests that reference
// cachedContent, so anything the model must see every turn has to live in the
// cache resource itself.
gboolean gemini_cache_create(AppContext *app, struct json_object *contents,
                             long est_tokens, const char *system_text) {
    if (!app || !contents) {
        return FALSE;
    }

    char *lt_pl   = g_strdup(app->ansi.lt_purple);
    char *cy      = g_strdup(app->ansi.cyan);
    char *yl      = g_strdup(app->ansi.yellow);
    char *gr      = g_strdup(app->ansi.green);
    char *red     = g_strdup(app->ansi.red);
    char *nml     = g_strdup(app->ansi.normal);

    gboolean success = FALSE;
    struct json_object *root = NULL;
    struct MemoryStruct chunk = { NULL, 0 };
    const int turn_count = json_object_array_length(contents);

    // Threshold < 0 disables explicit caching overhead entirely
    if (app->gemini_cache.min_token_threshold < 0) {
        DEBUG_PRINT("[ %sDEBUG%s ]: [%sCache%s] %sbypassed%s. Explicit caching disabled via threshold setting.%s\n",
            lt_pl, nml, cy, nml, red, yl, nml);
        goto out;
    }

    // Don't cache if the frozen prefix is below the local threshold floor
    if (est_tokens < app->gemini_cache.min_token_threshold) {
        DEBUG_PRINT("[ %sDEBUG%s ]: [%sCache%s] %screation bypassed. Estimated tokens [%s%ld%s] below floor [%s%ld%s].%s\n",
            lt_pl, nml, cy, nml, yl,
            red, est_tokens, yl,
            red, app->gemini_cache.min_token_threshold, yl, nml);
        goto out;
    }

    if (turn_count == 0) {
        goto out;
    }

    gemini_cache_clear(app);

    const ProviderConfig *api = ai_provider_current_config(app);
    if (!api) {
        goto out;
    }
    const char *model = (api->model && strlen(api->model) > 0) ? api->model : "gemini-1.5-flash-lite";
    const char *base_url = (api->base_url && strlen(api->base_url) > 0) ? api->base_url : "https://generativelanguage.googleapis.com/v1beta";
    const char *query_key = (api->query_key_name && strlen(api->query_key_name) > 0) ? api->query_key_name : "key";
    const char *api_key = api->api_key;

    char model_buf[256];
    if (strncmp(model, "models/", 7) == 0) {
        snprintf(model_buf, sizeof(model_buf), "%s", model);
    } else {
        snprintf(model_buf, sizeof(model_buf), "models/%s", model);
    }

    root = json_object_new_object();
    json_object_object_add(root, "model", json_object_new_string(model_buf));
    json_object_object_add(root, "contents", json_object_get(contents));
    json_object_object_add(root, "ttl", json_object_new_string("3600s"));
    json_object_object_add(root, "displayName", json_object_new_string("aiterm_history_cache"));

    if (system_text && system_text[0] != '\0') {
        struct json_object *sys_instruction = json_object_new_object();
        struct json_object *sys_parts = json_object_new_array();
        struct json_object *sys_part = json_object_new_object();
        json_object_object_add(sys_part, "text", json_object_new_string(system_text));
        json_object_array_add(sys_parts, sys_part);
        json_object_object_add(sys_instruction, "parts", sys_parts);
        json_object_object_add(root, "system_instruction", sys_instruction);
    }

    const char *post_data = json_object_to_json_string(root);

    char url[1024];
    snprintf(url, sizeof(url), "%s/cachedContents?%s=%s", base_url, query_key, api_key ? api_key : "");

    chunk.memory = calloc(1, 1);
    chunk.size = 0;

    CURL *curl_handle = curl_easy_init();

    if (curl_handle) {
        struct curl_slist *headers = curl_slist_append(NULL, "Content-Type: application/json");
        curl_easy_setopt(curl_handle, CURLOPT_URL, url);
        curl_easy_setopt(curl_handle, CURLOPT_POSTFIELDS, post_data);
        curl_easy_setopt(curl_handle, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
        curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)&chunk);
        curl_easy_setopt(curl_handle, CURLOPT_CONNECTTIMEOUT, 10L);
        curl_easy_setopt(curl_handle, CURLOPT_TIMEOUT, 30L);

        CURLcode res = curl_easy_perform(curl_handle);
        if (res == CURLE_OK) {
            struct json_object *resp_root = chunk.memory ? json_tokener_parse(chunk.memory) : NULL;
            if (resp_root) {
                struct json_object *name_obj = NULL;
                if (json_object_object_get_ex(resp_root, "name", &name_obj)) {
                    app->gemini_cache.id = g_strdup(json_object_get_string(name_obj));
                    app->gemini_cache.cache_model = g_strdup(model);
                    app->gemini_cache.cached_token_count = (int)est_tokens;
                    app->gemini_cache.created_at = time(NULL);
                    app->gemini_cache.turn_count = turn_count;
                    success = TRUE;
                    DEBUG_PRINT("[ %sDEBUG%s ]: [%sCache%s] %sGemini Context Cache Created! ID: [%s%s%s], Cached Turns: [%s%d%s]%s\n",
                        lt_pl, nml, cy, nml, yl,
                        red, app->gemini_cache.id, yl,
                        red, app->gemini_cache.turn_count, yl, nml);
                } else {
                    struct json_object *error_obj = NULL, *msg_obj = NULL;
                    if (json_object_object_get_ex(resp_root, "error", &error_obj) &&
                        json_object_object_get_ex(error_obj, "message", &msg_obj)) {
                        DEBUG_PRINT("[ %sDEBUG%s ]: [%sCache%s] %sGemini Cache Creation %sSkipped%s: [%s%s%s]%s\n",
                            lt_pl, nml, cy, nml, yl,
                            red, yl, red, json_object_get_string(msg_obj), yl, nml);
                    } else {
                        DEBUG_PRINT("[ %sDEBUG%s ]: [%sCache%s] %sGemini Cache Creation %sFailed%s: Unexpected JSON format.%s\n",
                            lt_pl, nml, cy, nml, yl,
                            red, yl, nml);
                    }
                }
                json_object_put(resp_root);
            }
        } else {
            DEBUG_PRINT("[ %sDEBUG%s ]: [%sCURL%s] %sError%s during Cache Creation: [%s%s%s]%s\n",
                lt_pl, nml, cy, nml, red, yl,
                red, curl_easy_strerror(res), yl, nml);
        }

        curl_slist_free_all(headers);
        curl_easy_cleanup(curl_handle);
    }

out:
    g_free(lt_pl);
    g_free(cy);
    g_free(yl);
    g_free(gr);
    g_free(red);
    g_free(nml);

    if (root)
        json_object_put(root);
    free(chunk.memory);
    return success;
}

/* ---- History -> explicit cache wiring ----------------------------------- */

static const char *gemini_turn_text(struct json_object *turn) {
    struct json_object *parts = NULL, *part = NULL, *text = NULL;
    if (!turn || !json_object_object_get_ex(turn, "parts", &parts) ||
        json_object_get_type(parts) != json_type_array ||
        json_object_array_length(parts) == 0)
        return NULL;
    part = json_object_array_get_idx(parts, 0);
    if (!part || !json_object_object_get_ex(part, "text", &text))
        return NULL;
    return json_object_get_string(text);
}

/* Identity of a history row.  Rows carry an XML timestamp wrapper, so two
 * rows with identical text still hash differently.  Caller frees. */
static char *gemini_turn_fingerprint(struct json_object *turn) {
    const char *text = gemini_turn_text(turn);
    return text ? g_compute_checksum_for_string(G_CHECKSUM_SHA256, text, -1) : NULL;
}

static gsize gemini_turn_bytes(struct json_object *turn) {
    const char *text = gemini_turn_text(turn);
    return (text ? strlen(text) : 0) + 160;  /* same overhead allowance as the loader */
}

/* Index of the row whose fingerprint matches, or -1. */
static int gemini_find_fingerprint(struct json_object *window, const char *fingerprint) {
    int n = json_object_array_length(window);
    for (int i = 0; i < n; i++) {
        char *fp = gemini_turn_fingerprint(json_object_array_get_idx(window, i));
        gboolean match = fp && strcmp(fp, fingerprint) == 0;
        g_free(fp);
        if (match)
            return i;
    }
    return -1;
}

/*
 * Decide how this request carries its conversation history.
 *
 * load_history_to_gemini() returns rows NEWEST FIRST.  A cache needs a stable,
 * chronological prefix, so this function works on a private copy of that window:
 *
 *   - valid cache whose newest frozen row is still in the window and few/small
 *     rows have arrived since  -> append only the newer rows (chronological)
 *   - otherwise, and the window is large enough -> (re)build the cache from the
 *     whole window and send no history in the body
 *   - otherwise -> return FALSE; caller falls back to the normal budgeted load
 *
 * Rebuilding from the same DB rows is deliberately preferred over summarising:
 * the cache accepts far more than the per-request budget, so nothing needs to
 * be chunked or digested just to refresh it.
 */
gboolean gemini_cache_prepare_history(AppContext *app, struct json_object *contents,
                                      gsize history_budget) {
    if (!app || !contents || !app->sys.smart_cache_enabled)
        return FALSE;
    if (app->gemini_cache.min_token_threshold < 0 || !app->database.global_db_conn)
        return FALSE;

    const ProviderConfig *api = ai_provider_current_config(app);
    const char *model = (api && api->model && api->model[0]) ? api->model : NULL;
    if (!model)
        return FALSE;

    /* Caches are bound to the model that created them. */
    if (app->gemini_cache.id &&
        (!app->gemini_cache.cache_model || strcmp(app->gemini_cache.cache_model, model) != 0))
        gemini_cache_invalidate(app);

    struct json_object *window = json_object_new_array();
    load_history_to_gemini(app, window, "", GEMINI_CACHE_WINDOW_BYTES);
    const int n = json_object_array_length(window);
    if (n == 0) {
        json_object_put(window);
        return FALSE;
    }

    /* 1. Reuse an existing cache when its prefix is still intact. */
    if (app->gemini_cache.id && gemini_cache_is_valid(app, n)) {
        int idx = gemini_find_fingerprint(window, app->gemini_cache.prefix_fingerprint);
        if (idx >= 0 && idx <= GEMINI_CACHE_MAX_DELTA_TURNS) {
            gsize delta_bytes = 0;
            for (int i = 0; i < idx; i++)
                delta_bytes += gemini_turn_bytes(json_object_array_get_idx(window, i));

            if (delta_bytes <= history_budget) {
                for (int i = idx - 1; i >= 0; i--) {   /* newest-first -> chronological */
                    struct json_object *turn = json_object_array_get_idx(window, i);
                    json_object_get(turn);
                    json_object_array_add(contents, turn);
                }
                DEBUG_PRINT("[ DEBUG ]: [Cache] reusing cache, %d new turn(s) (%zu bytes) in request body.\n",
                            idx, delta_bytes);
                json_object_put(window);
                return TRUE;
            }
        }
        DEBUG_PRINT("[ DEBUG ]: [Cache] prefix stale or delta too large; rebuilding.\n");
        gemini_cache_invalidate(app);
    }

    /* 2. Build a new cache from the whole window if it is worth caching. */
    if (time(NULL) < app->gemini_cache.create_retry_after) {
        json_object_put(window);
        return FALSE;
    }

    gsize total_bytes = 0;
    for (int i = 0; i < n; i++)
        total_bytes += gemini_turn_bytes(json_object_array_get_idx(window, i));
    long est_tokens = (long)(total_bytes / GEMINI_CACHE_BYTES_PER_TOKEN);

    if (est_tokens < app->gemini_cache.min_token_threshold) {
        json_object_put(window);
        return FALSE;
    }

    struct json_object *chrono = json_object_new_array();
    for (int i = n - 1; i >= 0; i--) {
        struct json_object *turn = json_object_array_get_idx(window, i);
        json_object_get(turn);
        json_object_array_add(chrono, turn);
    }

    extern const char *GENERAL_DIRECTIVES;
    gboolean ok = gemini_cache_create(app, chrono, est_tokens, GENERAL_DIRECTIVES);
    if (ok) {
        char *fp = gemini_turn_fingerprint(json_object_array_get_idx(window, 0));
        if (fp) {
            g_strlcpy(app->gemini_cache.prefix_fingerprint, fp,
                      sizeof(app->gemini_cache.prefix_fingerprint));
            g_free(fp);
        } else {
            gemini_cache_invalidate(app);   /* cannot track the prefix: don't keep it */
            ok = FALSE;
        }
    }
    if (!ok)
        app->gemini_cache.create_retry_after = time(NULL) + GEMINI_CACHE_RETRY_BACKOFF_SEC;

    json_object_put(chrono);
    json_object_put(window);
    return ok;
}

// Build query payload using json-c
char *gemini_cache_build_query_payload(AppContext *app, const char *new_prompt_text) {
    if (!app || !new_prompt_text) return NULL;

    struct json_object *root = json_object_new_object();

    if (app->gemini_cache.id && gemini_cache_is_valid(app, app->gemini_cache.turn_count)) {
        json_object_object_add(root, "cachedContent", json_object_new_string(app->gemini_cache.id));
    }

    struct json_object *contents_arr = json_object_new_array();
    struct json_object *turn_obj = json_object_new_object();
    json_object_object_add(turn_obj, "role", json_object_new_string("user"));

    struct json_object *parts_arr = json_object_new_array();
    struct json_object *part_obj = json_object_new_object();
    char *wrapped_prompt = xml_wrap_with_type(app, new_prompt_text, TAG_USER);
    json_object_object_add(part_obj, "text", json_object_new_string(
        wrapped_prompt ? wrapped_prompt : new_prompt_text));
    g_free(wrapped_prompt);

    json_object_array_add(parts_arr, part_obj);
    json_object_object_add(turn_obj, "parts", parts_arr);
    json_object_array_add(contents_arr, turn_obj);

    json_object_object_add(root, "contents", contents_arr);

    const char *json_str = json_object_to_json_string(root);
    char *result = json_str ? g_strdup(json_str) : NULL;

    json_object_put(root);
    return result;
}

// Invalidate active cache and release allocated handle
void gemini_cache_invalidate(AppContext *app) {
    if (!app) return;

    DEBUG_PRINT("[ DEBUG ]: Manual cache invalidation triggered.\n");

    if (app->gemini_cache.id && ai_provider_current_config(app)->api_key) {
        const ProviderConfig *api = ai_provider_current_config(app);
        const char *base_url = (api->base_url && strlen(api->base_url) > 0) ? api->base_url : "https://generativelanguage.googleapis.com/v1beta";
        const char *query_key = (api->query_key_name && strlen(api->query_key_name) > 0) ? api->query_key_name : "key";

        char url[1024];
        snprintf(url, sizeof(url), "%s/%s?%s=%s", base_url, app->gemini_cache.id, query_key, api->api_key);

        CURL *curl_handle = curl_easy_init();
        if (curl_handle) {
            curl_easy_setopt(curl_handle, CURLOPT_URL, url);
            curl_easy_setopt(curl_handle, CURLOPT_CUSTOMREQUEST, "DELETE");
            curl_easy_setopt(curl_handle, CURLOPT_TIMEOUT, 5L);
            
            curl_easy_perform(curl_handle);
            curl_easy_cleanup(curl_handle);
        }
    }

    gemini_cache_clear(app);
}


