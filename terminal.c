// part of aiterm project
// terminal.c
// Functions for setting up and managing terminal I/O
// By: Peter Talbott
// Assisted by: Gemini
// May 2026

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

// External helper from gui.c to trigger a fresh notebook page setup
extern void add_terminal_tab(AppContext *app);

void clear_terminal_ghosts(VteTerminal *terminal) {
    // \033[2J clears screen, \033[3J clears scrollback
    vte_terminal_feed(terminal, "\033[2J\033[3J", -1);
}

void apply_terminal_transparency(AppContext *app) {
    // Guard against uninitialized active terminal view
    if (!app->gui.terminal_view || !GTK_IS_WIDGET(app->gui.terminal_view)) return;

    GdkRGBA bg_color;
    gdk_rgba_parse(&bg_color, "#000000");
    bg_color.alpha = app->gui.transparency;
    vte_terminal_set_color_background(VTE_TERMINAL(app->gui.terminal_view), &bg_color);
}

void apply_ai_transparency(AppContext *app) {
    if (!app->gui.gemini_view || !GTK_IS_WIDGET(app->gui.gemini_view) || !app->gui.ai_css_provider) return;

    char *css = g_strdup_printf(
        "textview { font-family: '%s'; font-size: 10pt; }\n"
        "textview text { background-color: rgba(0, 0, 0, %f); color: #dcdcdc; }",
        app->gui.ai_font ? app->gui.ai_font : "Monospace",
        app->gui.ai_transparency
    );
    gtk_css_provider_load_from_data(app->gui.ai_css_provider, css, -1, NULL);
    g_free(css);
}

