#include <stdio.h>
#include <stdlib.h>
#include "gui.h"
#include "update.h"

int main(int argc, char *argv[]) {
    // 1. Initialize GTK
    gtk_init(&argc, &argv);
	g_object_set(gtk_settings_get_default(),
             "gtk-application-prefer-dark-theme", TRUE,
             NULL);
    // 2. Initialize our App Context (The "Model")
    AppContext *app = g_malloc0(sizeof(AppContext));

    // 3. Check for API Key (Safety First)
    app->api_key = getenv("OPENAI_API_KEY");
    if (!app->api_key) {
        fprintf(stderr, "Error: OPENAI_API_KEY environment variable not set.\n");
        return 1;
    }

    // 4. Build the UI (from gui.c)
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
