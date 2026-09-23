// part of aiterm project
// ai_provider.c - Provider abstraction layer
// By: Peter Talbott
// 0.9.11-alpha

#include <string.h>
#include <json-c/json.h>

#include "ai_provider.h"
#include "gui.h"
#include "gemini.h"
#include "openai.h"
#include "utils.h"

static gboolean provider_is_gemini(const AppContext *app) {
    return app && app->provider_config.kind == PROVIDER_KIND_GEMINI_GENERATE;
}

extern int debug_mode;

char *ai_provider_send_with_context(AppContext *app, const char *prompt,
                                    const char *terminal_context) {
    if (!app || !prompt) return NULL;

    char *lt_pl   = g_strdup(global_app->ansi.lt_purple);
    char *cy      = g_strdup(global_app->ansi.cyan);
    char *yl      = g_strdup(global_app->ansi.yellow);
    char *gr      = g_strdup(global_app->ansi.green);
    char *red     = g_strdup(global_app->ansi.red);
    char *nml     = g_strdup(global_app->ansi.normal);

    char *RV = g_malloc(128);

    DEBUG_PRINT("[ %sDEBUG%s ]: [%sProvider%s] %sdispatch ->%s", 
	lt_pl, nml, cy, nml, yl, gr);
    if (provider_is_gemini(app)) {
        DBG_PRINT("Gemini [%s%s%s]%s\n",
                red, app->provider_config.model ? app->provider_config.model : "default",
		gr, nml);
        DEBUG_PRINT("[ %sDEBUG%s ]: [%sProvider%s] %sterminal_context=%s%zu%s bytes%s\n",
		lt_pl, nml, cy, nml, gr,
                red, terminal_context ? strlen(terminal_context) : 0UL, gr, nml);

        RV = g_strdup(perform_gemini_request(app, prompt, terminal_context));
    } else {
        DBG_PRINT("OpenAI-compatible %s%s / %s%s\n",
                red, app->provider_config.provider ? app->provider_config.provider : "openai",
                app->provider_config.model ? app->provider_config.model : "default", nml);

 	DEBUG_PRINT("[ %sDEBUG%s ] [%sProvider%s] %sterminal_context=%s%zu%s bytes%s\n",
		lt_pl, nml, cy, nml, gr, 
                red, terminal_context ? strlen(terminal_context) : 0UL, gr, nml);

        RV = g_strdup(send_to_openai(app, prompt));
    }

    g_free(lt_pl);
    g_free(cy);
    g_free(yl);
    g_free(gr);
    g_free(red);
    g_free(nml);

    return(RV);
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
