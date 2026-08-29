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
#include "autoexec.h"

// External helper from gui.c to trigger a fresh notebook page setup
extern void add_terminal_tab(AppContext *app);
extern void execute_next_queued_command(AppContext *app);

char *terminal_capture_context(AppContext *app) {
    if (!app || !app->gui.terminal_view || !VTE_IS_TERMINAL(app->gui.terminal_view))
        return g_strdup("None");

    VteTerminal *vte = VTE_TERMINAL(app->gui.terminal_view);
    long row, col;
    vte_terminal_get_cursor_position(vte, &col, &row);
    long context_depth = 1000;
    long start_row = (row > context_depth) ? (row - context_depth) : 0;

    char *text = vte_terminal_get_text_range(vte, start_row, 0, row, col, NULL, NULL, NULL);
    if (!text || !*text) {
        g_free(text);
        return g_strdup("None");
    }
    return text;
}

void clear_terminal_ghosts(VteTerminal *terminal) {
    // \033[2J clears screen, \033[3J clears scrollback
    vte_terminal_feed(terminal, "\033[2J\033[3J", -1);
}

void on_vte_child_exited(VteTerminal *vte, gint status, gpointer user_data) {
    AppContext *app = (AppContext *)user_data;
    if (!app) return;

    DEBUG_PRINT("[VTE]: Process exited (status %d). Marking terminal available.\n", status);
    
    // Unlock and trigger the next enqueued autoexec command
    app->aiterm_runtime.is_command_running = FALSE;
    execute_next_queued_command(app);
}

void apply_terminal_transparency(AppContext *app) {
    if (!app) return;

    GdkRGBA bg_color;
    gdk_rgba_parse(&bg_color, "#000000");
    bg_color.alpha = app->gui.transparency;

    // 1. Apply to active terminal pointer if valid
    if (app->gui.terminal_view && GTK_IS_WIDGET(app->gui.terminal_view)) {
        vte_terminal_set_color_background(VTE_TERMINAL(app->gui.terminal_view), &bg_color);
    }

    // 2. Loop through ALL notebook tabs so every single open terminal gets updated
    if (app->gui.notebook && GTK_IS_NOTEBOOK(app->gui.notebook)) {
        int n_pages = gtk_notebook_get_n_pages(GTK_NOTEBOOK(app->gui.notebook));
        for (int i = 0; i < n_pages; i++) {
            GtkWidget *scrolled = gtk_notebook_get_nth_page(GTK_NOTEBOOK(app->gui.notebook), i);
            if (scrolled && GTK_IS_BIN(scrolled)) {
                GtkWidget *term = gtk_bin_get_child(GTK_BIN(scrolled));
                if (term && VTE_IS_TERMINAL(term)) {
                    vte_terminal_set_color_background(VTE_TERMINAL(term), &bg_color);
                }
            }
        }
    }
}

void apply_ai_transparency(AppContext *app) {
    if (!app->gui.gemini_view || !GTK_IS_WIDGET(app->gui.gemini_view) || !app->gui.ai_css_provider) return;

    // Convert float to string using standard C locale format to prevent comma decimal bugs
    char alpha_str[16];
    g_ascii_dtostr(alpha_str, sizeof(alpha_str), app->gui.ai_transparency);

    char *css = g_strdup_printf(
        "textview, textview text {\n"
        "    background-color: rgba(0, 0, 0, %s);\n"
        "    color: #dcdcdc;\n"
        "}\n",
        alpha_str
    );

    gtk_css_provider_load_from_data(app->gui.ai_css_provider, css, -1, NULL);
    g_free(css);
}

void apply_visual_settings(AppContext *app) {
    if (!app) return;

    GdkRGBA bg_color;
    gdk_rgba_parse(&bg_color, "#000000");
    bg_color.alpha = app->gui.transparency;

    // 1. Apply Terminal Transparency to active view & all notebook tabs
    if (app->gui.terminal_view && GTK_IS_WIDGET(app->gui.terminal_view)) {
        vte_terminal_set_color_background(VTE_TERMINAL(app->gui.terminal_view), &bg_color);
    }

    if (app->gui.notebook && GTK_IS_NOTEBOOK(app->gui.notebook)) {
        int n_pages = gtk_notebook_get_n_pages(GTK_NOTEBOOK(app->gui.notebook));
        for (int i = 0; i < n_pages; i++) {
            GtkWidget *scrolled = gtk_notebook_get_nth_page(GTK_NOTEBOOK(app->gui.notebook), i);
            if (scrolled && GTK_IS_BIN(scrolled)) {
                GtkWidget *term = gtk_bin_get_child(GTK_BIN(scrolled));
                if (term && VTE_IS_TERMINAL(term)) {
                    vte_terminal_set_color_background(VTE_TERMINAL(term), &bg_color);
                }
            }
        }
    }

    // 2. Apply Terminal Font
    if (app->gui.terminal_font && app->gui.terminal_view && GTK_IS_WIDGET(app->gui.terminal_view)) {
        PangoFontDescription *desc = pango_font_description_from_string(app->gui.terminal_font);
        vte_terminal_set_font(VTE_TERMINAL(app->gui.terminal_view), desc);
        pango_font_description_free(desc);
    }

    // 3. Apply AI Transparency (This was missing!)
    apply_ai_transparency(app);

    // 4. Update AI Pane Font
    if (app->gui.ai_font && app->gui.gemini_view && GTK_IS_WIDGET(app->gui.gemini_view)) {
        PangoFontDescription *desc = pango_font_description_from_string(app->gui.ai_font);
        gtk_widget_override_font(app->gui.gemini_view, desc);
        pango_font_description_free(desc);
    }
}

