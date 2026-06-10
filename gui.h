// Part of the aiterm project
// gui.h
// C header file for gui functions
// By: Peter Talbott
// With assistance from Gemini and OpenAI
// May 2026

#ifndef GUI_H
#define GUI_H

#include <gtk/gtk.h>
#include <mariadb/mysql.h>
#include <pthread.h>
#include "ratelimit.h"

#define APP_NAME    "aiterm"

// SessionContext structure for session management
typedef struct {
    char session_uuid[37];
    GString *history_cache;
    GString *description;
    GString tagged_buffer;
    time_t last_sync;
} SessionContext;

// AppContext the backbone of this entire application
typedef struct AppContext {
    // Adden 0.7.4-delta
    MYSQL *global_db_conn;
    pthread_mutex_t db_mutex;
    RateLimiter limiter;
    SessionContext session;

    GtkWidget *window;
    GtkWidget *notebook;
    GtkWidget *terminal_view;
    GtkWidget *gemini_view;
    GtkWidget *entry;
    GtkWidget *status_label;
    GtkWidget *tee_menu_item;
    GtkWidget *autoreply_menu_item;

    gboolean debug_mode;
    gboolean tee_enabled;
    gboolean autoreply_enabled;
    gboolean auto_execute_enabled;
    gboolean ratelimit_enabled;
    gboolean is_processing;

    GtkCssProvider *ai_css_provider;
    GString *tee_accumulator;
    GMutex buffer_mutex;

    char *master_key;
    char *api_key;
    char *provider;
    char *model;
    char *db_host;
    char *db_user;
    char *db_pass;
    char *db_name;
    char *terminal_font;
    char *ai_font;
    char *untagged_text;
    char *cache;
    char *session_uuid;

    int sequence_id;
    int silence_ticks;

    double transparency;
    double ai_transparency;

    long last_row;
    long last_col;
    long last_processed_row;
}AppContext;

// AIThreadData threaded sending data backbone
typedef struct {
    AppContext *app;
    char *prompt;
    char *terminal_context;
} AIThreadData;

// AIResponseData Threaded response data backbone
typedef struct {
    AppContext *app;
    char *response_text;
    char *original_prompt;
} AIResponseData;

// Function prototypes
void setup_gui(AppContext *app);
void append_ai_text(AppContext *app, const char *text, const char *tag_name);
void apply_block_cursor_to_input(GtkWidget *entry);
void set_icon(AppContext *app);
void on_upload_clicked(GtkButton *button, gpointer data);
void on_copy_clicked(GtkButton *button, gpointer data);

gboolean scroll_ai_pane_to_bottom(AppContext *app);

#endif
