// Part of the AITerm project
// provider_manager_gui.h
// AI Provider Manager GUI
// 0.9.10-beta

#ifndef PROVIDER_MANAGER_GUI_H
#define PROVIDER_MANAGER_GUI_H

#include <gtk/gtk.h>
#include "gui.h"

typedef struct {
    AppContext *app;
    GtkWidget *dialog;
    GtkWidget *provider_combo;
    GtkWidget *provider_name;
    GtkWidget *model_entry;
    GtkWidget *base_url_entry;
    GtkWidget *endpoint_entry;
    GtkWidget *auth_header_entry;
    GtkWidget *auth_scheme_entry;
    GtkWidget *api_key_entry;
    GtkWidget *query_key_entry;
    GtkWidget *api_key_query_check;
    GtkWidget *protocol_combo;
    GtkWidget *status_label;
} ProviderManagerDialog;

void open_provider_manager_window(AppContext *app);
void close_provider_manager(AppContext *app);

#endif
