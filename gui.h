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

    char *api_key;
    char *provider; // "openai" or "gemini"
    char *model;    // e.g., "gpt-4o" or "gemini-1.5-flash"

    char *db_host;
    char *db_user;
    char *db_pass;
    char *db_name;
}AppContext;

void setup_gui(AppContext *app);

#endif
