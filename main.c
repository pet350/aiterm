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
#include "build_id.h"
#include "session_manager.h"
#include "config.h"

// Current AITERM version
const char* AITERM_VERSION	= "0.9.2";
const char* AITERM_BUILDID	= BUILD_ID;
const char* AITERM_BUILD_TIME	= BUILD_TIME;
const char* CONFIG_FILE		= "/etc/aiterm.conf";

void print_version() {
    printf("aiterm version %-16s\n", AITERM_VERSION);
    printf("Build ID: %s\n", AITERM_BUILDID);
    printf("Build Time: %s\n", AITERM_BUILD_TIME);
}

AppContext *global_app = NULL;

int main(int argc, char *argv[]) {
    AppContext *app = g_malloc0(sizeof(AppContext));
    global_app = app;

    // 1. Set initial variables to their needed defaults
    initialize_booleans(app);

    char *env_key = getenv("AITERM_MASTER_KEY");
    if (env_key) {
        app->master_key = strdup(env_key);
    }
    // 2. Parse command line options if any
    for (int i = 1; i < argc; i++) {
       if (strcmp(argv[i], "--debug") == 0) {
            app->debug_mode = TRUE;
       } else if (strcmp(argv[i], "--version") == 0) {
            print_version();
            exit(0);
       } else if (strcmp(argv[i], "--list-models") == 0 ) {
	    load_config(app);
	    if (!app->master_key) {
		printf("Error: no master key found!\n");
		exit(1);
	     }
	    char *models = gemini_list_models(app);
	    printf("Gemini Model List:\n%s\n", models);
	    if (app->master_key) {
        	// Overwrite memory with zeros before freeing
        	size_t len = strlen(app->master_key);
        	memset(app->master_key, 0, len);
        	free(app->master_key);
    	    }
	    free_provider_config(&app->provider_config);
            g_free(app);
	    exit(0);
       } else if  (strcmp(argv[i], "--provider") == 0) {
	     load_config(app);
	     char info[512];
	     snprintf(info, sizeof(info), "Provider: %s\nModel: %s", app->provider, app->model);
	     printf("%s\n", info);
	     exit(0);
       } else if (strcmp(argv[i], "--features") == 0) {
            printf("%s\n", get_features_text());
            exit(0);
       } else if (strcmp(argv[i], "--help") == 0) {
            printf("%s\n", get_cmd_help());
            exit(0);
       } else if (strncmp(argv[i], "--master=", 9) == 0) {
            app->master_key = strdup(argv[i] + 9);
       } else if (strncmp(argv[i], "--crypt-pw=", 11) == 0) {
	    if (!app->master_key) {
        	fprintf(stderr, "Error: You must provide a master key (via --master or AITERM_MASTER_KEY) before encrypting.\n");
        	exit(1);
    	    }
            char *plaintext = argv[i] + 11;
            char *encrypted = crypt_to_hex(plaintext, app->master_key);
            if (encrypted) {
        	printf("Encrypted string: %s\n", encrypted);
        	free(encrypted);
            } else {
        	fprintf(stderr, "Error: Encryption failed.\n");
            }
            exit(0);
       }
    }
    if (!app->master_key) {
	char *pwd = getpass("Enter Master Encryption Key: ");
	if (pwd) app->master_key = strdup(pwd);
    }

    // 3. Initialize GTK
    DEBUG_PRINT("[DEBUG]: [MAIN] Initializing GTK...\n");
    gtk_init(&argc, &argv);
    g_object_set(gtk_settings_get_default(), "gtk-application-prefer-dark-theme", TRUE, NULL);

    // 4 Initialize App Context and load config
    DEBUG_PRINT("[DEBUG]: [MAIN] Invoking load_config...\n");
    load_config(app);
    DEBUG_PRINT("[DEBUG]: [MAIN] load_config sequence complete.\n");

    // 5) Provision the remote database on the XEN VM
    pthread_mutex_init(&app->db_mutex, NULL);
    pthread_t db_init_thread;
    DEBUG_PRINT("[DEBUG]: [MAIN] Spawning asynchronous DB initialization thread...\n");
    if (pthread_create(&db_init_thread, NULL, init_db_thread_worker, app) == 0) {
        pthread_detach(db_init_thread); // Allow thread to clean itself up on exit
    } else {
        fprintf(stderr, "Error: Failed to spawn database initialization thread.\n");
    }

    DEBUG_PRINT("[DEBUG]: [MAIN] Initializing Session Manager...\n");
    session_init(app);
    DEBUG_PRINT("[DEBUG]: [MAIN] Session Manager active.\n");

    // 6) INITIALIZE THE TEE HANDLER HERE
    DEBUG_PRINT("[DEBUG]: [MAIN] Initializing Tee Handler...\n");
    tee_handler_init(app);
    DEBUG_PRINT("[DEBUG]: [MAIN] Tee Handler active.\n");

    // 7) FALLBACK: Only check env vars if config key is still NULL
    if (!app->api_key || strlen(app->api_key) == 0) {
        app->api_key = getenv("GEMINI_API_KEY");
    }

    if (!app->api_key || strlen(app->api_key) == 0) {
        app->api_key = getenv("OPENAI_API_KEY");
    }

    if (!app->api_key) {
        fprintf(stderr, "Error: No API key found.\n");
    }
    // inot AI Provider config
    init_provider_config(app);

    // init rate limiter
    ratelimit_init(&app->limiter, app->limiter.requests_per_minute);

    // 8) Build the UI (from gui.c)
    DEBUG_PRINT("[DEBUG]: [MAIN] Launching create_main_window GUI setup...\n");
    setup_gui(app);

    // 9. Enter the GTK Main Event Loop
    DEBUG_PRINT("[DEBUG]: [MAIN] Passing control to gtk_main loop.\n");
    gtk_main();

    // 10) Clean up
    if (app->global_db_conn) {
        mysql_close(app->global_db_conn);
    }

    DEBUG_PRINT("[DEBUG]: [MAIN] Closing threaded database connection.\n");
    pthread_mutex_destroy(&app->db_mutex);


    if (app->master_key) {
        // Overwrite memory with zeros before freeing
        size_t len = strlen(app->master_key);
        memset(app->master_key, 0, len);
        free(app->master_key);
    }
    free_provider_config(&app->provider_config);
    g_free(app);
    return 0;
}
