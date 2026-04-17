// terminal.c

#include <stdio.h>
#include <string.h>
#include <vte/vte.h>
#include <gtk/gtk.h>
#include "utils.h"    // <--- THIS IS MISSING
#include "terminal.h"
#include "gui.h"

// From update.c
extern void process_for_ai(AppContext *app, const char *text);

// --- Internal state for output tracking ---
static char *last_snapshot = NULL;

// --- INPUT CAPTURE (what user types) ---
static void on_terminal_input(VteTerminal *terminal,
                             const gchar *text,
                             guint size,
                             gpointer user_data)
{
    AppContext *app = (AppContext *)user_data;

    if (!text || size == 0) return;

    // Copy chunk safely
    char buf[256];
    size_t len = size < sizeof(buf) - 1 ? size : sizeof(buf) - 1;
    memcpy(buf, text, len);
    buf[len] = '\0';

    // Debug
    DEBUG_PRINT("DEBUG: INPUT chunk: [%s]\n", buf);

    // Send to tee
    process_for_ai(app, buf);
}

// --- OUTPUT CAPTURE (what terminal prints) ---
static void on_terminal_output(VteTerminal *terminal,
                               gpointer user_data)
{
    AppContext *app = (AppContext *)user_data;

    char *current = vte_terminal_get_text(
        terminal,
        NULL, NULL, NULL
    );

    if (!current) return;

    // First run: initialize snapshot
    if (!last_snapshot) {
        last_snapshot = g_strdup(current);
        g_free(current);
        return;
    }

    // Only process NEW content
    size_t old_len = strlen(last_snapshot);
    size_t new_len = strlen(current);

    if (new_len > old_len) {
        const char *delta = current + old_len;

        // Filter out empty/noise chunks
        if (strlen(delta) > 1) {
            DEBUG_PRINT("DEBUG: OUTPUT delta:\n%s\n", delta);

            process_for_ai(app, delta);
        }
    }

    // Update snapshot
    g_free(last_snapshot);
    last_snapshot = g_strdup(current);

    g_free(current);
}

// --- TERMINAL SETUP ---
GtkWidget* setup_terminal(AppContext *app)
{
    GtkWidget *terminal = vte_terminal_new();

    GdkRGBA bg_color, fg_color;

    gchar **envp = g_get_environ();
    char *command[] = { "/bin/bash", NULL };

    // Colors
    gdk_rgba_parse(&bg_color, "#000000");
    gdk_rgba_parse(&fg_color, "#00ff00");

    vte_terminal_set_colors(
        VTE_TERMINAL(terminal),
        &fg_color,
        &bg_color,
        NULL,
        0
    );

    // Spawn shell
    vte_terminal_spawn_async(
        VTE_TERMINAL(terminal),
        VTE_PTY_DEFAULT,
        NULL,
        command,
        envp,
        G_SPAWN_SEARCH_PATH,
        NULL, NULL,
        NULL,
        -1,
        NULL,
        NULL, NULL
    );

    g_strfreev(envp);

    // --- SIGNAL HOOKS ---

    // Input (user typing)
    g_signal_connect(
        terminal,
        "commit",
        G_CALLBACK(on_terminal_input),
        app
    );

    // Output (terminal updates)
    g_signal_connect(
        terminal,
        "contents-changed",
        G_CALLBACK(on_terminal_output),
        app
    );

    return terminal;
}
