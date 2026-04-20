#include <stdio.h>
#include <stdlib.h>
#include "gui.h"
#include "update.h"
#include "utils.h"
#include "tee_handler.h"

// Current AITERM version
const char* AITERM_VERSION = "0.7-alpha";

int debug_mode = 0; // Global flag
AppContext *global_app = NULL; // The actual definition

int main(int argc, char *argv[]) {

    AppContext *app = g_malloc0(sizeof(AppContext));
    global_app = app; // Point the global to your active context

    // 0. Parse command line options if any
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--debug") == 0) {
            debug_mode = 1;
        }
    }

    // 1. Initialize GTK
    gtk_init(&argc, &argv);
	g_object_set(gtk_settings_get_default(),
             "gtk-application-prefer-dark-theme", TRUE,
             NULL);

    // 2.0) Initialize App Context and load config
    load_config(app); // This populates app->api_key from aiterm.conf

    // 2.1) Provision the remote database on the XEN VM
    // Add this line here:
    init_remote_db(app);

    // 2.2 INITIALIZE THE TEE HANDLER HERE
    tee_handler_init(app); //

    // 3.0) FALLBACK: Only check env vars if config key is still NULL
    if (!app->api_key || strlen(app->api_key) == 0) {
        app->api_key = getenv("GEMINI_API_KEY");
    }

    if (!app->api_key || strlen(app->api_key) == 0) {
        app->api_key = getenv("OPENAI_API_KEY");
    }

    if (!app->api_key) {
        fprintf(stderr, "Error: No API key found.\n");
    }

    // 4.0) Build the UI (from gui.c)
    setup_gui(app);

    // 5. Connect the Input Signal (from update.c)
    // This tells GTK: "When the user hits Enter in the entry, call on_input_activate"
    g_signal_connect(app->entry, "activate", G_CALLBACK(on_input_activate), app);

    // 6. Enter the GTK Main Event Loop
    // This blocks until the window is closed
    gtk_main();

    // 7. Cleanup
    g_free(app);

    return 0;
}