void apply_visual_settings(AppContext *app) {
    if (!app) return;

    // 1. Apply Terminal Transparency
    apply_terminal_transparency(app);

    // 2. Apply Terminal Font
    if (app->gui.terminal_font && app->gui.terminal_view && GTK_IS_WIDGET(app->gui.terminal_view)) {
        PangoFontDescription *desc = pango_font_description_from_string(app->gui.terminal_font);
        vte_terminal_set_font(VTE_TERMINAL(app->gui.terminal_view), desc);
        pango_font_description_free(desc);
    }

    // 3. Apply AI Transparency
    apply_ai_transparency(app);

    // 4. Update AI Pane (GtkTextView) - Only run if the layout widget is fully instantiated
    if (app->gui.ai_font && app->gui.gemini_view && GTK_IS_WIDGET(app->gui.gemini_view)) {
        PangoFontDescription *desc = pango_font_description_from_string(app->gui.ai_font);
        gtk_widget_override_font(app->gui.gemini_view, desc);
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
    if (!app->gui.terminal_view || !GTK_IS_WIDGET(app->gui.terminal_view)) return TRUE;

    long cur_row, cur_col;
    vte_terminal_get_cursor_position(VTE_TERMINAL(app->gui.terminal_view), &cur_col, &cur_row);

    if (cur_row != app->database.last_row || cur_col != app->database.last_col) {
        app->database.last_row = cur_row;
        app->database.last_col = cur_col;
        app->database.silence_ticks = 0;
    } else {
        app->database.silence_ticks++;

        // Trigger after ~2 seconds of silence
        if (app->database.silence_ticks == 8) {
            // ONLY get the text from where we last finished up to the current row
	    g_mutex_lock(&app->access.buffer_mutex);
            // Define the range of new text
            char *new_text = vte_terminal_get_text_range(
                VTE_TERMINAL(app->gui.terminal_view),
                app->database.last_processed_row, 0,  // Start from last row
                cur_row, cur_col,       // End at current cursor
                NULL, NULL, NULL
            );

            if (new_text && strlen(new_text) > 1) {
                app->database.last_processed_row = cur_row; // Move the pointer forward
		char *cleaned_text = strip_blank_lines(new_text);
		free(new_text);
		new_text=g_strdup(cleaned_text); // yet another attempt at silencing the blanks

 		// Create the XML Wrapper
//    		char *xml_chunk = g_strdup_printf(
//		        "<tee session_uuid=\"%s\" timestamp=\"%ld\">\n%s\n</tee>\n",
//		        app->session.session_uuid, time(NULL), new_text);

                if (app->sys.tee_enabled || app->sys.autoreply_enabled) {
                    // APPEND the new text, don't assign/overwrite!
                    g_string_append(app->aiterm_runtime.tee_accumulator, new_text);
                }
            }
            g_mutex_unlock(&app->access.buffer_mutex);
            if (new_text) {
                if (app->sys.autoreply_enabled && strlen(new_text) > 1) {
                    tee_flush_timed(app);
                }
                g_free(new_text);
	    }
        }
    }
    return TRUE;
}

// Right-Click Dropdown Option Handlers
static void menu_new_tab_selected(GtkMenuItem *menuitem, gpointer user_data) {
    AppContext *app = (AppContext *)user_data;
    add_terminal_tab(app);
}

static void menu_copy_selected(GtkMenuItem *menuitem, gpointer user_data) {
    VteTerminal *terminal = VTE_TERMINAL(user_data);
    vte_terminal_copy_clipboard_format(terminal, VTE_FORMAT_TEXT);
}

static void menu_paste_selected(GtkMenuItem *menuitem, gpointer user_data) {
    VteTerminal *terminal = VTE_TERMINAL(user_data);
    vte_terminal_paste_clipboard(terminal);
}

// Mouse Button Interceptor Signal Hook
gboolean on_terminal_button_press(GtkWidget *terminal, GdkEventButton *event, gpointer user_data) {
    AppContext *app = (AppContext *)user_data;

    // GDK_BUTTON_SECONDARY targets standard right-click (Button 3)
    if (event->type == GDK_BUTTON_PRESS && event->button == GDK_BUTTON_SECONDARY) {
        GtkWidget *menu = gtk_menu_new();

        GtkWidget *new_tab_item = gtk_menu_item_new_with_label("New Tab");
        GtkWidget *copy_item = gtk_menu_item_new_with_label("Copy");
        GtkWidget *paste_item = gtk_menu_item_new_with_label("Paste");

        // Attach layout actions to item selections
        g_signal_connect(new_tab_item, "activate", G_CALLBACK(menu_new_tab_selected), app);
        g_signal_connect(copy_item, "activate", G_CALLBACK(menu_copy_selected), terminal);
        g_signal_connect(paste_item, "activate", G_CALLBACK(menu_paste_selected), terminal);

        // Assemble the layout menu tree
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), new_tab_item);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), copy_item);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), paste_item);

        gtk_widget_show_all(menu);

        // Render the popup immediately at cursor location coordinates
        gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent *)event);
        return TRUE; // Event consumed cleanly
    }
    return FALSE;
}

GtkWidget* setup_terminal(AppContext *app) {
    app->database.last_row = 0;
    app->database.last_col = 0;
    app->database.silence_ticks = 0;
    app->database.last_processed_row = 0;

    GtkWidget *new_term = vte_terminal_new();

    // vte_terminal_spawn_async(NULL, NULL, NULL, NULL, -1, NULL, on_terminal_ready, app, NULL);
    vte_terminal_set_cursor_shape(VTE_TERMINAL(new_term), VTE_CURSOR_SHAPE_BLOCK);
    vte_terminal_set_cursor_blink_mode(VTE_TERMINAL(new_term), VTE_CURSOR_BLINK_ON);

    // Bind Keyboard and Right-Click Event Listeners to the fresh layout layer
    g_signal_connect(new_term, "key-press-event", G_CALLBACK(on_key_press), app);
    g_signal_connect(new_term, "button-press-event", G_CALLBACK(on_terminal_button_press), app);

    g_timeout_add(250, throttled_delta_check, app);

    vte_terminal_spawn_async(VTE_TERMINAL(new_term),
        VTE_PTY_DEFAULT, NULL, (char *[]){"/bin/bash", NULL},
        NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, -1, NULL, NULL, NULL);
    vte_terminal_set_scrollback_lines(VTE_TERMINAL(new_term), 10000);

    return new_term;
}
