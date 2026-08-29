// Part of the aiterm project
// gui.h
// Header file for gui functions
// And home of AppContext!
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

// define some static variables
#define APP_NAME	"aiterm"
#define MAX_TABS	32
#define MAX_DLG 	16
#define MAX_SNMP_HOSTS	128

#define AITERM_WM_CLASS "aiterm"
#define AITERM_WM_ROLE  "terminal"

// Added 0.9.7-Delta
// this is where we start ai snmp monitoring!!
// I do believe this is a briliant idea!
// Individual OID / Target Definition
typedef struct SnmpMetric {
    int id;		      // Primary Key ID from snmp_targets table
    char label[64];           // e.g., "Edge-Router CPU Usage"
    char ip_address[64];      // Target IP / Hostname
    char community[64];       // SNMP v2c Community String (e.g., "public")
    char oid_str[128];        // Numerical OID string (e.g., "1.3.6.1.4.1.2021.10.1.3.1")
    char last_value[256];     // Last polled string representation
    gboolean is_active;       // Individual target toggle
    gboolean alert_flag;      // Set TRUE if value crosses threshold
    time_t last_updated;      // Unix timestamp of last successful poll
} SnmpHost;

// Master Subsystem Context
typedef struct SnmpContext {
    GList *metrics;              // Double-linked list of SnmpMetric pointers
    pthread_mutex_t lock;        // Mutex protecting metrics list & last_value reads
    pthread_t poller_thread;     // Background poller thread reference
    gboolean loop_running;       // Thread loop controller flag
    gboolean initialized;        // SNMP mutex/condition initialization complete
    pthread_cond_t poller_cond;  // Wakes poller immediately during shutdown/config changes
    gboolean enable_gemini_feed; // Master toggle to attach SNMP telemetry to prompts
    gboolean force_gemini_feed;  // Force SNMP feed to gemini
    int poll_interval_sec;       // Global polling cycle interval (default: 10s)
    
    // Quick telemetry stats
    guint total_targets;
    guint active_alerts;

    char *payload;
} SnmpData;


//added 0.9.6-lambda
typedef struct {
    GtkWidget *dialog;
    GtkWidget *check_policy;
    GtkWidget *combo_action;
    char *command_text;
    gboolean active;
    int target_pane_id;
    int slot_id;
} exe_dlg;

typedef struct {
    struct ifaddrs *ifaddr;
    struct ifaddrs *ifa;
} NetworkConfig;

//Added 0.9.6-omega
/* Sub-structure dedicated to AI Retry configuration */
typedef struct {
    gboolean is_enabled;
    int max_retries;
    int delay_sec;
} AIRetryConfig;

// Added 0.9.6-omega
/* Sub-structure dedicated to AI Retry runtime state & metrics */
typedef struct {
    AIRetryConfig config;
    guint64 total_retries_executed;
} AIRetryState;

// Structure to govern opening and closing o tabs
// Added 0.9.6
typedef struct {
    // Identity & DB Sync
    char *session_uuid;          // Unique session ID for this tab
    char *custom_title;          // Label title on the GTK notebook tab
    char *model_override;        // Per-tab AI model selection (NULL = use global default)

    // Session-specific booleans (Tab-level state)
    gboolean is_active;
    gboolean close_tab_button_enabled;
    gboolean load_from_session;
    gboolean double_click_new_tab;
    gboolean enable_auto_all;
    gboolean enable_autoreply;
    gboolean enable_autoexe;
    gboolean enable_debug;
    gboolean enable_tee;
    gboolean enable_noise_filter;
    gboolean enable_smart_cache;
    gboolean enable_ratelimit;
    gboolean enable_session_write_global;
    gboolean enable_session_read_global;
    gboolean enable_xml_payload_tagging;
    gboolean enable_snmp_ticker;

    // UI Widgets bound to this specific tab
    GtkWidget *vte;              // VTE Terminal widget
    GtkWidget *tab_label_box;    // Container for label + close button
    GtkWidget *label;            // GTK Label widget
    GtkWidget *close_btn;        // GTK Close Button
    GtkWidget *box_container;    // Outer box for this tab's layout
    GtkWidget *search_bar;       // Per-tab search bar
    GtkWidget *search_entry;     // Per-tab search entry field
} TabSettings;

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
    GtkListStore *filters;              // GTK/UI representation, main thread only
    long count;
    GPtrArray *patterns;                // Thread-safe immutable snapshots for workers
    GMutex patterns_mutex;
} NoiseFilter;

// Structure of GtkWidgets for the window, the vterm, and all the toggles
typedef struct {
    GtkWidget *window;
    GtkWidget *vterm;
    GtkWidget *notebook;

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
    GtkWidget *toggle_session_config;
    GtkWidget *toggle_autoretry;
    GtkWidget *toggle_snmp_payload;
    GtkWidget *toggle_snmp_ticker;
} UIComponents;

// Structure containing all GtkWidgets of Manager Windows
// Added 0.9.4
typedef struct {
    GtkWidget *policy;
    GtkWidget *session;
    GtkWidget *noise;
    GtkWidget *history;
    GtkWidget *snmp;
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
    char *id;                  // Active "cachedContents/xxxx" handle
    char *name;                // Resource ID (e.g. "cachedContents/1234567890")
    char *system_instruction;  // System prompt pinned to this cache session
    time_t created_at;         // Local creation timestamp
    int ttl_seconds;           // Lifetime duration
    int cached_token_count;    // Tokens currently stored in cache
    int turn_count;            // Number of historical db rows frozen in this cache
    int min_token_floor;       // Threshold required before triggering cache
    long min_token_threshold;  // Minimum token count boundary
    gboolean is_valid;         // Active valid cache flag
} GeminiCacheState;


