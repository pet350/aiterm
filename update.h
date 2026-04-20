#ifndef UPDATE_H
#define UPDATE_H

#include "gui.h"

typedef struct {
    AppContext *app;
    char *response_text;
    char *original_prompt;
} AIResponseData;

typedef struct {
    AppContext *app;
    char *response_text;
    char *terminal_output; // Add this to pass the command output to the UI thread
} TeeResponseData;

// Add this line so menu.c knows what this function is
void append_to_view(GtkWidget *view, const char *prefix, const char *text);

// This is the callback triggered by the "activate" signal (Pressing Enter)
void on_input_activate(GtkEntry *entry, gpointer data);

// Callback function
size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp);

void display_all_history(AppContext *app);
void append_to_view(GtkWidget *view, const char *prefix, const char *text);

// ADD THIS FOR v0.6-alpha
void process_for_ai(AppContext *app, const char *text, gboolean is_input);

#endif
