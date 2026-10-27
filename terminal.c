// part of aiterm project
// terminal.c
// Functions for setting up and managing terminal I/O
// By: Peter Talbott
// Assisted by: Gemini and OpenAI
// May 2026 - Oct 2026

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

/* Per-tab capture bookkeeping. Previously this lived in app->database and was
 * shared by every tab (and reset by every new tab), so only the newest tab's
 * row counters ever made sense. It now hangs off each VteTerminal. */
typedef struct {
    long last_row;
    long last_col;
    long last_processed_row;
    int  silence_ticks;
    char name[16];               /* "pts/N", matches the notebook tab label */
} TeeTabState;

static TeeTabState *tee_tab_state(VteTerminal *vte) {
    TeeTabState *st = g_object_get_data(G_OBJECT(vte), "tee-tab-state");
    if (!st) {
        static int tab_seq = 0;
        st = g_new0(TeeTabState, 1);
        snprintf(st->name, sizeof(st->name), "pts/%d", ++tab_seq);
        g_object_set_data_full(G_OBJECT(vte), "tee-tab-state", st, g_free);
    }
    return st;
}

/* Capture new output from ONE specific terminal into the shared accumulator.
 * GTK-thread only. */
static gboolean terminal_capture_pending_tee_vte(AppContext *app, VteTerminal *vte) {
    if (!app || !vte || !VTE_IS_TERMINAL(vte) || !app->aiterm_runtime.tee_accumulator)
        return FALSE;

    if (!app->sys.tee_enabled && !app->sys.autoreply_enabled)
        return FALSE;

    TeeTabState *st = tee_tab_state(vte);

    long cur_row = 0, cur_col = 0;
    vte_terminal_get_cursor_position(vte, &cur_col, &cur_row);

    if (cur_row <= st->last_processed_row)
        return FALSE;

    char *new_text = vte_terminal_get_text_range(
        vte, st->last_processed_row, 0, cur_row, cur_col, NULL, NULL, NULL);

    if (!new_text || strlen(new_text) <= 1) {
        g_free(new_text);
        return FALSE;
    }

    st->last_processed_row = cur_row;

    char *cleaned = strip_blank_lines(new_text);
    g_free(new_text);
    if (!cleaned || !*cleaned) {
        g_free(cleaned);
        return FALSE;
    }

    char *lt_pl   = g_strdup(global_app->ansi.lt_purple);
    char *cy      = g_strdup(global_app->ansi.cyan);
    char *yl      = g_strdup(global_app->ansi.yellow);
    char *gr      = g_strdup(global_app->ansi.green);
    char *red     = g_strdup(global_app->ansi.red);
    char *nml     = g_strdup(global_app->ansi.normal);

    /* Tag every capture with its source tab so output from different tabs can
     * be told apart in the accumulator, in the DB, and by the AI. */
    g_mutex_lock(&app->access.buffer_mutex);
    g_string_append_printf(app->aiterm_runtime.tee_accumulator,
                           "[tab %s]\n%s", st->name, cleaned);
    if (app->aiterm_runtime.tee_accumulator->len > 65536)   /* same cap as tee_handle_output */
        g_string_erase(app->aiterm_runtime.tee_accumulator, 0,
                       app->aiterm_runtime.tee_accumulator->len - 65536);
    gsize accumulator_len = app->aiterm_runtime.tee_accumulator->len;
    g_mutex_unlock(&app->access.buffer_mutex);

    DEBUG_PRINT("[ %sDEBUG%s ]: [%sTEE SYNC%s] %svte=[%s%p%s] captured=[%s%zu%s] bytes accumulator=[%s%zu%s] bytes%s\n",
	lt_pl, nml, cy, nml, gr,
        red, (void *)vte, gr, 
        red, strlen(cleaned), gr, 
	red, accumulator_len, gr, nml);

    g_free(lt_pl);
    g_free(cy);
    g_free(yl);
    g_free(gr);
    g_free(red);
    g_free(nml);

    g_free(cleaned);
    return TRUE;
}

/* Public entry point: capture from the currently focused tab. */
gboolean terminal_capture_pending_tee(AppContext *app) {
    if (!app || !app->gui.terminal_view || !VTE_IS_TERMINAL(app->gui.terminal_view))
        return FALSE;
    return terminal_capture_pending_tee_vte(app, VTE_TERMINAL(app->gui.terminal_view));
}

