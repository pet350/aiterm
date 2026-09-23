// Part of the aiterm project
// provider_keys.h
// API provider key reference / status window
// By: Peter Talbott
// With assistance from Gemini and OpenAI
// September 2026

#ifndef PROVIDER_KEYS_H
#define PROVIDER_KEYS_H

#include "gui.h"

typedef struct {
    const char *name;
    const char *config_name;
    const char *url;
    char **key_ptr;
    gboolean local_only;
} ProviderKeyInfo;

// Display the aiTerm API Provider Keys reference window.
// API key values are never displayed.
void provider_keys_show(AppContext *app);

#endif /* PROVIDER_KEYS_H */
