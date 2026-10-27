// part of aiterm project
// ai_provider.h - Provider abstraction layer
// By: Peter Talbott
// 0.9.11-alpha

#ifndef AI_PROVIDER_H
#define AI_PROVIDER_H

#include "gui.h"
#include "utils.h"

/* Send a prompt using the provider selected in app->provider_config. */
char *ai_provider_send(AppContext *app, const char *prompt);

/* Send GENERAL_DIRECTIVES only when AITerm initializes or the provider changes. */
char *ai_provider_send_directives(AppContext *app);
void ai_provider_invalidate_directives(AppContext *app);
void ai_provider_config_lock(AppContext *app);
void ai_provider_config_unlock(AppContext *app);
char *ai_provider_send_directives_locked(AppContext *app);

/* Provider jobs use an immutable per-thread snapshot. The snapshot is copied
 * while provider_runtime.mutex is held briefly, then the mutex is released before any
 * network I/O, retry backoff, AutoChunk processing, or DB work begins. */
void ai_provider_snapshot_begin(AppContext *app);
void ai_provider_snapshot_end(void);
const ProviderConfig *ai_provider_current_config(const AppContext *app);
const char *ai_provider_current_directive_id(AppContext *app);
const char *ai_provider_current_directive_model(AppContext *app);
guint64 ai_provider_current_generation(AppContext *app);
gboolean ai_provider_snapshot_is_current(AppContext *app);
gboolean ai_provider_snapshot_active(AppContext *app);

/* Asynchronous provider dispatch. The worker owns the complete provider
 * transaction, including HTTP, AutoChunk, rate limiting, and retries. The
 * callback is invoked on the GTK main thread with ownership of raw_response. */
typedef void (*AIProviderAsyncCallback)(AppContext *app, char *raw_response, gpointer user_data);

gboolean ai_provider_send_with_context_async(AppContext *app, const char *prompt,
                                              const char *terminal_context,
                                              AIProviderAsyncCallback callback,
                                              gpointer user_data);
gboolean ai_provider_send_directives_async(AppContext *app,
                                            AIProviderAsyncCallback callback,
                                            gpointer user_data);
gboolean ai_provider_send_directives_async_locked(AppContext *app,
                                                   AIProviderAsyncCallback callback,
                                                   gpointer user_data);

/* Provider dispatch variant that preserves captured terminal context for providers
 * that support it, notably Gemini. */
char *ai_provider_send_with_context(AppContext *app, const char *prompt,
                                    const char *terminal_context);
char *ai_provider_send_with_context_types(AppContext *app, const char *prompt,
                                          const char *terminal_context,
                                          TagType prompt_type, TagType context_type);

char *provider_send_direct(AppContext *app, const char *prompt,
                                  const char *terminal_context,
                                  gboolean include_history);

/* Extract assistant text from a provider response. */
char *ai_provider_extract_text(const char *raw_json);

/* Extract common token usage and update the application's token display. */
void ai_provider_extract_usage(AppContext *app, const char *raw_json);

/* Return a human-readable protocol/provider description for diagnostics. */
const char *ai_provider_protocol_name(AppContext *app);

#endif