/* Name ("pts/N") of the focused tab, or NULL. Valid while the tab is open. */
const char *terminal_active_tab_name(AppContext *app) {
    if (!app || !app->gui.terminal_view || !VTE_IS_TERMINAL(app->gui.terminal_view))
        return NULL;
    return tee_tab_state(VTE_TERMINAL(app->gui.terminal_view))->name;
}

static char *terminal_capture_vte_context(VteTerminal *vte, long context_depth) {
    if (!vte || !VTE_IS_TERMINAL(vte))
        return g_strdup("No terminal output available.");

    glong cursor_column = 0;
    glong cursor_row = 0;
    vte_terminal_get_cursor_position(vte, &cursor_column, &cursor_row);
    glong start_row = cursor_row > context_depth ? cursor_row - context_depth : 0;
    char *text = vte_terminal_get_text_range(vte, start_row, 0,
                                             cursor_row, cursor_column,
                                             NULL, NULL, NULL);
    if (!text || !*text) {
        g_free(text);
        return g_strdup("No terminal output available.");
    }
    return text;
}

char *terminal_capture_context(AppContext *app) {
    if (!app || !app->gui.terminal_view || !VTE_IS_TERMINAL(app->gui.terminal_view))
        return g_strdup("None");
    return terminal_capture_vte_context(VTE_TERMINAL(app->gui.terminal_view), 1000);
}

/* Snapshot every tab while called on the GTK thread. Each tab is explicitly
 * labeled and bounded so background tabs cannot be confused with the active
 * session and a large scrollback cannot monopolize the request context. */
char *terminal_capture_tab_context_by_id(AppContext *app, const char *tab_id) {
    if (!app || !tab_id || !app->gui.notebook || !GTK_IS_NOTEBOOK(app->gui.notebook))
        return g_strdup("Targeted tab inspection unavailable: terminal notebook is unavailable.");

    GtkNotebook *notebook = GTK_NOTEBOOK(app->gui.notebook);
    gint page_count = gtk_notebook_get_n_pages(notebook);
    gint active_page = gtk_notebook_get_current_page(notebook);

    for (gint page_index = 0; page_index < page_count && page_index < MAX_TABS; page_index++) {
        TabSettings *tab = &app->tabs[page_index];
        if (g_strcmp0(tab->tab_id, tab_id) != 0)
            continue;

        const char *title = (tab->label && GTK_IS_LABEL(tab->label))
            ? gtk_label_get_text(GTK_LABEL(tab->label)) : "Terminal";
        GString *snapshot = g_string_new(NULL);
        g_string_append_printf(snapshot,
            "AITERM TARGETED TAB INSPECTION\nTAB %d of %d | id=%s | title=%s | active=%s\n"
            "This is additional evidence from one specific terminal session.\n\n",
            page_index + 1, page_count, tab_id, title ? title : "Terminal",
            page_index == active_page ? "yes" : "no");

        if (tab->vte && VTE_IS_TERMINAL(tab->vte)) {
            char *tab_text = terminal_capture_vte_context(VTE_TERMINAL(tab->vte), 1200);
            const gsize max_tab_bytes = 24000;
            gsize text_length = strlen(tab_text);
            if (text_length > max_tab_bytes) {
                const char *tail = tab_text + text_length - max_tab_bytes;
                g_string_append(snapshot, "[Earlier output omitted; showing the most recent 24000 bytes.]\n");
                g_string_append_len(snapshot, tail, max_tab_bytes);
            } else {
                g_string_append(snapshot, tab_text);
            }
            g_free(tab_text);
        } else {
            g_string_append(snapshot, "Terminal widget is not available for this tab.\n");
        }
        return g_string_free(snapshot, FALSE);
    }

    return g_strdup("Targeted tab inspection failed: the requested tab no longer exists.");
}