static gboolean on_key_press(GtkWidget *terminal, GdkEventKey *event, gpointer user_data) {
    AppContext *app = (AppContext *)user_data;

    // Check for Ctrl + Tab
    if ((event->state & GDK_CONTROL_MASK) && event->keyval == GDK_KEY_Tab) {
        if (app->gui.gemini_view && GTK_IS_WIDGET(app->gui.gemini_view)) {
            gtk_widget_grab_focus(app->gui.gemini_view);
        }
        return TRUE; // STOP VTE from consuming Ctrl+Tab
    }

    // Existing Ctrl + Shift + C/V handlers...
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
            // VTE access is performed on the GTK thread. Do not hold the
            // accumulator mutex while querying or processing the terminal.
            char *new_text = vte_terminal_get_text_range(
                VTE_TERMINAL(app->gui.terminal_view),
                app->database.last_processed_row, 0,
                cur_row, cur_col,
                NULL, NULL, NULL
            );

            if (new_text && strlen(new_text) > 1) {
                app->database.last_processed_row = cur_row;
                char *cleaned_text = strip_blank_lines(new_text);
                g_free(new_text);
                new_text = cleaned_text;

                if (new_text && (app->sys.tee_enabled || app->sys.autoreply_enabled)) {
                    g_mutex_lock(&app->access.buffer_mutex);
                    g_string_append(app->aiterm_runtime.tee_accumulator, new_text);
                    g_mutex_unlock(&app->access.buffer_mutex);
                }
            }

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

        return TRUE; // Right-click handled
    }

    // CRITICAL: Must return FALSE for left-click/drag so VTE gets the event!
    return FALSE;
}

GtkWidget* setup_terminal(AppContext *app) {
    app->database.last_row = 0;
    app->database.last_col = 0;
    app->database.silence_ticks = 0;
    app->database.last_processed_row = 0;

    GtkWidget *new_term = vte_terminal_new();
    app->gui.terminal_view = new_term;
    apply_terminal_transparency(app);

    vte_terminal_set_mouse_autohide(VTE_TERMINAL(new_term), FALSE);
    vte_terminal_set_scroll_on_output(VTE_TERMINAL(new_term), FALSE);
    vte_terminal_set_scroll_on_keystroke(VTE_TERMINAL(new_term), TRUE);
    vte_terminal_set_cursor_shape(VTE_TERMINAL(new_term), VTE_CURSOR_SHAPE_BLOCK);
    vte_terminal_set_cursor_blink_mode(VTE_TERMINAL(new_term), VTE_CURSOR_BLINK_ON);

    // Create Scrolled Window Container
    GtkWidget *scrolled_window = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_window),
                                   GTK_POLICY_AUTOMATIC,
                                   GTK_POLICY_ALWAYS);
    gtk_container_add(GTK_CONTAINER(scrolled_window), new_term);

    // Bind Keyboard and Right-Click Event Listeners
    g_signal_connect(new_term, "key-press-event", G_CALLBACK(on_key_press), app);
    g_signal_connect(new_term, "button-press-event", G_CALLBACK(on_terminal_button_press), app);
    g_signal_connect(new_term, "child-exited", G_CALLBACK(on_vte_child_exited), app); 
    g_timeout_add(250, throttled_delta_check, app);

    vte_terminal_spawn_async(VTE_TERMINAL(new_term),
        VTE_PTY_DEFAULT, NULL, (char *[]){"/bin/bash", NULL},
        NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, -1, NULL, NULL, NULL);
    vte_terminal_set_scrollback_lines(VTE_TERMINAL(new_term), 10000);

    // Save reference to terminal view in app struct
    app->gui.terminal_view = new_term;

    // Return the scrolled window container to be packed into the notebook tab
    return scrolled_window;
}

