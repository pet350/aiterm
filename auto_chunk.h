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

#define AITERM_AUTO_CHUNK_DEFAULT_MAX 32768U
#define AITERM_AUTO_CHUNK_DEFAULT_OVERLAP 1024U
#define AITERM_CONTEXT_BUDGET_GROQ    6000U
#define AITERM_CONTEXT_BUDGET_GEMINI  32768U
#define AITERM_CONTEXT_BUDGET_OPENAI  16000U
#define AITERM_CONTEXT_BUDGET_OTHER   16000U

gsize effective_max_chunk(const AppContext *app);
gsize effective_overlap(const AppContext *app, gsize chunk_size);
gsize utf8_safe_boundary(const char *payload, gsize start, gsize candidate);
gsize find_natural_boundary(const char *payload, gsize start, gsize limit);
gsize auto_chunk_request_budget(const AppContext *app);
void auto_chunk_init(AppContext *app);
void auto_chunk_reset(AppContext *app);
void auto_chunk_configure(AppContext *app, gsize max_chunk, gsize overlap);
gboolean auto_chunk_needed(AppContext *app, const char *payload);
GPtrArray *auto_chunk_payload_internal(AppContext *app, const char *payload, gsize forced_max);
GPtrArray *auto_chunk_payload(AppContext *app, const char *payload);
GPtrArray *auto_chunk_payload_with_limit(AppContext *app, const char *payload, gsize max_bytes);
void auto_chunk_shutdown(AppContext *app);

#endif /* AUTO_CHUNK_H */