char *terminal_capture_all_tabs_context(AppContext *app) {
    if (!app || !app->gui.notebook || !GTK_IS_NOTEBOOK(app->gui.notebook))
        return g_strdup("AITERM multi-tab context unavailable.");

    GtkNotebook *notebook = GTK_NOTEBOOK(app->gui.notebook);
    gint page_count = gtk_notebook_get_n_pages(notebook);
    gint active_page = gtk_notebook_get_current_page(notebook);
    GString *snapshot = g_string_new("AITERM MULTI-TAB SNAPSHOT\n");

    for (gint page_index = 0; page_index < page_count && page_index < MAX_TABS; page_index++) {
        TabSettings *tab = &app->tabs[page_index];
        const char *title = (tab->label && GTK_IS_LABEL(tab->label))
            ? gtk_label_get_text(GTK_LABEL(tab->label)) : NULL;
        if (!title || !*title)
            title = "Terminal";

        g_string_append_printf(snapshot,
            "\n--- TAB %d of %d | id=%s | title=%s | active=%s ---\n",
            page_index + 1, page_count,
            tab->tab_id ? tab->tab_id : "unavailable",
            title, page_index == active_page ? "yes" : "no");

        if (tab->vte && VTE_IS_TERMINAL(tab->vte)) {
            char *tab_text = terminal_capture_vte_context(VTE_TERMINAL(tab->vte), 180);
            const gsize max_tab_bytes = 5000;
            gsize text_length = strlen(tab_text);
            if (text_length > max_tab_bytes) {
                const char *tail = tab_text + text_length - max_tab_bytes;
                g_string_append(snapshot, "[Earlier tab output omitted; showing recent tail.]\n");
                g_string_append_len(snapshot, tail, max_tab_bytes);
            } else {
                g_string_append(snapshot, tab_text);
            }
            g_free(tab_text);
        } else {
            g_string_append(snapshot, "Terminal widget is not available for this tab.\n");
        }
        g_string_append_c(snapshot, '\n');
    }

    if (page_count == 0)
        g_string_append(snapshot, "No terminal tabs are currently open.\n");
    return g_string_free(snapshot, FALSE);
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

/* One timer per terminal. It watches ITS OWN vte (held by weak ref so a closed
 * tab ends the timer) instead of whichever tab is currently focused. */
typedef struct {
    AppContext *app;
    GWeakRef    vte_ref;
} TeeTimerCtx;

static void tee_timer_ctx_free(gpointer data) {
    TeeTimerCtx *ctx = data;
    g_weak_ref_clear(&ctx->vte_ref);
    g_free(ctx);
}

static gboolean throttled_delta_check(gpointer user_data) {
    TeeTimerCtx *ctx = user_data;
    AppContext *app = ctx->app;

    GtkWidget *w = g_weak_ref_get(&ctx->vte_ref);
    if (!w) return G_SOURCE_REMOVE;              /* tab was closed */

    VteTerminal *vte = VTE_TERMINAL(w);
    TeeTabState *st = tee_tab_state(vte);

    long cur_row, cur_col;
    vte_terminal_get_cursor_position(vte, &cur_col, &cur_row);

    char *lt_pl   = g_strdup(global_app->ansi.lt_purple);
    char *cy      = g_strdup(global_app->ansi.cyan);
    char *yl      = g_strdup(global_app->ansi.yellow);
    char *gr      = g_strdup(global_app->ansi.green);
    char *red     = g_strdup(global_app->ansi.red);
    char *nml     = g_strdup(global_app->ansi.normal);

    if (cur_row != st->last_row || cur_col != st->last_col) {
        st->last_row = cur_row;
        st->last_col = cur_col;
        st->silence_ticks = 0;
    } else {
        st->silence_ticks++;

        /* ~2 seconds of silence on THIS tab */
        if (st->silence_ticks == 8) {
            if (terminal_capture_pending_tee_vte(app, vte)) {
                DEBUG_PRINT("[ %sDEBUG%s ]: [%sTEE ACCUMULATOR%s] %sFlushing tee data (vte=%s%p%s)%s\n", 
		    lt_pl, nml, cy, nml, gr,
		    red, (void *)vte, gr, nml);

                tee_flush_timed(app);
            }
        }
    }

    g_free(lt_pl);
    g_free(cy);
    g_free(yl);
    g_free(gr);
    g_free(red);
    g_free(nml);

    g_object_unref(w);
    return G_SOURCE_CONTINUE;
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
    GtkWidget *new_term = vte_terminal_new();
    tee_tab_state(VTE_TERMINAL(new_term));   /* zeroed per-tab state */
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
    TeeTimerCtx *tctx = g_new0(TeeTimerCtx, 1);
    tctx->app = app;
    g_weak_ref_init(&tctx->vte_ref, new_term);
    g_timeout_add_full(G_PRIORITY_DEFAULT, 250, throttled_delta_check, tctx, tee_timer_ctx_free);

    vte_terminal_spawn_async(VTE_TERMINAL(new_term),
        VTE_PTY_DEFAULT, NULL, (char *[]){"/bin/bash", NULL},
        NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, -1, NULL, NULL, NULL);
    vte_terminal_set_scrollback_lines(VTE_TERMINAL(new_term), 10000);

    // Save reference to terminal view in app struct
    app->gui.terminal_view = new_term;

    // Return the scrolled window container to be packed into the notebook tab
    return scrolled_window;
}

