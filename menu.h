// Part of project: aiterm
// menu.h
// C Program header file for menu functions
// By: Peter Talbott
// With assistance from Gemini and OpenAI
// April, May 2026

#ifndef MENU_H
#define MENU_H

#include <stdio.h>
#include <gtk/gtk.h>
#include <string.h>
#include <stdlib.h>
#include "gui.h"
#include "menu.h"
#include "commands.h"
#include "history_manager_gui.h"
#include "noise_filter_manager_gui.h"
#include "policy_manager_gui.h"

// Callback data container to map menu actions to command strings safely
typedef struct {
    AppContext *app;
    char *cmd_base;
    gboolean requires_arg;
} MenuCommandData;

// Function Prototypes
GtkWidget* create_menu_bar(AppContext *app);

char* prompt_for_argument(GtkWindow *parent, char *action_title, char *placeholder);

void on_menu_history_manager_activate(GtkMenuItem *menuitem, gpointer user_data);
void on_menu_noise_filter_manager_activate(GtkMenuItem *menuitem, gpointer user_data);
void on_menu_policy_manager_activate(GtkMenuItem *menuitem, gpointer user_data);
void free_menu_data(gpointer data, GClosure *closure);
void on_clear(GtkWidget *widget, gpointer data);
void on_menu_exit(GtkWidget *widget, gpointer data);
void on_help(GtkWidget *widget, gpointer data);
void on_about(GtkWidget *widget, gpointer data);
void on_copy(GtkWidget *widget, gpointer data);
void on_paste(GtkWidget *widget, gpointer data);
void on_tee_toggle(GtkWidget *widget, gpointer data);
void on_autoreply_toggle(GtkWidget *widget, gpointer data);
void on_tee_toggle(GtkWidget *widget, gpointer data);
void on_autoreply_toggle(GtkWidget *widget, gpointer data);
void on_menu_command_clicked(GtkMenuItem *menuitem, gpointer user_data);
void on_transparency_changed(GtkRange *range, gpointer data);
void on_ai_transparency_changed(GtkRange *range, gpointer data);
void on_terminal_font_set(GtkFontButton *btn, gpointer data);
void on_ai_font_set(GtkFontButton *btn, gpointer data);
void on_preferences(GtkWidget *widget, gpointer data);
void on_tee_flush(GtkWidget *widget, gpointer data);
void on_menu_session_manager(GtkMenuItem *item, gpointer data);
void sync_toggle_ui_elements(AppContext *app);

#endif
