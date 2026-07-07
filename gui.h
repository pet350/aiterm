// Part of the aiterm project
// gui.h
// C header file for gui functions
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
#include "ratelimit.h"

#define APP_NAME    "aiterm"

typedef enum {
    PROVIDER_KIND_OPENAI_CHAT,
    PROVIDER_KIND_GEMINI_GENERATE
} ProviderKind;

typedef struct {
    char *name;
    char *model;
    char *api_key;
    char *base_url;
    char *endpoint;
    char *auth_header;
    char *auth_scheme;
    char *query_key_name;
    ProviderKind kind;
    gboolean api_key_in_query;
} ProviderConfig;

// SessionContext structure for session management
typedef struct {
    char *session_uuid;

    GString *history_cache;
    GString *description;
    GString tagged_buffer;

    gboolean is_seeded;
    gboolean write_to_global;  // TRUE: writes history to GLOBAL 0000... UUID
    gboolean read_from_global; // TRUE: AI sees the 0000... UUID stream
    gboolean cfg_loaded_write_to_global;
    gboolean cfg_loaded_read_from_global;

    time_t last_sync;
    int last_sent_db_id;
} SessionContext;

typedef struct {
    GtkWidget *window;
    GtkWidget *vterm;

    // Unified Toggle Menu Item Pointers
    GtkWidget *toggle_auto_all;
    GtkWidget *toggle_autoreply;
    GtkWidget *toggle_autoexe;
    GtkWidget *toggle_tee;
    GtkWidget *toggle_noise_filter;
    GtkWidget *toggle_smart_cache;
    GtkWidget *toggle_ratelimit;
} UIComponents;

// AppContext the backbone of this entire application used by almost all functions
typedef struct {
    // Adden 0.7.4-delta
    MYSQL *global_db_conn;
    pthread_mutex_t db_mutex;
    pthread_mutex_t session_mutex;
    pthread_mutex_t db_init_mutex;
    pthread_cond_t db_init_cond;

    GtkListStore *session_list_store;
    UIComponents ui;
    RateLimiter limiter;
    SessionContext session;
    ProviderConfig provider_config;

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
    gboolean smart_cache_enabled;
    gboolean noise_filter_enabled;
    gboolean mysql_busy;
    gboolean ai_busy;
    gboolean is_processing;
    gboolean db_initialized;

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

    int sequence_id;
    int silence_ticks;

    double transparency;
    double ai_transparency;

    long last_row;
    long last_col;
    long last_processed_row;
    long last_sent_db_id;
}  AppContext;

// AIThreadData threaded sending data backbone
typedef struct {
    AppContext *app;
    char *prompt;
    char *terminal_context;
    long last_sent_db_id; // Added here for thread tracking
} AIThreadData;

// AIResponseData Threaded response data backbone
typedef struct {
    AppContext *app;
    char *response_text;
    char *original_prompt;
    long last_sent_db_id; // Added here for thread tracking
} AIResponseData;

// Function prototypes
void setup_gui(AppContext *app);
void append_ai_text(AppContext *app, const char *text, const char *tag_name);
void apply_custom_theme();
void apply_block_cursor_to_input(GtkWidget *entry);
void set_icon(AppContext *app);
void on_upload_clicked(GtkButton *button, gpointer data);
void on_copy_clicked(GtkButton *button, gpointer data);
void on_buffer_changed_scroll(GtkTextBuffer *buffer, gpointer data);

gboolean scroll_ai_pane_to_bottom(AppContext *app);
gboolean scroll_to_bottom_idle(gpointer data);

#endif

