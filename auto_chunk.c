// Part of the aiterm project
// auto_chunk.c
// Provider-independent automatic payload chunking
// By: Peter Talbott
// Added: 0.9.12-alpha

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <gtk/gtk.h>

#include "auto_chunk.h"
#include "gui.h"
#include "utils.h"

gsize effective_max_chunk(const AppContext *app) {

    if (!app)
        return AITERM_AUTO_CHUNK_DEFAULT_MAX;

    char *lt_pl   = g_strdup(app->ansi.lt_purple);
    char *cy      = g_strdup(app->ansi.cyan);
    char *yl      = g_strdup(app->ansi.yellow);
    char *gr      = g_strdup(app->ansi.green);
    char *red     = g_strdup(app->ansi.red);
    char *nml     = g_strdup(app->ansi.normal);

    if (app->chunk.max_chunk > 0) {
        DEBUG_PRINT("[ %sDEBUG%s ] [%sMax Chunk%s] %sReturning max chunk size [%s%zu%s] byte %s\n",
            lt_pl, nml, cy, nml, gr,
            red, app->chunk.max_chunk, gr, nml);
        g_free(lt_pl);
        g_free(cy);
        g_free(yl);
        g_free(gr);
        g_free(red);
        g_free(nml);
        return app->chunk.max_chunk;
    }

    DEBUG_PRINT("[ %sDEBUG%s ] [%sMax Chunk%s] %sReturning default chunk size [%s%u%s] byte %s\n",
        lt_pl, nml, cy, nml, gr,
        red, AITERM_AUTO_CHUNK_DEFAULT_MAX, gr, nml);

    g_free(lt_pl);
    g_free(cy);
    g_free(yl);
    g_free(gr);
    g_free(red);
    g_free(nml);

    return AITERM_AUTO_CHUNK_DEFAULT_MAX;
}

static gsize effective_overlap(const AppContext *app, gsize chunk_size) {
    gsize overlap = app ? app->chunk.overlap : 0;

    if (!app)
        return 0;

    char *lt_pl   = g_strdup(app->ansi.lt_purple);
    char *cy      = g_strdup(app->ansi.cyan);
    char *yl      = g_strdup(app->ansi.yellow);
    char *gr      = g_strdup(app->ansi.green);
    char *red     = g_strdup(app->ansi.red);
    char *nml     = g_strdup(app->ansi.normal);

    /* Never allow overlap to consume the entire chunk. */
    if (overlap >= chunk_size)
        overlap = chunk_size > 1 ? chunk_size / 2 : 0;

    DEBUG_PRINT("[ %sDEBUG%s ] [%sAutoChunk%s] %sOverlap: [%s%ld%s]%s\n",
	lt_pl, nml, cy, nml, gr,
	red, overlap, gr, nml);

    g_free(lt_pl);
    g_free(cy);
    g_free(yl);
    g_free(gr);
    g_free(red);
    g_free(nml);

    return overlap;
}

/*
 * Move a byte offset backward until it is at the beginning of a UTF-8
 * character.  If the input is not valid UTF-8, the original byte offset is
 * retained because the payload may legitimately contain arbitrary terminal
 * bytes.
 */
static gsize utf8_safe_boundary(const char *payload, gsize start, gsize candidate) {
    if (!payload || candidate <= start)
        return candidate;

    if (g_utf8_validate(payload + start, candidate - start, NULL))
        return candidate;

    while (candidate > start &&
           ((guchar)payload[candidate] & 0xC0U) == 0x80U) {
        candidate--;
    }

    return candidate > start ? candidate : start;
}

/*
 * Find the best natural break at or before limit.  Newlines are preferred
 * over ordinary whitespace.  The returned offset is the first byte of the
 * next chunk, so the delimiter remains in the preceding chunk.
 */
static gsize find_natural_boundary(const char *payload,
                                   gsize start,
                                   gsize limit) {
    if (!payload || limit <= start)
        return limit;

    /* Prefer a paragraph boundary. */
    for (gsize i = limit; i > start + 1; i--) {
        if (payload[i - 1] == '\n' && payload[i - 2] == '\n')
            return i;
    }

    /* Then a normal newline. */
    for (gsize i = limit; i > start; i--) {
        if (payload[i - 1] == '\n')
            return i;
    }

    /* Finally use whitespace. */
    for (gsize i = limit; i > start; i--) {
        unsigned char c = (unsigned char)payload[i - 1];
        if (g_ascii_isspace(c))
            return i;
    }

    return limit;
}

