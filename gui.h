// Part of the aiterm project
// gui.h
// Header file for gui functions
// AppContext moved to its own header file!
// By: Peter Talbott
// With assistance from Gemini and OpenAI
// May 2026

#ifndef GUI_H
#define GUI_H

#include <mariadb/mysql.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <gtk/gtk.h>
#include <vte/vte.h>
#include <stdbool.h>
#include <json-c/json.h>
#include <pthread.h>
#include <curl/curl.h>
#include <openssl/evp.h>
#include <time.h>

#include "appcontext.h"
#include "ratelimit.h"
#include "idle.h"

// Function prototypes
void init_local_cmd_history(AppContext *app);
void init_token_tracker(AppContext *app);
void setup_gui(AppContext *app);
void dispatch_command_to_pane(AppContext *app, int target_pane_id, const char *cmd);
void on_exec_confirm_response(GtkDialog *dialog, gint response_id, gpointer user_data);
void on_vte_populate_popup(VteTerminal *vte, GtkWidget *popup_menu, gpointer user_data);
void append_ai_text(AppContext *app, const char *text, const char *tag_name);
void apply_custom_theme();
void apply_block_cursor_to_input(GtkWidget *entry);
void set_icon(AppContext *app);
void on_upload_clicked(GtkButton *button, gpointer data);
void on_copy_clicked(GtkButton *button, gpointer data);
void on_buffer_changed_scroll(GtkTextBuffer *buffer, gpointer data);
void on_terminal_child_exited(VteTerminal *terminal, gint status, gpointer user_data);
void on_tab_close_requested(AppContext *app, GtkWidget *term);
void on_tab_changed(GtkNotebook *notebook, GtkWidget *page, guint page_num, gpointer data);
void tab_report_current_async(AppContext *app);   /* re-report the active tab (e.g. after a session change) */
void on_notebook_double_click(GtkGestureMultiPress *gesture, gint n_press, gdouble x, gdouble y, gpointer user_data);
void on_tab_close_clicked(GtkButton *button, gpointer user_data);
void add_terminal_tab(AppContext *app);
void update_snmp_ticker_payload(AppContext *app, const char *payload_summary);
void on_send_button_clicked(GtkButton *button, gpointer user_data);

// Boolean function Prototypes
gboolean scroll_ai_pane_to_bottom(AppContext *app);
gboolean scroll_to_bottom_idle(gpointer data);
gboolean on_entry_key_press(GtkWidget *widget, GdkEventKey *event, gpointer user_data);
gboolean on_notebook_button_press(GtkWidget *widget, GdkEventButton *event, gpointer user_data);
gboolean update_snmp_ticker_scroll(gpointer user_data);

GtkWidget *create_snmp_ticker(AppContext *app);

#endif

