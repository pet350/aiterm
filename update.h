// Part of project: aiterm
// update.h
// C Program header file for updating AI functions
// By: Peter Talbott
// With assistance from Gemini and OpenAI
// April, May 2026

#ifndef UPDATE_H
#define UPDATE_H

#include <stdio.h>
#include "gui.h"

typedef struct {
    AppContext *app;
    char *response_text;
    char *terminal_output;
} TeeResponseData;

// Function prototypes defines in updare.c
void execute_ai_command(AppContext *app, const char *ai_text);
void update_status_label(AppContext *app, const char *status);
void on_input_activate(GtkEntry *entry, gpointer data);
void process_for_ai(AppContext *app, const char *text, gboolean is_input);
void write_to_ai_pane(AppContext *app, const char *header, const char *body, const char *header_tag, const char *body_tag);

int risk_str_to_int(const char *str);
const char* risk_int_to_str(int risk);

gboolean update_gui_with_response(gpointer data);
gboolean request_human_approval(AppContext *app, const char *input_text);

#endif
