// part of aiterm project
// xml_tagging.c
// Utility for adding xml tags to AI payload 
// By: Peter Talbott
// Assisted by: Gemini
// August 2026

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
char* xml_wrap(AppContext *app, const char *input) {
    if (!input) return NULL;
    if (!app->xml.tagging_enabled) return g_strdup(input);
    if (!app->xml.type) return g_strdup(input);

    GString *xml_buffer = g_string_new(NULL);
    time_t now = time(NULL);
    char time_str[20];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", localtime(&now));
    switch(app->xml.type) {
        case TAG_NONE:
            // xml buffer was already initialized so we're just going to append input to it
            DEBUG_PRINT("[DEBUG]: [XML_WRAP] xml.type is none, not wrapping\n");
            g_string_append(xml_buffer, input);
            break;
        case TAG_HISTORY:
            DEBUG_PRINT("[DEBUG]: [XML_WRAP] xml.type is history, wrapping with <context>\n");
            // Being a history payload we don't want to send the current timestamp
            g_string_append(xml_buffer, "<context");
            if (app->session.session_uuid) {
                g_string_append_printf(xml_buffer, " session=\"%s\"", app->session.session_uuid);
            }
            g_string_append_printf(xml_buffer, ">%s</context>\n", input);
            break;
        case TAG_MEMORY:
            DEBUG_PRINT("[DEBUG]: [XML_WRAP] xml.type is memory, wrapping with <memory>\n");
            // Here this would be user data loaded from database,
            //if the timestamp from the database is available we'll use it
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
            DEBUG_PRINT("[DEBUG]: [XML_WRAP] xml.type is log_dump, wrapping with <log_dump>\n");
            // Tee is live data payload, we will timestamp it
            // A Wise AI assistant suggested log_dump as the tag instead of Tee
            g_string_printf(xml_buffer, "<log_dump timestamp=\"%s\"", time_str);
            if (app->session.session_uuid) {
                g_string_append_printf(xml_buffer, " session=\"%s\"", app->session.session_uuid);
            }
            g_string_append_printf(xml_buffer, ">%s</log_dump>\n", input);
            break;
        case TAG_SYSTEM:
            DEBUG_PRINT("[DEBUG]: [XML_WRAP] xml.type is system, wrapping with <system>\n");
            // System is live data payload, we will timestamp it
            g_string_printf(xml_buffer, "<system timestamp=\"%s\"", time_str);
            if (app->session.session_uuid) {
                g_string_append_printf(xml_buffer, " session=\"%s\"", app->session.session_uuid);
            }
            g_string_append_printf(xml_buffer, ">%s</system>\n", input);
            break;
        case TAG_STATUS:
            DEBUG_PRINT("[DEBUG]: [XML_WRAP] xml.type is status, wrapping with <status>\n");
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