gsize auto_chunk_request_budget(const AppContext *app) {
    if (!app)
        return AITERM_CONTEXT_BUDGET_OTHER;

    const char *provider = app->provider_config.provider;
    if (!provider || !*provider)
        return AITERM_CONTEXT_BUDGET_OPENAI;

    if (g_ascii_strcasecmp(provider, "groq") == 0)
        return AITERM_CONTEXT_BUDGET_GROQ;

    if (app->provider_config.kind == PROVIDER_KIND_GEMINI_GENERATE ||
        g_ascii_strcasecmp(provider, "gemini") == 0)
        return AITERM_CONTEXT_BUDGET_GEMINI;

    if (g_ascii_strcasecmp(provider, "openai") == 0)
        return AITERM_CONTEXT_BUDGET_OPENAI;

    return AITERM_CONTEXT_BUDGET_OTHER;
}

void auto_chunk_init(AppContext *app) {
    if (!app)
        return;

    app->chunk.enabled = TRUE;
    app->chunk.initialized = TRUE;

    if (app->chunk.max_chunk == 0)
        app->chunk.max_chunk = AITERM_AUTO_CHUNK_DEFAULT_MAX;

    if (app->chunk.overlap > app->chunk.max_chunk / 2)
        app->chunk.overlap = AITERM_AUTO_CHUNK_DEFAULT_OVERLAP;

    auto_chunk_reset(app);
}

void auto_chunk_reset(AppContext *app) {
    if (!app)
        return;

    char *lt_pl   = g_strdup(app->ansi.lt_purple);
    char *cy      = g_strdup(app->ansi.cyan);
    char *yl      = g_strdup(app->ansi.yellow);
    char *gr      = g_strdup(app->ansi.green);
    char *red     = g_strdup(app->ansi.red);
    char *nml     = g_strdup(app->ansi.normal);

    DEBUG_PRINT("[ %sDEBUG%s ] [%sAutoChunk%s] %sResetting%s\n",
	lt_pl, nml, cy, nml, red, nml); 
    app->chunk.original_size = 0;
    app->chunk.chunk_count = 0;
    app->chunk.current_chunk = 0;
    app->chunk.current_chunk_size = 0;
    app->chunk.was_chunked = FALSE;
    app->chunk.processing = FALSE;
    app->chunk.complete = TRUE;
    app->chunk.total_bytes = 0;
    app->chunk.bytes_processed = 0;

    g_free(lt_pl);
    g_free(cy);
    g_free(yl);
    g_free(gr);
    g_free(red);
    g_free(nml);
}

void auto_chunk_configure(AppContext *app, gsize max_chunk, gsize overlap) {
    if (!app)
        return;

    char *lt_pl   = g_strdup(app->ansi.lt_purple);
    char *cy      = g_strdup(app->ansi.cyan);
    char *yl      = g_strdup(app->ansi.yellow);
    char *gr      = g_strdup(app->ansi.green);
    char *red     = g_strdup(app->ansi.red);
    char *nml     = g_strdup(app->ansi.normal);

    if (max_chunk == 0)
        max_chunk = AITERM_AUTO_CHUNK_DEFAULT_MAX;

    if (overlap >= max_chunk)
        overlap = max_chunk / 2;

    app->chunk.max_chunk = max_chunk;
    app->chunk.overlap = overlap;

    if (!app->chunk.initialized)
        app->chunk.initialized = TRUE;

    DEBUG_PRINT("[ %sDEBUG%s ] [%sAutoChunk%s] %sConfigured%s\n",
	lt_pl, nml, cy, nml, red, nml);

    g_free(lt_pl);
    g_free(cy);
    g_free(yl);
    g_free(gr);
    g_free(red);
    g_free(nml);
}

