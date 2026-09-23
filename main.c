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
    // Initialize structure AppContext
    AppContext *app = g_malloc0(sizeof(AppContext));
    global_app = app;

    // 1.1: Set initial variables to their needed defaults
    initialize_booleans(app);

    // 1.2: Check Environment Variables
    // Added 0.9.9-beta
    char *env_color = getenv("AITERM_COLOR");
    if (env_color) {
        app->sys.debug_color = atoi(env_color);
    }

    char *env_debug = getenv("AITERM_DEBUG");
    if (env_debug) {
        app->sys.debug_mode = atoi(env_debug);
    }

    // 1.3: Initialize Color Variabled
    init_colors(app);

    // 1.4 Check if root is calling the app
    check_for_root();

    // 2.0: Parse command line options if any
    parse_command_line_options(app, argc, argv);

    // 2.1: Set Config filename
    init_config_pointer(app);

    // 2.2: Check Network availability
    app->sys.is_network_available = check_network_availability(app);
    if(!app->sys.is_network_available && !app->sys.offline_override) {
       DEBUG_PRINT("[ DEBUG ]: [Network] Unavailable.\n");
       exit(1);
    }

    // 2.3: Check if our STDERR is outputting to a TTY or not
    check_debug_tty(app);

    // 2.4: Initialize ANSI Colors
    init_colors(app);
 
    // 3.0: Initialize GTK
    DEBUG_PRINT("[%s DEBUG %s]: [%sMAIN%s] %sInitializing GTK...%s\n",
	app->ansi.lt_purple, app->ansi.normal, app->ansi.yellow, app->ansi.normal,
	app->ansi.lt_purple, app->ansi.normal);
    gtk_init(&argc, &argv);
    g_object_set(gtk_settings_get_default(), "gtk-application-prefer-dark-theme", TRUE, NULL);

    // 3.1: Initialize AI Retry
    ai_retry_init(app);

    // 3.2: Initilize Runtime Queues
    init_runtime_queues(app);

    // 4.0: Initialize App Context and load config
    DEBUG_PRINT("[%s DEBUG %s]: [%sMAIN%s] %sInvoking load_config...%s \n",
        app->ansi.lt_purple, app->ansi.normal, app->ansi.yellow, app->ansi.normal,
        app->ansi.lt_purple, app->ansi.normal);
    load_config(app);
    DEBUG_PRINT("[%s DEBUG %s]: [%sMAIN%s] %sDone! load_config sequence is now complete.%s\n",
        app->ansi.lt_purple, app->ansi.normal, app->ansi.yellow, app->ansi.normal,
        app->ansi.lt_purple, app->ansi.normal);

    // 5.0: Initialize all DB synchronization primitives BEFORE any worker can use them.
    pthread_mutex_init(&app->access.db_mutex, NULL);
    pthread_mutex_init(&app->access.db_init_mutex, NULL);
    pthread_cond_init(&app->access.db_init_cond, NULL);
    app->sys.db_initialized = FALSE;
    app->access.db_init_thread_started = FALSE;

    // 5.1: Start Database worker thread
    DEBUG_PRINT("[%s DEBUG %s]: [%sMAIN%s]%s Spawning asynchronous DB initialization thread...%s\n",
        app->ansi.lt_purple, app->ansi.normal, app->ansi.yellow, app->ansi.normal,
        app->ansi.lt_purple, app->ansi.normal);
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

    // 6.0: Initialize Session Manager
    DEBUG_PRINT("[%s DEBUG %s]: [%sMAIN%s]%s Initializing Session Manager...%s\n",
        app->ansi.lt_purple, app->ansi.normal, app->ansi.yellow, app->ansi.normal,
        app->ansi.lt_purple, app->ansi.normal);
    session_init(app);
    DEBUG_PRINT("[%s DEBUG %s]: [%sMAIN%s]%s Done! Session Manager is now active.%s\n",
        app->ansi.lt_purple, app->ansi.normal, app->ansi.yellow, app->ansi.normal,
        app->ansi.lt_purple, app->ansi.normal);

    // 7.0: INITIALIZE THE TEE HANDLER HERE
    DEBUG_PRINT("[%s DEBUG %s]: [%sMAIN%s]%s Initializing Tee Handler...%s\n",
        app->ansi.lt_purple, app->ansi.normal, app->ansi.yellow, app->ansi.normal,
        app->ansi.lt_purple, app->ansi.normal);
    tee_handler_init(app);
    DEBUG_PRINT("[%s DEBUG %s]: [%sMAIN%s]%s Done! Tee Handler Initialized%s\n",
        app->ansi.lt_purple, app->ansi.normal, app->ansi.yellow, app->ansi.normal,
        app->ansi.lt_purple, app->ansi.normal);

    // 8.0: Initialize Noise Filter
    if (app->sys.db_initialized) {
        DEBUG_PRINT("[%s DEBUG %s]: [%sNoise Filter%s]:%s Initializing List...%s\n",
        app->ansi.lt_purple, app->ansi.normal, app->ansi.yellow, app->ansi.normal,
        app->ansi.lt_purple, app->ansi.normal);
        noise_filter_load_from_db(app);
        DEBUG_PRINT("[%s DEBUG %s]: [%sNoise Filter%s]:%s Initializing Done!%s\n",
        app->ansi.lt_purple, app->ansi.normal, app->ansi.yellow, app->ansi.normal,
        app->ansi.lt_purple, app->ansi.normal);
    }

    // 9.0: Initialize Token Tracker
    // Added 0.9.5
    DEBUG_PRINT("[%s DEBUG %s]: [%sMAIN%s]%s Initializing Token Tracker...%s\n",
        app->ansi.lt_purple, app->ansi.normal, app->ansi.yellow, app->ansi.normal,
        app->ansi.lt_purple, app->ansi.normal);
    init_token_tracker(app);
    DEBUG_PRINT("[%s DEBUG %s]: [%sToken Tracker%s]%s Initalizing Done!%s\n",
        app->ansi.lt_purple, app->ansi.normal, app->ansi.yellow, app->ansi.normal,
        app->ansi.lt_purple, app->ansi.normal);

    /* 0.9.11-alpha: provider-specific credentials are resolved by the
     * Provider Manager/configuration layer.  Do not fall back from one
     * provider's credential to another provider's credential. */

    // 11.0: initialize AI Provider config
    DEBUG_PRINT("[%s DEBUG %s]: [%sMAIN%s]%s Initialize AI Provider Configuration...%s\n",
        app->ansi.lt_purple, app->ansi.normal, app->ansi.yellow, app->ansi.normal,
        app->ansi.lt_purple, app->ansi.normal);
    init_provider_config(app);
    DEBUG_PRINT("[%s DEBUG %s]: [%sAI Provider%s]%s Initialization Done!%s\n",
        app->ansi.lt_purple, app->ansi.normal, app->ansi.yellow, app->ansi.normal,
        app->ansi.lt_purple, app->ansi.normal);

    if (!app->provider_config.api_key || strlen(app->provider_config.api_key) == 0) {
        DEBUG_PRINT("[ DEBUG ]: No API key configured for active provider [%s].\n",
                    app->provider_config.provider ? app->provider_config.provider : "unknown");
    }

    // 12.0: initialize rate limiter
    DEBUG_PRINT("[%s DEBUG %s]: [%sMAIN%s]%s Initialize Rate Limiter...%s\n",
        app->ansi.lt_purple, app->ansi.normal, app->ansi.yellow, app->ansi.normal,
        app->ansi.lt_purple, app->ansi.normal);
    ratelimit_init(&app->limiter, app->limiter.requests_per_minute);
    DEBUG_PRINT("[%s DEBUG %s]: [%sRate Limiter%s]%s Initialization Done!%s\n",
        app->ansi.lt_purple, app->ansi.normal, app->ansi.yellow, app->ansi.normal,
        app->ansi.lt_purple, app->ansi.normal);

    // 13.0: Initialize local command cache
    // Added 0.9.5
    DEBUG_PRINT("[%s DEBUG %s]: [%sMAIN%s]%s Initialize Local Command History Cache.%s\n",
        app->ansi.lt_purple, app->ansi.normal, app->ansi.yellow, app->ansi.normal,
        app->ansi.lt_purple, app->ansi.normal);
    init_local_cmd_history(app);
    DEBUG_PRINT("[%s DEBUG %s]: [%sLocal Command%s]%s Initialization Done! Use Up/Down Arrow keys to activate%s\n",
        app->ansi.lt_purple, app->ansi.normal, app->ansi.yellow, app->ansi.normal,
        app->ansi.lt_purple, app->ansi.normal);

    // 14.0: Initialize Smart Cache
    // Added 0.9.5-omega
    DEBUG_PRINT("[%s DEBUG %s]: [%sMAIN%s]%s Initialize smart cache variables%s\n",
        app->ansi.lt_purple, app->ansi.normal, app->ansi.yellow, app->ansi.normal,
        app->ansi.lt_purple, app->ansi.normal);
    gemini_cache_init(app);
    DEBUG_PRINT("[%s DEBUG %s]: [%sSmart Cach%s]%s Done initalizing%s\n",
        app->ansi.lt_purple, app->ansi.normal, app->ansi.yellow, app->ansi.normal,
        app->ansi.lt_purple, app->ansi.normal);

    // 15.0: Build the UI (from gui.c)
    // Revised 0.9.2, 0.9.3, 0.9.4 and 0.9.5
    DEBUG_PRINT("[%s DEBUG %s]: [%sMAIN%s]%s Launching create_main_window GUI setup...%s\n",
        app->ansi.lt_purple, app->ansi.normal, app->ansi.yellow, app->ansi.normal,
        app->ansi.lt_purple, app->ansi.normal);

    setup_gui(app);
    DEBUG_PRINT("[%s DEBUG %s]: [%sGUI Setup%s]%s Done!%s\n",
        app->ansi.lt_purple, app->ansi.normal, app->ansi.yellow, app->ansi.normal,
        app->ansi.lt_purple, app->ansi.normal);

    // 15.1: Start the idle watchdog after the GUI exists.  The timeout is
    // loaded from aiterm.conf when present; otherwise idle.c defaults to 10
    // minutes.
    idle_init(app);


    // 16.0: Send general direcives Added 0.9.6-gamma
    DEBUG_PRINT("[%s DEBUG %s]: [%sMain%s] %sSending General Directives%s\n",
        app->ansi.lt_purple, app->ansi.normal, app->ansi.yellow, app->ansi.normal,
        app->ansi.lt_purple, app->ansi.normal);

    g_idle_add(on_app_startup_prime, app);

    // 17.0: Initialize SNMP Subsystem
    init_snmp_subsystem(app);
    init_snmp("aiterm");
    snmp_load_targets_from_db(app);
    snmp_start_poller(app);

    // 17.1: Sync all Booleans
    sync_toggle_ui_elements(app);

    // 18.0: Enter the GTK Main Event Loop
    DEBUG_PRINT("[%s DEBUG %s]: [%sMAIN%s]%s Passing control to gtk_main loop.%s\n",
        app->ansi.lt_purple, app->ansi.normal, app->ansi.yellow, app->ansi.normal,
        app->ansi.lt_purple, app->ansi.normal);

    gtk_main();

    // 19.0: Clean up
    DEBUG_PRINT("[ %sDEBUG%s ]: [%sMAIN%s] %sBeginning orderly shutdown.%s\n",
        app->ansi.lt_purple, app->ansi.normal, app->ansi.yellow, app->ansi.normal,
        app->ansi.lt_purple, app->ansi.normal);

    // Restore any temporarily suspended toggle states before the final DB
    // synchronization so an idle period is never persisted as a real user
    // choice.
    idle_shutdown(app);
    
    // 19.1: Stop and join the SNMP worker before destroying AppContext resources. */
    snmp_stop_poller(app);

    // 19.2: The DB initialization worker owns no AppContext lifetime.  Join it so
    // shutdown can never race a still-running database initializer.
    if (app->access.db_init_thread_started) {
        DEBUG_PRINT("[ %sDEBUG%s ]: [%sMAIN%s] %sJoining DB initialization thread.%s\n",
        app->ansi.lt_purple, app->ansi.normal, app->ansi.yellow, app->ansi.normal,
        app->ansi.lt_purple, app->ansi.normal);

        pthread_join(app->access.db_init_thread, NULL);
        app->access.db_init_thread_started = FALSE;
    }

    // 19.3: Sync system booleans to the database
    session_sync_booleans_to_db(app);

    if (app->database.global_db_conn) {
        mysql_close(app->database.global_db_conn);
        app->database.global_db_conn = NULL;
    }

    // 20: Close main threaded database connection
    DEBUG_PRINT("[ %sDEBUG%s ]: [%sMAIN%s] %sClosing threaded database connection.%s\n",
        app->ansi.lt_purple, app->ansi.normal, app->ansi.yellow, app->ansi.normal,
        app->ansi.lt_purple, app->ansi.normal);

    // 20.1: Clean up ANSI
    cleanup_colors(app);

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

    // 22) free provider config and explicit provider credentials
    free_provider_config(&app->provider_config);
    g_free(app->security.openai_key);
    g_free(app->security.gemini_key);
    g_free(app->security.groq_key);
    g_free(app->security.openrouter_key);
    g_free(app->security.mistral_key);
    g_free(app->security.ollama_key);
    g_free(app->security.custom_key);

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
