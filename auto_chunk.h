// Part of the aiterm project
// auto_chunk.h
// Provider-independent automatic payload chunking
// By: Peter Talbott
// Added: 0.9.12-alpha

#ifndef AUTO_CHUNK_H
#define AUTO_CHUNK_H

#include <glib.h>
#include <gtk/gtk.h>

/* Forward declaration keeps this header independent of gui.h. */
typedef struct AppContext AppContext;

/*
 * Default limits for the first implementation.
 * These are deliberately conservative and can be changed later by the
 * provider/configuration layer without changing the chunking algorithm.
 */
#define AITERM_AUTO_CHUNK_DEFAULT_MAX 32768U
#define AITERM_AUTO_CHUNK_DEFAULT_OVERLAP 1024U

/* Conservative single-request input budgets in bytes. These are byte budgets,
 * intentionally below provider token limits because terminal text can have
 * a much higher token/byte ratio than ordinary prose. */
#define AITERM_CONTEXT_BUDGET_GROQ    6000U
#define AITERM_CONTEXT_BUDGET_GEMINI  32768U
#define AITERM_CONTEXT_BUDGET_OPENAI  16000U
#define AITERM_CONTEXT_BUDGET_OTHER   16000U

gsize effective_max_chunk(const AppContext *app);

/* Return a conservative total input-context byte budget for the active provider. */
gsize auto_chunk_request_budget(const AppContext *app);

/* Split using an explicit per-request limit without changing persistent configuration. */
GPtrArray *auto_chunk_payload_with_limit(AppContext *app, const char *payload,
                                         gsize max_bytes);

/* Initialize the AppContext chunk subsystem. */
void auto_chunk_init(AppContext *app);

/* Reset runtime statistics/state while retaining configuration. */
void auto_chunk_reset(AppContext *app);

/*
 * Configure the maximum chunk size and overlap in bytes.
 * A max_chunk of zero restores the default maximum.
 * An overlap of zero disables overlap.
 */
void auto_chunk_configure(AppContext *app, gsize max_chunk, gsize overlap);

/* Return TRUE when payload exceeds the active maximum chunk size. */
gboolean auto_chunk_needed(AppContext *app, const char *payload);

/*
 * Split payload into independently addressable text chunks.
 *
 * The returned GPtrArray owns each chunk and has g_free as its element
 * destroy function.  The caller owns the array and must release it with
 * g_ptr_array_unref() (or g_ptr_array_free(chunks, TRUE)).
 *
 * No source bytes are intentionally discarded.  The splitter prefers:
 *   1. paragraph/newline boundaries
 *   2. whitespace boundaries
 *   3. a UTF-8-safe hard boundary
 *
 * Overlap, when enabled, intentionally repeats bytes between adjacent
 * chunks so context at a boundary is not lost.
 */
GPtrArray *auto_chunk_payload(AppContext *app, const char *payload);

/* Release/reset the subsystem state.  AppContext itself remains owned by AITerm. */
void auto_chunk_shutdown(AppContext *app);

#endif /* AUTO_CHUNK_H */
