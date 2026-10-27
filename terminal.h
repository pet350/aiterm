// Part of project: aiterm
// terminal.h
// C Program header file for terminal functions
// By: Peter Talbott
// With assistance from Gemini and OpenAI
// April, May 2026

#ifndef TERMINAL_H
#define TERMINAL_H

#include <vte/vte.h>

// Forward declaration: tells the compiler AppContext exists
// without needing the full definition from gui.h yet.
//typedef struct AppContext AppContext;

GtkWidget* setup_terminal(AppContext *app);
void on_vte_child_exited(VteTerminal *vte, gint status, gpointer user_data);
void apply_terminal_transparency(AppContext *app);
void apply_visual_settings(AppContext *app);
char *terminal_capture_context(AppContext *app);
/* Capture a bounded context snapshot from every open terminal tab. GTK-thread only. */
char *terminal_capture_all_tabs_context(AppContext *app);
/* Retrieve a larger, bounded snapshot for one stable tab UUID. GTK-thread only. */
char *terminal_capture_tab_context_by_id(AppContext *app, const char *tab_id);
/* Capture terminal output not yet moved into the TEE accumulator. GTK-thread only. */
gboolean terminal_capture_pending_tee(AppContext *app);
/* Name ("pts/N") of the focused tab; used to scope TEE context to that tab. */
const char *terminal_active_tab_name(AppContext *app);

#endif

