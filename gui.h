/* gui.h
* C header file for gui functions
* By: Peter Talbott
* With assistance from Gemini and OpenAI
* May 2026
*/

#ifndef GUI_H
#define GUI_H

#include <gtk/gtk.h>
#include <mariadb/mysql.h>

#define APP_NAME    "aiterm"

typedef struct AppContext {
    GtkWidget *window;
    GtkWidget *terminal_view;
    GtkWidget *gemini_view;
    GtkWidget *entry;
    GtkWidget *status_label;

    GtkCssProvider *ai_css_provider;

    GString *tee_accumulator; // Use GLib's dynamic string
    GMutex buffer_mutex;      // Ensure thread safety between VTE and AI thread
    char *api_key;
    char *provider; // "openai" or "gemini"
    char *model;    // e.g., "gpt-4o" or "gemini-1.5-flash"

    char *db_host;
    char *db_user;
    char *db_pass;
    char *db_name;
    char *session_uuid;

    char *terminal_font; // e.g., "Monospace 10"
    char *ai_font;       // e.g., "Sans 10"

    int sequence_id;
    double transparency; 	// Console transparencyValue from 0.0 (transparent) to 1.0 (opaque)  -- Added 0.7.1-beta
    double ai_transparency; // Added for separate AI pane control

    // Adden 0.7.4-delta
    MYSQL *global_db_conn;
    pthread_mutex_t db_mutex;
    gboolean autoreply_enabled;
    gboolean is_processing; // Guard for double-triggers 0.7.5-beta
}AppContext;

/* gui.h */

typedef struct {
    AppContext *app;
    char *prompt;
    char *terminal_context;
} AIThreadData;


typedef struct {
    AppContext *app;
    char *response_text;
    char *original_prompt; // Required for ai_thread_func in gemini.c
} AIResponseData;

void setup_gui(AppContext *app);
void append_ai_text(AppContext *app, const char *text, const char *tag_name);
void apply_block_cursor_to_input(GtkWidget *entry);
void set_icon(AppContext *app);
void on_upload_clicked(GtkButton *button, gpointer data);
void on_copy_clicked(GtkButton *button, gpointer data);

#endif


