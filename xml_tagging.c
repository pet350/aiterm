// part of aiterm project
// xml_tagging.c
// Utility for adding xml tags to AI payload 
// By: Peter Talbott
// Assisted by: Gemini
// August 2026

// Modified 0.9.9-beta 
// Added ANSI Colors to debug messages
// Makes Logs easier to follow

#include <stdlib.h>
#include <glib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <time.h>
#include <json-c/json.h>
#include <vte/vte.h>
#include <gtk/gtk.h>
#include <pthread.h>

#include "gui.h"
#include "utils.h"
#include "xml_tagging.h"
#include "openai.h"
#include "update.h"
#include "gemini.h"

// Added 0.9.5-beta
// For wrapping payload in XML tags that AI will understand
char* xml_wrap_with_type(AppContext *app, const char *input, TagType type) {
    if (!input) return NULL;
    if (!app->xml.tagging_enabled) return g_strdup(input);
    if (!type) return g_strdup(input);

    GString *xml_buffer = g_string_new(NULL);
    time_t now = time(NULL);
    char time_str[20];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", localtime(&now));
    char DEBUG_PREFIX[256];
    snprintf(DEBUG_PREFIX, 256, "[%s DEBUG %s]: [%sXML WRAP%s] %sXML type is %s",
	app->ansi.lt_purple, app->ansi.normal, app->ansi.cyan, app->ansi.normal, app->ansi.lt_green, app->ansi.lt_red);
    switch(type) {
        case TAG_NONE:
            DEBUG_PRINT("%s NONE%s, not wrapping%s\n", DEBUG_PREFIX, app->ansi.lt_green, app->ansi.normal);
            g_string_append(xml_buffer, input);
            break;
        case TAG_HISTORY:
            DEBUG_PRINT("%s HISTORY, %swrapping with %s<context>%s\n", DEBUG_PREFIX, app->ansi.lt_green, app->ansi.lt_red, app->ansi.normal);
            g_string_append(xml_buffer, "<context");
            if (app->session.session_uuid) {
                g_string_append_printf(xml_buffer, " session=\"%s\"", app->session.session_uuid);
            }
            g_string_append_printf(xml_buffer, ">%s</context>\n", input);
            break;
        case TAG_MEMORY:
            DEBUG_PRINT("%s MEMORY, %swrapping with %s<memory>%s \n", DEBUG_PREFIX, app->ansi.lt_green, app->ansi.lt_red, app->ansi.normal);
            g_string_printf(xml_buffer, "<memory");
            if (app->xml.database_timestamp) {
                g_string_append_printf(xml_buffer, " timestamp=\"%s\"", app->xml.database_timestamp);
            }
            if (app->session.session_uuid) {
                g_string_append_printf(xml_buffer, " session=\"%s\"", app->session.session_uuid);
            }
            g_string_append_printf(xml_buffer, ">%s</memory>\n", input);
            break;
        case TAG_LOG_DUMP: // Was ** TAG_TEE: **
            DEBUG_PRINT("%s LOG_DUMP, %swrapping with %s<log_dump>%s\n", DEBUG_PREFIX, app->ansi.lt_green, app->ansi.lt_red, app->ansi.normal);
            g_string_printf(xml_buffer, "<log_dump timestamp=\"%s\"", time_str);
            if (app->session.session_uuid) {
                g_string_append_printf(xml_buffer, " session=\"%s\"", app->session.session_uuid);
            }
            g_string_append_printf(xml_buffer, ">%s</log_dump>\n", input);
            break;
        case TAG_SYSTEM:
            DEBUG_PRINT("%s SYSTEM, %swrapping with %s<system>%s\n", DEBUG_PREFIX, app->ansi.lt_green, app->ansi.lt_red, app->ansi.normal);
            g_string_printf(xml_buffer, "<system timestamp=\"%s\"", time_str);
            if (app->session.session_uuid) {
                g_string_append_printf(xml_buffer, " session=\"%s\"", app->session.session_uuid);
            }
            g_string_append_printf(xml_buffer, ">%s</system>\n", input);
            break;
        case TAG_STATUS:
            DEBUG_PRINT("%s STATUS, %swrapping with %s<status>%s\n", DEBUG_PREFIX, app->ansi.lt_green, app->ansi.lt_red, app->ansi.normal);
            // System is live data payload, we will timestamp it
            g_string_printf(xml_buffer, "<status timestamp=\"%s\"", time_str);
            if (app->session.session_uuid) {
                g_string_append_printf(xml_buffer, " session=\"%s\"", app->session.session_uuid);
            }
            g_string_append_printf(xml_buffer, ">%s</status>\n", input);
            break;
    }
    // Return the string and destroy the container, keeping the data alive
    return g_string_free(xml_buffer, FALSE);
}

char* xml_wrap(AppContext *app, const char *input) {
    if (!app) return input ? g_strdup(input) : NULL;
    return xml_wrap_with_type(app, input, app->xml.type);
}

