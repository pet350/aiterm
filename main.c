/* Main program file for aiterm
** The terminal emulator with AI assistance
** By: Peter Talbott
** Assisted compilation from Gemini and OpenAI
*/

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <mariadb/mysql.h>
#include "gui.h"
#include "update.h"
#include "utils.h"
#include "tee_handler.h"
#include "crypto.h" // Add this include
#include "help.h"

// Current AITERM version
const char* AITERM_VERSION = "0.8.3-stable";
const char* CONFIG_FILE = "/etc/aiterm.conf";

int debug_mode = 0;
AppContext *global_app = NULL;

int main(int argc, char *argv[]) {
    AppContext *app = g_malloc0(sizeof(AppContext));
    global_app = app;

    // Set default autoreply to OFF. Added 0.7.4-delta
    app->autoreply_enabled = FALSE;

    // --- NEW: Release the lock ---
    app->is_processing = FALSE;

    // 1. Generate the unique ID immediately
    char session_buf[64];
    snprintf(session_buf, sizeof(session_buf), "sess-%ld", (long)time(NULL));
    app->session_uuid = strdup(session_buf);
	app->sequence_id = 0; // NEW: Start counter at zero

    // 2. Parse command line options if any
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--debug") == 0) {
            debug_mode = 1;
        }
	    if (strcmp(argv[i], "--version") == 0) {
	        printf("aiterm %s\n", AITERM_VERSION);
	        exit(0);
	    }

        if (strcmp(argv[i], "--features") == 0) {
            printf("%s\n", get_features_text());
            exit(0);
        }

        if (strcmp(argv[i], "--help") == 0) {
            printf("%s\n", get_cmd_help());
            exit(0);
        }

        if (strncmp(argv[i], "--crypt-pw=", 11) == 0) {
            char *plaintext = argv[i] + 11;
            char *encrypted = crypt_to_hex(plaintext);
            if (encrypted) {
                printf("%s\n", encrypted);
                free(encrypted);
            }
            exit(0);
        }
    }

    // 3. Initialize GTK
    gtk_init(&argc, &argv);
	g_object_set(gtk_settings_get_default(),
             "gtk-application-prefer-dark-theme", TRUE,
             NULL);

    // 4 Initialize App Context and load config
    load_config(app);

    // 5) Provision the remote database on the XEN VM
    pthread_mutex_init(&app->db_mutex, NULL);

    // 2. Call the refactored init function
    if (init_remote_db(app)) {
        DEBUG_PRINT("Database setup and persistent connection ready.\n");
    } else {
        DEBUG_PRINT("Database offline. History will not be saved.\n");
    }

    // 6) INITIALIZE THE TEE HANDLER HERE
    tee_handler_init(app); //

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

    // 8) Build the UI (from gui.c)
    setup_gui(app);

    // 9. Enter the GTK Main Event Loop
    gtk_main();

    // 10) Clean up
    if (app->global_db_conn) {
        mysql_close(app->global_db_conn);
    }
    pthread_mutex_destroy(&app->db_mutex);

    g_free(app);
    return 0;
}


