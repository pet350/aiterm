/* update.h
* Part of project: aiterm
* C Program header file for updating AI functions
* By: Peter Talbott
* With assistance from Gemini and OpenAI
* April, May 2026
*/


#ifndef UPDATE_H
#define UPDATE_H

#include "gui.h"

typedef struct {
    AppContext *app;
    char *response_text;
    char *terminal_output;
} TeeResponseData;

void update_status_label(AppContext *app, const char *status);
void on_input_activate(GtkEntry *entry, gpointer data);
void process_for_ai(AppContext *app, const char *text, gboolean is_input);
void write_to_ai_pane(AppContext *app, const char *header, const char *body, const char *header_tag, const char *body_tag);
gboolean update_gui_with_response(gpointer data);

#endif
