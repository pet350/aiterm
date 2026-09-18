// part of aiterm project
// ai_provider.c - Provider abstraction layer
// By: Peter Talbott
// 0.9.10-beta

#include <string.h>
#include <json-c/json.h>

#include "ai_provider.h"
#include "gemini.h"
#include "openai.h"
#include "utils.h"

static gboolean provider_is_gemini(const AppContext *app) {
    return app && app->provider_config.kind == PROVIDER_KIND_GEMINI_GENERATE;
}

char *ai_provider_send_with_context(AppContext *app, const char *prompt,
                                    const char *terminal_context) {
    if (!app || !prompt) return NULL;

    if (provider_is_gemini(app)) {
        DEBUG_PRINT("[ DEBUG ]: [Provider] dispatch -> Gemini (%s), terminal_context=%zu bytes\n",
                    app->provider_config.model ? app->provider_config.model : "default",
                    terminal_context ? strlen(terminal_context) : 0UL);
        /* Use the context-aware Gemini entry point so the GTK-thread VTE snapshot
         * captured by update.c survives the provider abstraction layer. */
        return perform_gemini_request(app, prompt, terminal_context);
    }

    DEBUG_PRINT("[ DEBUG ]: [Provider] dispatch -> OpenAI-compatible (%s / %s), terminal_context=%zu bytes\n",
                app->provider_config.provider ? app->provider_config.provider : "openai",
                app->provider_config.model ? app->provider_config.model : "default",
                terminal_context ? strlen(terminal_context) : 0UL);
    return send_to_openai(app, prompt);
}

char *ai_provider_send(AppContext *app, const char *prompt) {
    return ai_provider_send_with_context(app, prompt, NULL);
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