gboolean auto_chunk_needed(AppContext *app, const char *payload) {
    if (!payload || !*payload)
        return FALSE;

    if (app && !app->chunk.enabled)
        return FALSE;

    return strlen(payload) > effective_max_chunk(app);
}

static GPtrArray *auto_chunk_payload_internal(AppContext *app, const char *payload, gsize forced_max) {
    GPtrArray *chunks = g_ptr_array_new_with_free_func(g_free);

    if (!app) {
        if (payload && *payload)
            g_ptr_array_add(chunks, g_strdup(payload));
        return chunks;
    }

    if (!app->chunk.initialized)
        auto_chunk_init(app);

    auto_chunk_reset(app);

    if (!payload || !*payload) {
        DEBUG_PRINT("[ AUTOCHUNK ] Empty payload; nothing to split.\n");
        return chunks;
    }

    const gsize total = strlen(payload);
    const gsize max_chunk = forced_max > 0 ? forced_max : effective_max_chunk(app);

    app->chunk.original_size = total;
    app->chunk.total_bytes = total;
    app->chunk.processing = TRUE;

    if (!app->chunk.enabled || total <= max_chunk) {
        g_ptr_array_add(chunks, g_strdup(payload));

        app->chunk.chunk_count = 1;
        app->chunk.current_chunk = 1;
        app->chunk.current_chunk_size = total;
        app->chunk.bytes_processed = total;
        app->chunk.was_chunked = FALSE;
        app->chunk.processing = FALSE;
        app->chunk.complete = TRUE;

        DEBUG_PRINT("[ AUTOCHUNK ] Payload %zu bytes fits in one chunk.\n", total);
        return chunks;
    }

    app->chunk.was_chunked = TRUE;

    gsize start = 0;
    gsize chunk_index = 0;

    while (start < total) {
        gsize limit = start + max_chunk;
        if (limit > total)
            limit = total;

        gsize cut = limit;
        if (limit < total)
            cut = find_natural_boundary(payload, start, limit);

        if (cut <= start)
            cut = limit;

        /* Keep UTF-8 intact when possible. */
        if (cut < total)
            cut = utf8_safe_boundary(payload, start, cut);

        if (cut <= start)
            cut = limit;

        const gsize chunk_len = cut - start;
        char *chunk = g_strndup(payload + start, chunk_len);
        g_ptr_array_add(chunks, chunk);

        chunk_index++;
        app->chunk.current_chunk = chunk_index;
        app->chunk.current_chunk_size = chunk_len;
        app->chunk.bytes_processed = cut;

        if (cut >= total)
            break;

        /*
         * Overlap is optional and deliberately bounded.  If it is enabled,
         * the next chunk starts before the previous chunk ended.  This does
         * not discard anything.  It only repeats boundary context.
         */
        gsize overlap = effective_overlap(app, chunk_len);
        gsize next_start = cut > overlap ? cut - overlap : cut;

        /* Always make forward progress. */
        if (next_start <= start)
            next_start = cut;

        start = next_start;
    }

    app->chunk.chunk_count = chunks->len;
    app->chunk.current_chunk = chunks->len;
    app->chunk.current_chunk_size = chunks->len > 0
        ? strlen((char *)g_ptr_array_index(chunks, chunks->len - 1))
        : 0;
    app->chunk.bytes_processed = total;
    app->chunk.processing = FALSE;
    app->chunk.complete = TRUE;

    DEBUG_PRINT("[ AUTOCHUNK ] Split %zu bytes into %u chunks (max=%zu overlap=%zu).\n",
                total,
                chunks->len,
                max_chunk,
                app->chunk.overlap);

    return chunks;
}

GPtrArray *auto_chunk_payload(AppContext *app, const char *payload) {
    return auto_chunk_payload_internal(app, payload, 0);
}

GPtrArray *auto_chunk_payload_with_limit(AppContext *app, const char *payload,
                                         gsize max_bytes) {
    if (max_bytes == 0)
        return auto_chunk_payload(app, payload);

    return auto_chunk_payload_internal(app, payload, max_bytes);
}

void auto_chunk_shutdown(AppContext *app) {
    if (!app)
        return;

    auto_chunk_reset(app);
    app->chunk.initialized = FALSE;
}
