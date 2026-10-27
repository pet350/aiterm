// part of aiterm project
// gemini_cache.h
// Lifecycle management declarations for Gemini Context Caching
// By: Peter Talbott
// Assisted by: Gemini
// May 2026, July 2026

#ifndef GEMINI_CACHE_H
#define GEMINI_CACHE_H

#include <time.h>
#include <glib.h>
#include <json-c/json.h>
#include "gui.h"
#include "gemini.h"

#define GEMINI_CACHE_DEFAULT_TTL_SEC 3600    /* 1 hour default TTL */
#define GEMINI_CACHE_MIN_TOKEN_FLOOR 4096    /* Highest documented per-model minimum (1024-4096) */
#define GEMINI_CACHE_TTL_MARGIN_SEC  30      /* Expiration buffer in seconds */

/* History -> explicit cache wiring */
#define GEMINI_CACHE_WINDOW_BYTES      262144 /* Max history bytes frozen into one cache */
#define GEMINI_CACHE_MAX_DELTA_TURNS   12     /* More unfrozen turns than this -> rebuild */
#define GEMINI_CACHE_RETRY_BACKOFF_SEC 300    /* Wait after a failed create before retrying */
#define GEMINI_CACHE_BYTES_PER_TOKEN   4      /* Rough bytes->tokens estimate */


// Function prototypes
char *get_cache_dir(void);
char *gemini_cache_lookup(const char *prompt_hash);

void gemini_cache_store(const char *prompt_hash, const char *response_json);
void gemini_cache_init(AppContext *app);
void gemini_cache_clear(AppContext *app);
void gemini_cache_invalidate(AppContext *app);

gboolean gemini_cache_is_valid(AppContext *app, int current_history_turns);
gboolean gemini_cache_create(AppContext *app, struct json_object *contents,
                             long est_tokens, const char *system_text);

/* Fills `contents` with the history turns that must travel in the request
 * body and returns TRUE when a valid server-side cache covers the rest (the
 * caller must then add "cachedContent" and omit system_instruction).  On
 * FALSE, `contents` is untouched and the caller loads history normally. */
gboolean gemini_cache_prepare_history(AppContext *app, struct json_object *contents,
                                      gsize history_budget);


#endif // GEMINI_CACHE_H
