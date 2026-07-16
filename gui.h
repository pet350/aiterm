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

// enum of supported AI APIs
typedef enum {
    PROVIDER_KIND_OPENAI_CHAT,
    PROVIDER_KIND_GEMINI_GENERATE
} ProviderKind;

// enum of all different tag types
typedef enum {
    TAG_NONE,       // Don't wrap in XML
    TAG_HISTORY,    // For DB-loaded Assistant context
    TAG_MEMORY,     // For DB-loaded User history
    TAG_LOG_DUMP,   // For real-time Tee data
    TAG_SYSTEM,	    // Meta-data. warninggs, system state
    TAG_STATUS      // For UI labels/status bars (non-AI-fed)
} TagType;

// Configuration of AI Provider
typedef struct {
    char *name;
    char *model;
    char *api_key;
    char *base_url;
    char *endpoint;
    char *auth_header;
    char *auth_scheme;
    char *query_key_name;
    char *provider;
    ProviderKind kind;
    gboolean api_key_in_query;
} ProviderConfig;

// Structure for XML Tag Payloads
typedef struct {
    TagType type;
    char *database_timestamp;
    gboolean tagging_enabled;
} TagPayload;

// SessionContext structure for session management
typedef struct {
    char *session_uuid;
    GtkListStore *session_list_store;

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

// Local "Cache" storage for noise filtes stored in mysql
// Added 0.9.4
typedef struct {
    GtkListStore *filters;
    long count;
} NoiseFilter;

// Structure of GtkWidgets for the window, the vterm, and all the toggles
typedef struct {
    GtkWidget *window;
    GtkWidget *vterm;

    // Unified Toggle Menu Item Pointers
    GtkWidget *toggle_auto_all;
    GtkWidget *toggle_autoreply;
    GtkWidget *toggle_autoexe;
    GtkWidget *toggle_debug;
    GtkWidget *toggle_tee;
    GtkWidget *toggle_noise_filter;
    GtkWidget *toggle_smart_cache;
    GtkWidget *toggle_ratelimit;
    GtkWidget *toggle_session_write_global;
    GtkWidget *toggle_session_read_global;
    GtkWidget *toggle_xml_payload_tagging;
} UIComponents;

// Structure containing all GtkWidgets of Manager Windows
// Added 0.9.4
typedef struct {
    GtkWidget *policy;
    GtkWidget *session;
    GtkWidget *noise;
    GtkWidget *history;
} ManagerWindows;

// Structure containing Local Command History
// Added 0.9.4
typedef struct {
    GPtrArray *cmd_history;      // Dynamic string array from GLib
    int history_index;          // Current position in history cycle
    char *history_temp_entry;
} LocalCommand;

// Dedicated sub-structure for tracking API resource metrics
// Added 0.9.5
typedef struct {
    GtkWidget *bar;         // The actual GTK progress bar widget pointer
    long current;           // Accumulated session tokens currently active
    long max;               // Absolute ceiling boundary for the active model
    long last;              // Exact token weight consumed by the immediate last prompt
} TokenTracker;

// Dedicated sub-structure for Smart Cache
// Added 0.9.5-alpha
typedef struct {
    char *id;               // Tracks the active "cachedContents/xxxx" handle
    time_t created_at;      // Epoch timestamp tracking to handle TTL expiration
    int turn_count;         // Number of historical db rows safely frozen in this cache instance
    long min_token_threshold;
} GeminiCacheState;

// Moved all system booleans to it's own structure
// Added 0.9.5-omega
typedef struct {
    gboolean debug_mode;
    gboolean debug_mode_override;
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
} SystemBooleans;

// All main GUI related variable structure
// Added 0.9.5-omega
typedef struct {
    GtkCssProvider *ai_css_provider;


    GtkWidget *window;
    GtkWidget *notebook;
    GtkWidget *terminal_view;
    GtkWidget *gemini_view;
    GtkWidget *entry;
    GtkWidget *status_label;
    GtkWidget *tee_menu_item;
    GtkWidget *autoreply_menu_item;

    double transparency;
    double ai_transparency;

    char *terminal_font;
    char *ai_font;

} SysWidgets;

// Structure for controlling what process has access to the database
// Added 0.9.5-omega
typedef struct {
    pthread_mutex_t db_mutex;
    pthread_mutex_t session_mutex;
    pthread_mutex_t db_init_mutex;
    pthread_cond_t db_init_cond;
    GMutex buffer_mutex;

} ResourceControl;

// Global MySQL Database variables
// Added 0.9.5-omega
typedef struct {
    MYSQL *global_db_conn;

    char *db_host;
    char *db_user;
    char *db_pass;
    char *db_name;

    long last_row;
    long last_col;
    long last_processed_row;
    long last_sent_db_id;

    int sequence_id;
    int silence_ticks;

} SQL_DataBase;

// Misc runtime variables
// Added 0.9.5-omega
typedef struct {
    GString *tee_accumulator;
    char *untagged_text;
    char *cache;
    char *model;
} RunTimeVariables;

// Security configuration
// Added 0.9.5-omega
typedef struct {
    char *master_key;
    char *api_key;
} SecurityConfig;

// AppContext the backbone of this entire application used by almost all functions
// Constantly being updated
// Completely modularized AppContext: 0.9.5-omega
// Gemini stated: `AppContext` root now exclusively acts as a "Table of Contents" for your sub-systems.
typedef struct {
    // Global DB Connection - Adden 0.7.4-delta
    SQL_DataBase database;		 // Sub-Structure for mysql database access
    RunTimeVariables aiterm_runtime;     // Sub-Structure for misc runtime variables
    ResourceControl access;		 // Sub-Structure for control over resources
    SystemBooleans sys;			 // Sub-Structure for all system control booleans
    UIComponents ui;			 // Sub-Structure for GtkWidgets of all UI components
    RateLimiter limiter;                 // Sub-Structure for Rate Limiter variables
    NoiseFilter noise;	 		 // Sub-Structure for all Noise filter variables
    SessionContext session;		 // Sub-Structure for the current sessions variables
    ProviderConfig provider_config;	 // Sub-Structure for AI Provider config variables
    ManagerWindows manager;		 // Sub-Structure for GtkWidgest of all the manager windows
    LocalCommand local;	  		 // Sub-Structure for local commands being cached. (up/down arrow call back)
    TokenTracker tokens;		 // Sub-Structure for keeping track of AI Tokens
    GeminiCacheState gemini_cache;	 // Sub-Structure for Smart Cache variables
    TagPayload xml;			 // Sub-Structure for handling xml taggs
    SysWidgets gui;			 // Sub-Structure for handling system GUI Widgets
    SecurityConfig security;		 // Sub-Structure for security keys

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
void init_local_cmd_history(AppContext *app);
void init_token_tracker(AppContext *app);
void setup_gui(AppContext *app);
void append_ai_text(AppContext *app, const char *text, const char *tag_name);
void apply_custom_theme();
void apply_block_cursor_to_input(GtkWidget *entry);
void set_icon(AppContext *app);
void on_upload_clicked(GtkButton *button, gpointer data);
void on_copy_clicked(GtkButton *button, gpointer data);
void on_buffer_changed_scroll(GtkTextBuffer *buffer, gpointer data);

// Boolean function Prototypes
gboolean scroll_ai_pane_to_bottom(AppContext *app);
gboolean scroll_to_bottom_idle(gpointer data);
gboolean on_entry_key_press(GtkWidget *widget, GdkEventKey *event, gpointer user_data);

#endif

