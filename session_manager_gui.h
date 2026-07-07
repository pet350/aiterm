// part of aiterm project
// session_manager.h
// Header File used to manage sessions in a gui window
// By: Peter Talbott
// Assisted by: Gemini
// May 2026



#ifndef SESSION_MANAGER_GUI_H
#define SESSION_MANAGER_GUI_H

#include <stdio.h>
#include <stdlib.h>
#include <gtk/gtk.h>

#include "gui.h"
#include "utils.h"
#include "session_manager.h"


gboolean refresh_list_callback(gpointer data);
gboolean timed_refresh_list_callback(gpointer data);

void refresh_session_list(AppContext *app, GtkListStore *store);
void on_menu_session_manager(GtkMenuItem *item, gpointer data);
void on_add_clicked(GtkButton *btn, gpointer user_data);
void on_load_clicked(GtkButton *btn, gpointer user_data);
void on_default_clicked(GtkButton *btn, gpointer user_data);
void on_delete_clicked(GtkButton *btn, gpointer user_data);
void open_session_manager_window(AppContext *app);

#endif
