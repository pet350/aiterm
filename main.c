// Main program file for aiterm	project
// The terminal emulator with AI assistance
// By: Peter Talbott
// Assisted compilation from Gemini and OpenAI 
// April 2026 - August 2026

#include <stdio.h>
#include <stdlib.h>
#include <gtk/gtk.h>
#include <pthread.h>
#include <time.h>
#include <getopt.h>
#include <unistd.h>
#include <mariadb/mysql.h>
#include <net-snmp/net-snmp-config.h>
#include <net-snmp/net-snmp-includes.h>

#include "main.h"
#include "gui.h"
#include "update.h"
#include "utils.h"
#include "tee_handler.h"
#include "crypto.h"
#include "help.h"
#include "gemini.h"
#include "gemini_cache.h"
#include "build_id.h"
#include "session_manager.h"
#include "config.h"
#include "noisefilter.h"
#include "commands.h"
#include "ai_retry.h"
#include "snmp_manager.h"
#include "menu.h"

AppContext *global_app = NULL;

int main(int argc, char *argv[]) {
    AppContext *app = g_malloc0(sizeof(AppContext));
    global_app = app;

    // 1. Set initial variables to their needed defaults
    initialize_booleans(app);

    // 2. Parse command line options if any
    parse_command_line_options(app, argc, argv);

    // 2.1 Set Config filename
    init_config_pointer();

    // 3. Initialize GTK
    DEBUG_PRINT("[DEBUG]: [MAIN] Initializing GTK...\n");
    gtk_init(&argc, &argv);
    g_object_set(gtk_settings_get_default(), "gtk-application-prefer-dark-theme", TRUE, NULL);

    // 3.1: Initialize AI Retry
    ai_retry_init(app);

    // 3.2: Initilize Runtime Queues
    init_runtime_queues(app);

    // 4 Initialize App Context and load config
    DEBUG_PRINT("[DEBUG]: [MAIN] Invoking load_config... \n");
    load_config(app);
    DEBUG_PRINT("[DEBUG]: [MAIN] Done! load_config sequence is now complete.\n");

    // 5) Initialize all DB synchronization primitives BEFORE any worker can use them.
    pthread_mutex_init(&app->access.db_mutex, NULL);
    pthread_mutex_init(&app->access.db_init_mutex, NULL);
    pthread_cond_init(&app->access.db_init_cond, NULL);
    app->sys.db_initialized = FALSE;
    app->access.db_init_thread_started = FALSE;

    DEBUG_PRINT("[DEBUG]: [MAIN] Spawning asynchronous DB initialization thread...\n");
    if (pthread_create(&app->access.db_init_thread, NULL, init_db_thread_worker, app) == 0) {
        app->access.db_init_thread_started = TRUE;
    } else {
        fprintf(stderr, "Error: Failed to spawn database initialization thread.\n");
        /* Prevent session_init() from waiting forever when creation fails. */
        pthread_mutex_lock(&app->access.db_init_mutex);
        app->sys.db_initialized = TRUE;
        pthread_cond_broadcast(&app->access.db_init_cond);
        pthread_mutex_unlock(&app->access.db_init_mutex);
    }

    // 6) Initialize Session Manager
    DEBUG_PRINT("[DEBUG]: [MAIN] Initializing Session Manager...\n");
    session_init(app);
    DEBUG_PRINT("[DEBUG]: [MAIN] Done! Session Manager is now active.\n");

    // 7) INITIALIZE THE TEE HANDLER HERE
    DEBUG_PRINT("[DEBUG]: [MAIN] Initializing Tee Handler...\n");
    tee_handler_init(app);
    DEBUG_PRINT("[DEBUG]: [MAIN] Done! Tee Handler Initialized\n");

    // 8) Initialize Noise Filter
    if (app->sys.db_initialized) {
        DEBUG_PRINT("[DEBUG]: [Noise Filter]: Initializing List...\n");
        noise_filter_load_from_db(app);
        DEBUG_PRINT("[DEBUG]: [Noise Filter]: Initializing Done!\n");
    }

    // 9) Initialize Token Tracker
    // Added 0.9.5
    DEBUG_PRINT("[DEBUG]: [MAIN] Initializing Token Tracker...\n");
    init_token_tracker(app);
    DEBUG_PRINT("[DEBUG]: [Token Tracker] Initalizing Done!\n");

    // 10) FALLBACK: Only check env vars if config key is still NULL
    if (!app->security.api_key || strlen(app->security.api_key) == 0) {
        app->security.api_key = getenv("GEMINI_API_KEY");
    }

    if (!app->security.api_key || strlen(app->security.api_key) == 0) {
        app->security.api_key = getenv("OPENAI_API_KEY");
    }

    if (!app->security.api_key) {
        fprintf(stderr, "Error: No API key found.\n");
    }

    // 11) initialize AI Provider config
    DEBUG_PRINT("[DEBUG]: [MAIN] Initialize AI Provider Configuration...\n");
    init_provider_config(app);
    DEBUG_PRINT("[DEBUG]: [AI Provider] Initialization Done!\n");

    // 12) initialize rate limiter
    DEBUG_PRINT("[DEBUG]: [MAIN] Initialize Rate Limiter...\n");
    ratelimit_init(&app->limiter, app->limiter.requests_per_minute);
    DEBUG_PRINT("[DEBUG]: [Rate Limiter] Initialization Done!\n");

    // 13) Initialize local command cache
    // Added 0.9.5
    DEBUG_PRINT("[DEBUG]: [MAIN] Initialize Local Command History Cache.\n");
    init_local_cmd_history(app);
    DEBUG_PRINT("[DEBUG]: [Local Command] Initialization Done! Use Up/Down Arrow keys to activate\n");

    // 14) Initialize Smart Cache
    // Added 0.9.5-omega
    DEBUG_PRINT("[DEBUG]: [MAIN] Initialize smart cache variables\n");
    gemini_cache_init(app);
    DEBUG_PRINT("[DEBUG]: [Smart Cache] Done initalizing\n");

    // 15) Build the UI (from gui.c)
    // Revised 0.9.2, 0.9.3, 0.9.4 and 0.9.5
    DEBUG_PRINT("[DEBUG]: [MAIN] Launching create_main_window GUI setup...\n");
    setup_gui(app);
    DEBUG_PRINT("[DEBUG]: [GUI Setup] Done!\n");

    // 16) Send general direcives Added 0.9.6-gamma
    DEBUG_PRINT("[DEBUG]: [Main] Sending General Directives\n");
    g_idle_add(on_app_startup_prime, app);

    // 17) Initialize SNMP Subsystem
    init_snmp_subsystem(app);
    init_snmp("aiterm");
    snmp_load_targets_from_db(app);
    snmp_start_poller(app);

    // 17.1) Sync all Booleans
    sync_toggle_ui_elements(app);

    // 18) Enter the GTK Main Event Loop
    DEBUG_PRINT("[DEBUG]: [MAIN] Passing control to gtk_main loop.\n");
    gtk_main();

    // 19) Clean up
    DEBUG_PRINT("[DEBUG]: [MAIN] Beginning orderly shutdown.\n");

    /* Stop and join the SNMP worker before destroying AppContext resources. */
    snmp_stop_poller(app);

    /* The DB initialization worker owns no AppContext lifetime.  Join it so
     * shutdown can never race a still-running database initializer. */
    if (app->access.db_init_thread_started) {
        DEBUG_PRINT("[DEBUG]: [MAIN] Joining DB initialization thread.\n");
        pthread_join(app->access.db_init_thread, NULL);
        app->access.db_init_thread_started = FALSE;
    }

    session_sync_booleans_to_db(app);

    if (app->database.global_db_conn) {
        mysql_close(app->database.global_db_conn);
        app->database.global_db_conn = NULL;
    }

    // 20) Close main threaded database connection
    DEBUG_PRINT("[DEBUG]: [MAIN] Closing threaded database connection.\n");
    pthread_cond_destroy(&app->access.db_init_cond);
    pthread_mutex_destroy(&app->access.db_init_mutex);
    pthread_mutex_destroy(&app->access.db_mutex);

    // 21) free master key from memory
    if (app->security.master_key) {
        // Overwrite memory with zeros before freeing
        size_t len = strlen(app->security.master_key);
        memset(app->security.master_key, 0, len);
        free(app->security.master_key);
    }

    // 22) free provider config
    free_provider_config(&app->provider_config);

    /* Release GTK-owned ticker resources before destroying AppContext. */
    if (app->gui.snmp_ticker_timer_id) {
        g_source_remove(app->gui.snmp_ticker_timer_id);
        app->gui.snmp_ticker_timer_id = 0;
    }
    g_free(app->gui.snmp_ticker_text);
    g_free(app->gui.snmp_ticker_chars);
    app->gui.snmp_ticker_text = NULL;
    app->gui.snmp_ticker_chars = NULL;
    app->gui.snmp_ticker_len = 0;

    g_free(app);
    return 0;
}

// Thats All Folks! LOL!
// Latter!
