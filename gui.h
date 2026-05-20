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
    // Adden 0.7.4-delta
    MYSQL *global_db_conn;
    pthread_mutex_t db_mutex;

    GtkWidget *window;
    GtkWidget *notebook;
    GtkWidget *terminal_view;
    GtkWidget *gemini_view;
    GtkWidget *entry;
    GtkWidget *status_label;
    GtkWidget *tee_menu_item;
    GtkWidget *autoreply_menu_item;

    gboolean tee_enabled;
    gboolean autoreply_enabled;
    gboolean is_processing;

    GtkCssProvider *ai_css_provider;
    GString *tee_accumulator;
    GMutex buffer_mutex;

    char *api_key;
    char *provider;
    char *model;
    char *db_host;
    char *db_user;
    char *db_pass;
    char *db_name;
    char *session_uuid;
    char *terminal_font;
    char *ai_font;

    int sequence_id;
    int silence_ticks;

    double transparency;
    double ai_transparency;

    long last_row;
    long last_col;
    long last_processed_row;
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


