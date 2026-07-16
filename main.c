/* Main program file for aiterm			*/
/* The terminal emulator with AI assistance	*/
/* By: Peter Talbott				*/
/* Assisted compilation from Gemini and OpenAI  */

#include <stdio.h>
#include <stdlib.h>
#include <gtk/gtk.h>
#include <pthread.h>
#include <time.h>
#include <getopt.h>
#include <unistd.h>
#include <mariadb/mysql.h>

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

// Current AITERM version
const char* AITERM_VERSION	= "0.9.6";
const char* AITERM_BUILDID	= BUILD_ID;
const char* AITERM_BUILD_TIME	= BUILD_TIME;
const char* CONFIG_FILE		= "/etc/aiterm.conf";


AppContext *global_app = NULL;

int main(int argc, char *argv[]) {
    AppContext *app = g_malloc0(sizeof(AppContext));
    global_app = app;

    // 1. Set initial variables to their needed defaults
    initialize_booleans(app);

    // 2. Parse command line options if any
    parse_command_line_options(app, argc, argv);

    // 3. Initialize GTK
    DEBUG_PRINT("[DEBUG]: [MAIN] Initializing GTK...\n");
    gtk_init(&argc, &argv);
    g_object_set(gtk_settings_get_default(), "gtk-application-prefer-dark-theme", TRUE, NULL);

    // 4 Initialize App Context and load config
    DEBUG_PRINT("[DEBUG]: [MAIN] Invoking load_config... \n");
    load_config(app);
    DEBUG_PRINT("[DEBUG]: [MAIN] Done! load_config sequence is now complete.\n");

    // 5) Provision the remote database on the XEN VM
    pthread_mutex_init(&app->access.db_mutex, NULL);
    pthread_t db_init_thread;
    DEBUG_PRINT("[DEBUG]: [MAIN] Spawning asynchronous DB initialization thread...\n");
    if (pthread_create(&db_init_thread, NULL, init_db_thread_worker, app) == 0) {
        pthread_detach(db_init_thread); // Allow thread to clean itself up on exit
    } else {
        fprintf(stderr, "Error: Failed to spawn database initialization thread.\n");
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

    // 16. Enter the GTK Main Event Loop
    DEBUG_PRINT("[DEBUG]: [MAIN] Passing control to gtk_main loop.\n");
    gtk_main();

    // 17) Clean up
    if (app->database.global_db_conn) {
        mysql_close(app->database.global_db_conn);
    }

    DEBUG_PRINT("[DEBUG]: [MAIN] Closing threaded database connection.\n");
    pthread_mutex_destroy(&app->access.db_mutex);


    if (app->security.master_key) {
        // Overwrite memory with zeros before freeing
        size_t len = strlen(app->security.master_key);
        memset(app->security.master_key, 0, len);
        free(app->security.master_key);
    }
    free_provider_config(&app->provider_config);
    g_free(app);
    return 0;
}


// Thats All Folks! LOL!
// Latter!
