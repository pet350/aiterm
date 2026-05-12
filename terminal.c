#include <stdio.h>
#include <string.h>
#include <vte/vte.h>
#include <gtk/gtk.h>
#include <mariadb/mysql.h>
#include "utils.h"
#include "terminal.h"
#include "gui.h"
#include "update.h"
#include "tee_handler.h"

static long last_row = 0;
static long last_col = 0;
static int silence_ticks = 0;


void apply_terminal_transparency(AppContext *app) {
    GdkRGBA bg_color;
    gdk_rgba_parse(&bg_color, "#000000");
    bg_color.alpha = app->transparency;
    vte_terminal_set_color_background(VTE_TERMINAL(app->terminal_view), &bg_color);
}

// Add this to update the AI Pane as well
void apply_ai_transparency(AppContext *app) {
    if (!app->gemini_view || !app->ai_css_provider) return;

    // We use a CSS provider to force the textview and its viewport to be transparent
    char *css = g_strdup_printf(
        "textview { font-family: '%s'; font-size: 10pt; }\n"
        "textview text { background-color: rgba(0, 0, 0, %f); color: #dcdcdc; }", 
        app->ai_font ? app->ai_font : "Monospace", // Fallback to Monospace if NULL [cite: 314]
        app->ai_transparency
    );
    gtk_css_provider_load_from_data(app->ai_css_provider, css, -1, NULL);
    g_free(css);

}

void apply_visual_settings(AppContext *app) {
    // 1. Apply Terminal Transparency
    apply_terminal_transparency(app);
    
    // 2. Apply Terminal Font
    if (app->terminal_font) {
        PangoFontDescription *desc = pango_font_description_from_string(app->terminal_font);
        vte_terminal_set_font(VTE_TERMINAL(app->terminal_view), desc);
        pango_font_description_free(desc);
    }
    
    // 3. Apply AI Transparency
    apply_ai_transparency(app);
    
    // 4. Update AI Pane (GtkTextView) - Font via Native GTK
    if (app->ai_font) {
        PangoFontDescription *desc = pango_font_description_from_string(app->ai_font);
        gtk_widget_override_font(app->gemini_view, desc);
        pango_font_description_free(desc);
    }
}

static gboolean on_key_press(GtkWidget *terminal, GdkEventKey *event, gpointer user_data) {
    if ((event->state & GDK_CONTROL_MASK) && (event->state & GDK_SHIFT_MASK)) {
        if (event->keyval == GDK_KEY_C || event->keyval == GDK_KEY_c) {
            vte_terminal_copy_clipboard_format(VTE_TERMINAL(terminal), VTE_FORMAT_TEXT);
            return TRUE;
        }
        if (event->keyval == GDK_KEY_V || event->keyval == GDK_KEY_v) {
            vte_terminal_paste_clipboard(VTE_TERMINAL(terminal));
            return TRUE;
        }
    }
    return FALSE;
}

static gboolean throttled_delta_check(gpointer user_data) {
    AppContext *app = (AppContext *)user_data;
    if (!app->terminal_view) return TRUE;

    long cur_row, cur_col;
    vte_terminal_get_cursor_position(VTE_TERMINAL(app->terminal_view), &cur_col, &cur_row);

    if (cur_row != last_row || cur_col != last_col) {
        last_row = cur_row;
        last_col = cur_col;
        silence_ticks = 0; 
    } else {
        silence_ticks++;

        // 2 seconds of silence
        if (silence_ticks == 8) {
            char *current_text = vte_terminal_get_text(VTE_TERMINAL(app->terminal_view), NULL, NULL, NULL);

            if (current_text && strlen(current_text) > 2) {
                // Create a temporary stripped copy to check for commands
                char *stripped = g_strstrip(g_strdup(current_text));

                // Only trigger "hw" if it's the VERY LAST thing in the terminal
                if (g_str_has_suffix(stripped, " hw") || g_str_has_suffix(stripped, "\nhw") || strcmp(stripped, "hw") == 0) {
                    append_ai_text(app, "\n[ System Hardware Stats ]\n", "user_tag");
                    extern char* get_hw_stats(); 
                    char *stats = get_hw_stats();
                    append_ai_text(app, stats, "ai_tag");

                    // Add a newline to terminal so it doesn't trigger again immediately
                    vte_terminal_feed(VTE_TERMINAL(app->terminal_view), "\r\n", 2);
                } 
                else if (app->autoreply_enabled) { 
                    append_ai_text(app, "\nLog Analysis Triggered...\n", "user_tag");
                    
                    g_mutex_lock(&app->buffer_mutex);
                    g_string_assign(app->tee_accumulator, current_text);
                    g_mutex_unlock(&app->buffer_mutex);
                    
                    extern void tee_flush_timed(AppContext *app);
                    tee_flush_timed(app);
                }
                g_free(stripped); // Clean up the copy!
            }
            g_free(current_text); // Clean up the VTE buffer!
        }
    }
    return TRUE;
}

// THIS IS THE FUNCTION THE LINKER IS LOOKING FOR
GtkWidget* setup_terminal(AppContext *app) {
    app->terminal_view = vte_terminal_new();
    vte_terminal_set_cursor_shape(VTE_TERMINAL(app->terminal_view), VTE_CURSOR_SHAPE_BLOCK);
    vte_terminal_set_cursor_blink_mode(VTE_TERMINAL(app->terminal_view), VTE_CURSOR_BLINK_ON);
    g_signal_connect(app->terminal_view, "key-press-event", G_CALLBACK(on_key_press), app);
    g_timeout_add(250, throttled_delta_check, app);

    vte_terminal_spawn_async(VTE_TERMINAL(app->terminal_view),
        VTE_PTY_DEFAULT, NULL, (char *[]){"/bin/bash", NULL},
        NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, -1, NULL, NULL, NULL);
    vte_terminal_set_scrollback_lines(VTE_TERMINAL(app->terminal_view), 10000);
    return app->terminal_view;
}


