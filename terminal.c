// terminal.c

#include <stdio.h>
#include <string.h>
#include <vte/vte.h>
#include <gtk/gtk.h>
#include "utils.h"    // <--- THIS IS MISSING
#include "terminal.h"
#include "gui.h"
#include "update.h"

// --- Internal state for output tracking ---
static char *last_snapshot = NULL;

void apply_terminal_transparency(AppContext *app) {
    GdkRGBA bg_color;

    // Parse base color (black) and apply the transparency value as the alpha channel
    gdk_rgba_parse(&bg_color, "#000000");
    bg_color.alpha = app->transparency;

    // This is the correct VTE 2.91 method for transparency
    vte_terminal_set_color_background(VTE_TERMINAL(app->terminal_view), &bg_color);
}

void apply_visual_settings(AppContext *app) {
    // 1. Update Terminal (VTE) - Transparency & Font
    GdkRGBA bg_color;
    gdk_rgba_parse(&bg_color, "#000000");
    bg_color.alpha = app->transparency; 
    vte_terminal_set_color_background(VTE_TERMINAL(app->terminal_view), &bg_color);

    if (app->terminal_font) {
        PangoFontDescription *desc = pango_font_description_from_string(app->terminal_font);
        vte_terminal_set_font(VTE_TERMINAL(app->terminal_view), desc);
        pango_font_description_free(desc);
    }

    // 2. Update AI Pane (GtkTextView) - Transparency via CSS
    // Using %.2f ensures the CSS parser sees a valid number (e.g., 0.85)
    char *css = g_strdup_printf(
        "textview, textview text { background-color: rgba(0, 0, 0, %.2f); }", 
        app->transparency
    );
    gtk_css_provider_load_from_data(app->ai_css_provider, css, -1, NULL);
    g_free(css);

    // 3. Update AI Pane (GtkTextView) - Font via Native GTK
    // This avoids the "Pango syntax is deprecated" CSS warning
    if (app->ai_font) {
        PangoFontDescription *desc = pango_font_description_from_string(app->ai_font);
        gtk_widget_override_font(app->gemini_view, desc);
        pango_font_description_free(desc);
    }
}


//void apply_visual_settings(AppContext *app) {
//    // 1. Update Terminal (VTE)
//    GdkRGBA bg_color;
//    gdk_rgba_parse(&bg_color, "#000000");
//    bg_color.alpha = app->transparency;
//    vte_terminal_set_color_background(VTE_TERMINAL(app->terminal_view), &bg_color);
//
//    // --- Font ---
//    if (app->terminal_font) {
//        PangoFontDescription *desc = pango_font_description_from_string(app->terminal_font);
//        vte_terminal_set_font(VTE_TERMINAL(app->terminal_view), desc);
//        pango_font_description_free(desc);
//    }
//
//    // 2. Update AI Pane (CSS)
//    // We use rgba(13, 13, 13) to match your existing theme [cite: 23]
//    if (app->ai_font) {
//        PangoFontDescription *desc = pango_font_description_from_string(app->ai_font);
//        gtk_widget_override_font(app->gemini_view, desc);
//        pango_font_description_free(desc);
//    }
//
//    // Refined selector for better coverage
//    char *css = g_strdup_printf(
//        "textview, textview text { "
//	"    background-color: rgba(0,0,0,%f); "
//	"    font: %s; "
//	"}",
//        app->transparency, app->ai_font
//    );
//
//    gtk_css_provider_load_from_data(app->ai_css_provider, css, -1, NULL);
//    g_free(css);
//}

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
    process_for_ai(app, buf, TRUE);
}

static gboolean on_key_press(GtkWidget *terminal, GdkEventKey *event, gpointer user_data) {
    // Check if both Control and Shift are held down
    if ((event->state & GDK_CONTROL_MASK) && (event->state & GDK_SHIFT_MASK)) {
        if (event->keyval == GDK_KEY_C || event->keyval == GDK_KEY_c) {
            vte_terminal_copy_clipboard_format(VTE_TERMINAL(terminal), VTE_FORMAT_TEXT);
            return TRUE; // Signal that we handled the event
        }
        if (event->keyval == GDK_KEY_V || event->keyval == GDK_KEY_v) {
            vte_terminal_paste_clipboard(VTE_TERMINAL(terminal));
            return TRUE;
        }
    }
    return FALSE;
}

// In terminal.c -> on_terminal_output
static void on_terminal_output(VteTerminal *terminal, gpointer user_data) {
    AppContext *app = (AppContext *)user_data;
    char *current = vte_terminal_get_text(terminal, NULL, NULL, NULL);
    if (!current) return;

    // Fix #2: Initialize if NULL so we don't lose the first-run data
    if (!last_snapshot) last_snapshot = g_strdup("");

    size_t old_len = strlen(last_snapshot);
    size_t new_len = strlen(current);

    // Fix #1: Use a more robust delta detection
    // If the buffer wrapped, the old_len might be invalid. 
    // We treat the current EOF as the source of truth.
    if (new_len > 0) {
        const char *delta = NULL;
        
        if (new_len > old_len) {
            delta = current + old_len;
        } else {
            // Buffer wrapped/shifted: send the last chunk of the current buffer
            // To be truly robust on Apollo Lake, we'll just take the last 4KB
            size_t chunk_size = (new_len > 4096) ? 4096 : new_len;
            delta = current + (new_len - chunk_size);
        }

        if (delta && strlen(delta) > 0) {
            DEBUG_PRINT("DEBUG: OUTPUT delta captured (%zu bytes)\n", strlen(delta));
            process_for_ai(app, delta, FALSE);
        }
    }

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

    // terminal.c -> inside setup_terminal
    vte_terminal_set_scrollback_lines(VTE_TERMINAL(terminal), 10000); // 10k lines is safe for 149MB RAM [cite: 441]

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

    g_signal_connect(terminal, "key-press-event", G_CALLBACK(on_key_press), NULL);
    return terminal;
}
