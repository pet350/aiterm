#ifndef GUI_H
#define GUI_H

#include <gtk/gtk.h>

#define APP_VERSION "0.2-alpha"
#define APP_NAME    "Gemini-Term"

typedef struct AppContext {
    GtkWidget *window;
    GtkWidget *terminal_view;
    GtkWidget *gemini_view;
    GtkWidget *entry;
    GtkWidget *status_label;
    char *api_key;
    char *provider; // "openai" or "gemini"
    char *model;    // e.g., "gpt-4o" or "gemini-1.5-flash"
}AppContext;

void setup_gui(AppContext *app);

#endif
