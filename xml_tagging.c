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
char* xml_wrap_with_type_timestamp(AppContext *app, const char *input, TagType type, const char *timestamp) {
    if (!input) return NULL;
    if (!app->xml.tagging_enabled) return g_strdup(input);
    if (!type) return g_strdup(input);

    GString *xml_buffer = g_string_new(NULL);
    time_t now = time(NULL);
    char time_str[20];
    if (timestamp && *timestamp) {
        g_strlcpy(time_str, timestamp, sizeof(time_str));
    } else {
        strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", localtime(&now));
    }

    char DEBUG_PREFIX[256];
    snprintf(DEBUG_PREFIX, sizeof(DEBUG_PREFIX), "[%s DEBUG %s]: [%sXML WRAP%s] %sXML type is %s",
        app->ansi.lt_purple, app->ansi.normal, app->ansi.cyan, app->ansi.normal,
        app->ansi.lt_green, app->ansi.lt_red);

    switch(type) {
        case TAG_NONE:
            DEBUG_PRINT("%s NONE%s, not wrapping%s\n", DEBUG_PREFIX, app->ansi.lt_green, app->ansi.normal);
            g_string_append(xml_buffer, input);
            break;
        case TAG_HISTORY:
            DEBUG_PRINT("%s HISTORY, %swrapping with %s<history>%s\n", DEBUG_PREFIX, app->ansi.lt_green, app->ansi.lt_red, app->ansi.normal);
            g_string_printf(xml_buffer, "<history timestamp=\"%s\"", time_str);
            if (app->session.session_uuid)
                g_string_append_printf(xml_buffer, " session=\"%s\"", app->session.session_uuid);
            g_string_append_printf(xml_buffer, ">%s</history>\n", input);
            break;
        case TAG_MEMORY:
            DEBUG_PRINT("%s MEMORY, %swrapping with %s<memory>%s\n", DEBUG_PREFIX, app->ansi.lt_green, app->ansi.lt_red, app->ansi.normal);
            g_string_printf(xml_buffer, "<memory timestamp=\"%s\"", time_str);
            if (app->session.session_uuid)
                g_string_append_printf(xml_buffer, " session=\"%s\"", app->session.session_uuid);
            g_string_append_printf(xml_buffer, ">%s</memory>\n", input);
            break;
        case TAG_LOG_DUMP:
            DEBUG_PRINT("%s LOG_DUMP/TEE, %swrapping with %s<tee>%s\n", DEBUG_PREFIX, app->ansi.lt_green, app->ansi.lt_red, app->ansi.normal);
            g_string_printf(xml_buffer, "<tee timestamp=\"%s\"", time_str);
            if (app->session.session_uuid)
                g_string_append_printf(xml_buffer, " session=\"%s\"", app->session.session_uuid);
            g_string_append_printf(xml_buffer, ">%s</tee>\n", input);
            break;
        case TAG_SYSTEM:
            DEBUG_PRINT("%s SYSTEM, %swrapping with %s<system>%s\n", DEBUG_PREFIX, app->ansi.lt_green, app->ansi.lt_red, app->ansi.normal);
            g_string_printf(xml_buffer, "<system timestamp=\"%s\"", time_str);
            if (app->session.session_uuid)
                g_string_append_printf(xml_buffer, " session=\"%s\"", app->session.session_uuid);
            g_string_append_printf(xml_buffer, ">%s</system>\n", input);
            break;
        case TAG_STATUS:
            DEBUG_PRINT("%s STATUS, %swrapping with %s<status>%s\n", DEBUG_PREFIX, app->ansi.lt_green, app->ansi.lt_red, app->ansi.normal);
            g_string_printf(xml_buffer, "<status timestamp=\"%s\"", time_str);
            if (app->session.session_uuid)
                g_string_append_printf(xml_buffer, " session=\"%s\"", app->session.session_uuid);
            g_string_append_printf(xml_buffer, ">%s</status>\n", input);
            break;
        case TAG_USER:
            DEBUG_PRINT("%s USER, %swrapping with %s<user>%s\n", DEBUG_PREFIX, app->ansi.lt_green, app->ansi.lt_red, app->ansi.normal);
            g_string_printf(xml_buffer, "<user timestamp=\"%s\"", time_str);
            if (app->session.session_uuid)
                g_string_append_printf(xml_buffer, " session=\"%s\"", app->session.session_uuid);
            g_string_append_printf(xml_buffer, ">%s</user>\n", input);
            break;
        case TAG_SNMP:
            DEBUG_PRINT("%s SNMP, %swrapping with %s<snmp>%s\n", DEBUG_PREFIX, app->ansi.lt_green, app->ansi.lt_red, app->ansi.normal);
            g_string_printf(xml_buffer, "<snmp timestamp=\"%s\"", time_str);
            if (app->session.session_uuid)
                g_string_append_printf(xml_buffer, " session=\"%s\"", app->session.session_uuid);
            g_string_append_printf(xml_buffer, ">%s</snmp>\n", input);
            break;
    }
    return g_string_free(xml_buffer, FALSE);
}

char* xml_wrap_with_type(AppContext *app, const char *input, TagType type) {
    return xml_wrap_with_type_timestamp(app, input, type, NULL);
}

char* xml_wrap(AppContext *app, const char *input) {
    if (!app) return input ? g_strdup(input) : NULL;
    return xml_wrap_with_type(app, input, app->xml.type);
}

