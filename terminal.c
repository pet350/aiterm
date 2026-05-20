// terminal.c
// Part of the aiterm project
// C Program file for terminal interactions
// By: Peter Talbott
// With assistance from Gemini and OpenAI
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

void apply_terminal_transparency(AppContext *app) {
    // Guard against uninitialized active terminal view
    if (!app->terminal_view || !GTK_IS_WIDGET(app->terminal_view)) return;

    GdkRGBA bg_color;
    gdk_rgba_parse(&bg_color, "#000000");
    bg_color.alpha = app->transparency;
    vte_terminal_set_color_background(VTE_TERMINAL(app->terminal_view), &bg_color);
}

void apply_ai_transparency(AppContext *app) {
    if (!app->gemini_view || !GTK_IS_WIDGET(app->gemini_view) || !app->ai_css_provider) return;

    char *css = g_strdup_printf(
        "textview { font-family: '%s'; font-size: 10pt; }\n"
        "textview text { background-color: rgba(0, 0, 0, %f); color: #dcdcdc; }",
        app->ai_font ? app->ai_font : "Monospace",
        app->ai_transparency
    );
    gtk_css_provider_load_from_data(app->ai_css_provider, css, -1, NULL);
    g_free(css);
}

void apply_visual_settings(AppContext *app) {
    if (!app) return;

    // 1. Apply Terminal Transparency
    apply_terminal_transparency(app);

    // 2. Apply Terminal Font
    if (app->terminal_font && app->terminal_view && GTK_IS_WIDGET(app->terminal_view)) {
        PangoFontDescription *desc = pango_font_description_from_string(app->terminal_font);
        vte_terminal_set_font(VTE_TERMINAL(app->terminal_view), desc);
        pango_font_description_free(desc);
    }

    // 3. Apply AI Transparency
    apply_ai_transparency(app);

    // 4. Update AI Pane (GtkTextView) - Only run if the layout widget is fully instantiated
    if (app->ai_font && app->gemini_view && GTK_IS_WIDGET(app->gemini_view)) {
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
    if (!app->terminal_view || !GTK_IS_WIDGET(app->terminal_view)) return TRUE;

    long cur_row, cur_col;
    vte_terminal_get_cursor_position(VTE_TERMINAL(app->terminal_view), &cur_col, &cur_row);

    if (cur_row != app->last_row || cur_col != app->last_col) {
        app->last_row = cur_row;
        app->last_col = cur_col;
        app->silence_ticks = 0;
    } else {
        app->silence_ticks++;

        // Trigger after ~2 seconds of silence
        if (app->silence_ticks == 8) {
            // ONLY get the text from where we last finished up to the current row
	    g_mutex_lock(&app->buffer_mutex);
            // Define the range of new text
            char *new_text = vte_terminal_get_text_range(
                VTE_TERMINAL(app->terminal_view),
                app->last_processed_row, 0,  // Start from last row
                cur_row, cur_col,       // End at current cursor
                NULL, NULL, NULL
            );

            if (new_text && strlen(new_text) > 1) {
                app->last_processed_row = cur_row; // Move the pointer forward

                if (app->tee_enabled || app->autoreply_enabled) {
                    // APPEND the new text, don't assign/overwrite!
                    g_string_append(app->tee_accumulator, new_text);
                }
            }
            g_mutex_unlock(&app->buffer_mutex);
            if (new_text) {
                if (app->autoreply_enabled && strlen(new_text) > 1) {
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
    app->last_row = 0;
    app->last_col = 0;
    app->silence_ticks = 0;
    app->last_processed_row = 0;

    GtkWidget *new_term = vte_terminal_new();
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
