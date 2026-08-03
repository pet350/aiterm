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
#define GEMINI_CACHE_MIN_TOKEN_FLOOR 32768   /* API minimum token threshold */
#define GEMINI_CACHE_TTL_MARGIN_SEC  30      /* Expiration buffer in seconds */


// Function prototypes
char *get_cache_dir(void);
char *gemini_cache_lookup(const char *prompt_hash);

void gemini_cache_store(const char *prompt_hash, const char *response_json);
void gemini_cache_init(AppContext *app);
void gemini_cache_clear(AppContext *app);
void gemini_cache_invalidate(AppContext *app);

gboolean gemini_cache_is_valid(AppContext *app, int current_history_turns);
gboolean gemini_cache_create(AppContext *app, struct json_object *contents);


#endif // GEMINI_CACHE_H
