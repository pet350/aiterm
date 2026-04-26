#ifndef GUI_H
#define GUI_H

#include <gtk/gtk.h>

#define APP_NAME    "aiterm"

typedef struct AppContext {
    GtkWidget *window;
    GtkWidget *terminal_view;
    GtkWidget *gemini_view;
    GtkWidget *entry;
    GtkWidget *status_label;

    GtkCssProvider *ai_css_provider;

    char *api_key;
    char *provider; // "openai" or "gemini"
    char *model;    // e.g., "gpt-4o" or "gemini-1.5-flash"

    char *db_host;
    char *db_user;
    char *db_pass;
    char *db_name;

    char *terminal_font; // e.g., "Monospace 10"
    char *ai_font;       // e.g., "Sans 10"

    double transparency; // Value from 0.0 (transparent) to 1.0 (opaque)  -- Added 0.7.1-beta
}AppContext;

void setup_gui(AppContext *app);

#endif