// Sub-structure for Export Configurations
// Added 0.9.7-alpha
typedef struct {
    char *console_export_path;
    char *ai_export_path;
    gboolean include_timestamps;
    gboolean format_as_json;
} ExportConfig;

// Sub-structure for Print Configurations
// Added 0.9.7-alpha
typedef struct {
    GtkPrintSettings *settings;
    GtkPageSetup *page_setup;
    gboolean print_selection_only;
} PrintConfig;

// Moved all system booleans to it's own structure
// Added 0.9.5-omega
typedef struct {
    gint ai_busy;
    gint is_processing;

    gboolean debug_mode;
    gboolean debug_mode_override;
    gboolean tee_enabled;
    gboolean autoreply_enabled;
    gboolean auto_execute_enabled;
    gboolean ratelimit_enabled;
    gboolean smart_cache_enabled;
    gboolean noise_filter_enabled;
    gboolean mysql_busy;
    gboolean db_initialized;
    gboolean is_initializing;
    gboolean xml_payload_tagging_enabled; // Added 0.9.7-alpha
    gboolean session_write_global;        // Added 0.9.7-alpha
    gboolean session_read_global;         // Added 0.9.7-alpha
    gboolean load_from_session; 	  // Added 0.9.7-alpha
    gboolean snmp_ticker_enabled;	  // Added 0.9.8-tau-5
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

    GtkWidget *snmp_ticker_label;
    char *snmp_ticker_text;
    gunichar *snmp_ticker_chars;   // Cached UTF-8 -> UCS-4 ticker text
    glong snmp_ticker_len;         // Cached Unicode character count
    size_t snmp_ticker_offset;
    guint snmp_ticker_timer_id;
    gboolean ai_scroll_pending;      // Coalesces redundant GTK scroll-idle callbacks
} SysWidgets;

// Structure for controlling what process has access to the database
// Added 0.9.5-omega
typedef struct {
    pthread_mutex_t db_mutex;
    pthread_mutex_t session_mutex;
    pthread_mutex_t db_init_mutex;
    pthread_cond_t db_init_cond;
    pthread_t db_init_thread;
    gboolean db_init_thread_started;
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
    int active_dialog_count;
    GQueue *cmd_queue;      
    gboolean is_command_running;       // Added 0.9.8-alpha IS the terminal busy
    gboolean ticker_completed;          // SNMP ticker: TRUE when full payload has completed one pass
    GQueue *pending_autoexec_queue;    // Added 0.9.8-alpha Store chained commands safely
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
    ResourceControl	access;				// Control over multi-threaded DB resources
    RunTimeVariables	aiterm_runtime;			// Misc runtime buffers and command queues
    SQL_DataBase	database;			// MySQL database connection and counters
    exe_dlg		exec_dialog[MAX_DLG];		// Embedded execution dialog array
    ExportConfig	export_opts;			// Data export settings
    GeminiCacheState	gemini_cache;			// Smart Cache state and token tracking
    SysWidgets		gui;				// Core system GUI widgets and theme providers
    RateLimiter		limiter;			// API rate-limiting structures
    LocalCommand	local;				// Shell command history cache
    ManagerWindows	manager;			// Handles for policy, session, noise, and history windows
    NetworkConfig	net;				// System interface network configuration
    NoiseFilter		noise;				// Active noise filter rules loaded from DB
    PrintConfig		print_opts;			// GTK printer configurations
    ProviderConfig	provider_config;		// AI backend API parameters (Gemini / OpenAI)
    AIRetryConfig	retry_config;			// Backoff/retry settings for API calls
    AIRetryState	retry_state;			// Runtime execution counters for API retries
    SecurityConfig	security;			// Encrypted keys and security master tokens
    SessionContext	session;			// Active UUID session parameters and log buffers
    SnmpData		SnmpContext;			// Master SNMP subsystem and thread controller
    SnmpHost 		SnmpMetric[MAX_SNMP_HOSTS];	// Array of target SNMP polling hosts
    SystemBooleans	sys;				// Global feature flags and system state booleans
    TabSettings		tabs[MAX_TABS];			// Notebook tab array and per-tab VTE instances
    TokenTracker	tokens;				// Active token consumption progress tracker
    UIComponents	ui;				// GTK menu item pointers and system toggles
    TagPayload		xml;				// XML tag wrapper configurations for AI feeds
} AppContext;

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
void on_notebook_double_click(GtkGestureMultiPress *gesture, gint n_press, gdouble x, gdouble y, gpointer user_data);
void on_tab_close_clicked(GtkButton *button, gpointer user_data);
void add_terminal_tab(AppContext *app);
void update_snmp_ticker_payload(AppContext *app, const char *payload_summary);

// Boolean function Prototypes
gboolean scroll_ai_pane_to_bottom(AppContext *app);
gboolean scroll_to_bottom_idle(gpointer data);
gboolean on_entry_key_press(GtkWidget *widget, GdkEventKey *event, gpointer user_data);
gboolean on_notebook_button_press(GtkWidget *widget, GdkEventButton *event, gpointer user_data);
gboolean update_snmp_ticker_scroll(gpointer user_data);

GtkWidget *create_snmp_ticker(AppContext *app);

#endif

