// part of aiterm project
// ai_provider.h - Provider abstraction layer
// By: Peter Talbott
// 0.9.10-beta

#ifndef AI_PROVIDER_H
#define AI_PROVIDER_H

#include "gui.h"

/* Send a prompt using the provider selected in app->provider_config. */
char *ai_provider_send(AppContext *app, const char *prompt);

/* Provider dispatch variant that preserves captured terminal context for providers
 * that support it, notably Gemini. */
char *ai_provider_send_with_context(AppContext *app, const char *prompt,
                                    const char *terminal_context);

/* Extract assistant text from a provider response. */
char *ai_provider_extract_text(const char *raw_json);

/* Extract common token usage and update the application's token display. */
void ai_provider_extract_usage(AppContext *app, const char *raw_json);

/* Return a human-readable protocol/provider description for diagnostics. */
const char *ai_provider_protocol_name(const AppContext *app);

#endif
