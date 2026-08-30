// Part of the aiterm project
// gui.c
// C Program file for gui functions
// By: Peter Talbott
// With assistance from Gemini and OpenAI
// May 2026 - August 2026

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <json-c/json.h>
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <mariadb/mysql.h>

#include "utils.h"
#include "gui.h"
#include "policy_dao.h"
#include "commands.h"
#include "openai.h"
#include "crypto.h"
#include "update.h"
#include "terminal.h"
#include "menu.h"
#include "gemini.h"
#include "autoexec.h"

// Initialize local command cache
void init_local_cmd_history(AppContext *app) {
    DEBUG_PRINT("[DEBUG]: [Local Command Cache] Initializing\n");
    app->local.cmd_history = g_ptr_array_new_with_free_func(g_free);
    app->local.history_index = 0;
    app->local.history_temp_entry = NULL;
}

// Initialize token metric tracker variables
void init_token_tracker(AppContext *app) {
    DEBUG_PRINT("[DEBUG]: [Token Tracker] Initializing baseline metrics\n");
    app->tokens.bar = NULL;     // Safe pointer tracking before widget creation
    app->tokens.current = 0;    // Start session at 0 tokens
    app->tokens.max = 1000000;   // Default 1M token budget (e.g., Gemini 1.5 Pro)
    app->tokens.last = 0;       // No transactions processed yet
}

gboolean on_entry_key_press(GtkWidget *widget, GdkEventKey *event, gpointer user_data) {
    AppContext *app = (AppContext *)user_data;

    // Safety fallback initialization check
    if (!app->local.cmd_history) {
        app->local.cmd_history = g_ptr_array_new_with_free_func(g_free);
        app->local.history_index = 0;
    }

    int history_len = app->local.cmd_history->len;

    // --- UP ARROW pressed ---
    if (event->keyval == GDK_KEY_Up) {
        if (app->local.history_index > 0) {
            // Save the uncommitted text if we are leaving the bottom line
            if (app->local.history_index == history_len) {
                g_free(app->local.history_temp_entry);
                app->local.history_temp_entry = g_strdup(gtk_entry_get_text(GTK_ENTRY(app->gui.entry)));
            }

            app->local.history_index--;
            const char *past_cmd = g_ptr_array_index(app->local.cmd_history, app->local.history_index);

            gtk_entry_set_text(GTK_ENTRY(app->gui.entry), past_cmd);
            gtk_editable_set_position(GTK_EDITABLE(app->gui.entry), -1); // Move cursor to end
        }
        return TRUE; // Suppress default GTK event routing
    }

    // --- DOWN ARROW pressed ---
    if (event->keyval == GDK_KEY_Down) {
        if (app->local.history_index < history_len) {
            app->local.history_index++;

            if (app->local.history_index == history_len) {
                // Restore what the user was typing originally
                gtk_entry_set_text(GTK_ENTRY(app->gui.entry), app->local.history_temp_entry ? app->local.history_temp_entry : "");
                g_free(app->local.history_temp_entry);
                app->local.history_temp_entry = NULL;
            } else {
                const char *past_cmd = g_ptr_array_index(app->local.cmd_history, app->local.history_index);
                gtk_entry_set_text(GTK_ENTRY(app->gui.entry), past_cmd);
            }
            gtk_editable_set_position(GTK_EDITABLE(app->gui.entry), -1); // Move cursor to end
        }
        return TRUE; // Suppress default GTK event routing
    }

    return FALSE; // Let letters, backspaces, and shortcuts through safely
}

void setup_tags(GtkTextBuffer *buffer) {
    GtkTextTagTable *table = gtk_text_buffer_get_tag_table(buffer);

    // BLUE tag for you
    if (!gtk_text_tag_table_lookup(table, "user_tag")) {
        gtk_text_buffer_create_tag(buffer, "user_tag",
        "foreground", "#007BFF",
        "weight", PANGO_WEIGHT_BOLD, NULL);
    }
    // GREEN tag for the AI
    if (!gtk_text_tag_table_lookup(table, "ai_tag")) {
        gtk_text_buffer_create_tag(buffer, "ai_tag",
        "foreground", "#28A745",
        "weight", PANGO_WEIGHT_BOLD, NULL);
    }
    // LIGHT RED for system messages
    if (!gtk_text_tag_table_lookup(table, "cmd_tag")) {
        gtk_text_buffer_create_tag(buffer, "cmd_tag",
        "foreground", "#E74C3C",
        "weight", PANGO_WEIGHT_BOLD, NULL);
    }
    // GREY tag for the actual message text
    if (!gtk_text_tag_table_lookup(table, "body_tag")) {
        gtk_text_buffer_create_tag(buffer, "body_tag",
        "foreground", "#E5C07B", NULL);
    }
    if (!gtk_text_tag_table_lookup(table, "system_tag")) {
        gtk_text_buffer_create_tag(buffer, "system_tag",
	"foreground", "#FF3B30",
	"weight", PANGO_WEIGHT_BOLD, NULL);
    }
    if (!gtk_text_tag_table_lookup(table, "green_tag")) {
        gtk_text_buffer_create_tag(buffer, "green_tag",
	"foreground", "#32D74B",
	"weight", PANGO_WEIGHT_BOLD, NULL);
    }
}

void apply_block_cursor_to_input(GtkWidget *entry) {
    GtkCssProvider *provider = gtk_css_provider_new();

    const gchar *css =
        "entry { "
        "  -GtkWidget-cursor-aspect-ratio: 0.5; "
        "  caret-color: #00FF00; "
        "}";

    gtk_css_provider_load_from_data(provider, css, -1, NULL);

    GtkStyleContext *context = gtk_widget_get_style_context(entry);
    gtk_style_context_add_provider(context,
                                   GTK_STYLE_PROVIDER(provider),
                                   GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

    g_object_unref(provider);
}

// 0.8.3 FIX: Enhanced CSS for GtkNotebook (Tab Bar)
// Targets internal node names (header, stack, tab) to override
// default white themes when running as root/Adwaita-light.
// Revised 0.9.0
void apply_custom_theme() {
    GtkCssProvider *provider = gtk_css_provider_new();
    const char *css =
        /* 1. Main UI layout elements remain transparent for your AI pane */
        "window, box, grid { background-color: transparent; }"
        "menubar, menu, menuitem { background-color: #000000; color: #ffffff; }"

        /* 2. SPECIFICALLY TARGET THE SESSION DIALOG BY CLASS */
        ".session-dialog, .session-dialog box, .session-dialog grid { background-color: #141414; color: #ffffff; }"
        ".session-dialog action-area { background-color: #1a1a1a; }"

        /* 3. Buttons inside the session manager dialog */
        ".session-dialog button { background-color: #222222; border: 1px solid #444444; padding: 4px 8px; }"

        /* Orange/Amber text by default (Change to #ff3b30 if you want pure Red) */
        ".session-dialog button label { color: #ff9f0a; }"

        /* Hover states - Turns green */
        ".session-dialog button:hover { background-color: #333333; border-color: #00FF00; }"
        ".session-dialog button:hover label { color: #00FF00; }"

        /* Active/Clicked state - Stays green but goes bold */
        ".session-dialog button:active { background-color: #000000; }"
        ".session-dialog button:active label { color: #00FF00; font-weight: bold; }"

        /* 4. Lists and Trees inside the session manager dialog */
        ".session-dialog treeview, .session-dialog list { background-color: #1a1a1a; color: #ffffff; }"
        ".session-dialog treeview:selected, .session-dialog list row:selected { background-color: #00FF00; color: #000000; font-weight: bold; }"

        /* 5. Main Notebook container and tabs */
        "notebook { background-color: rgba(20, 20, 20, 0.6); border: none; }"
        "notebook header { background-color: rgba(30, 30, 30, 0.8); border-bottom: 1px solid #333333; padding: 2px; }"
        "notebook stack { background-color: transparent; }"

        "notebook header tabs tab { "
        "  background-color: rgba(45, 45, 45, 0.5); "
        "  color: #aaaaaa; "
        "  padding: 6px 12px; "
        "  border: 1px solid #333333; "
        "  margin: 0 2px; "
        "}"

        "notebook header tabs tab:checked { "
        "  background-color: #000000; "
        "  color: #00FF00; "
        "  font-weight: bold; "
        "  border-bottom: 2px solid #00FF00; "
        "}"

        // Aded 0.9.6
        /* Custom close button styling on GtkNotebook tabs */
        "#tab-close-btn { "
        "   padding: 0px 3px; "
        "   margin: 0px; "
        "   border: none; "
        "   background: transparent; "
        "   color: #aaaaaa; "
        "   font-weight: bold; "
        "   font-size: 11pt; "
        "}"
        "#tab-close-btn:hover { "
        "   color: #ff3b30; "
        "   background-color: rgba(255, 59, 48, 0.2); "
        "   border-radius: 3px; "
        "}"

        /* 6. Token Tracker Bar Customization */
        "#token-bar text { "
        "   color: #00FF00; "
        "   font-weight: bold; "
        "   font-size: 9pt; "
        "}"
        "#token-bar trough { "
        "   background-color: #121212; "
        "   border: 1px solid #333333; "
        "}"
        "#token-bar progress { "
        "   background-color: #0f380f; "
        "}"

        /* SNMP Ticker Customization */
        "#snmp-ticker { "
        "   font-family: monospace; "
        "   font-size: 9pt; "
        "   color: #00FF00; "
        "   background-color: transparent; "
        "   padding: 2px 4px; "
        "}"
        /* 7. Text inputs and global typography fallback */
        "textview { background-color: transparent; color: #dcdcdc; font-family: monospace; font-size: 10pt; }"
        "entry { background-color: #1a1a1a; color: #ffffff; border: 1px solid #333333; }"
        "label { color: #aaaaaa; }";

    gtk_css_provider_load_from_data(provider, css, -1, NULL);
    gtk_style_context_add_provider_for_screen(
        gdk_screen_get_default(),
        GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION
    );
    g_object_unref(provider);
}

// Global window keystroke interceptor handling focus cycles
static gboolean on_window_key_press(GtkWidget *widget, GdkEventKey *event, gpointer data) {
    AppContext *app = (AppContext *)data;

    if ((event->state & GDK_CONTROL_MASK) && event->keyval == GDK_KEY_Tab) {
        if (gtk_widget_has_focus(app->gui.entry)) {
            if (app->gui.terminal_view) gtk_widget_grab_focus(app->gui.terminal_view);
            DEBUG_PRINT("[DEBUG]: Focus: Active Terminal Tab\n");
        } else if (app->gui.terminal_view && gtk_widget_has_focus(app->gui.terminal_view)) {
            gtk_widget_grab_focus(app->gui.gemini_view);
            DEBUG_PRINT("[DEBUG]: Focus: AI View\n");
        } else {
            gtk_widget_grab_focus(app->gui.entry);
            DEBUG_PRINT("[DEBUG]: Focus: Input\n");
        }
        return TRUE;
    }
    return FALSE;
}

// NEW: Lifecycle tracking handler syncing app->gui.terminal_view during tab adjustments
// Updated 0.9.4
void on_tab_changed(GtkNotebook *notebook, GtkWidget *page, guint page_num, gpointer data) {
    AppContext *app = (AppContext *)data;

    // The page added inside our scroll windows is the scrolled window container.
    // We dig out its child to grab the pure active VteTerminal instance.
    GtkWidget *scrolled_win = page;
    GtkWidget *terminal = gtk_bin_get_child(GTK_BIN(scrolled_win));

    if (VTE_IS_TERMINAL(terminal)) {
        app->gui.terminal_view = terminal;
        app->ui.vterm = terminal; // 0.9.4 addition
        DEBUG_PRINT("[DEBUG]: [TAB_CHANGED]: Focused tab shifted to Page #%d (Widget: %p)\n", page_num, (void*)terminal);

        // Push current fonts and transparency settings dynamically down to the new pane
        apply_visual_settings(app);
    }
}

// Total rework on 0.9.6
// NEW: Modular spawner adding fully functional isolated terminals into the notebook array
void add_terminal_tab(AppContext *app) {
    static int tab_counter = 0;

    // Safety check against exceeding MAX_TABS threshold
    gint current_count = gtk_notebook_get_n_pages(GTK_NOTEBOOK(app->gui.notebook));
    if (current_count >= MAX_TABS) {
        g_warning("Maximum tab threshold (%d) reached.", MAX_TABS);
        return;
    }

    tab_counter++;
    DEBUG_PRINT("[DEBUG]: [Tabs] Opening new tab instance #%d\n", tab_counter);

    // 1. Build infrastructure terminal elements
    GtkWidget *term_scroll = gtk_scrolled_window_new(NULL, NULL);
    GtkWidget *new_terminal = setup_terminal(app); // Spawns VTE + execs shell hook
    gtk_container_add(GTK_CONTAINER(term_scroll), new_terminal);

    // 2. Build composite Tab Header Box (Label + Close Button)
    GtkWidget *tab_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);

    char tab_label_text[32];
    snprintf(tab_label_text, sizeof(tab_label_text), "pts/%d", tab_counter);
    GtkWidget *tab_label = gtk_label_new(tab_label_text);

    // Create custom close 'X' button
    GtkWidget *close_btn = gtk_button_new_with_label("×");
    gtk_widget_set_name(close_btn, "tab-close-btn"); // CSS targeting handle
    gtk_button_set_relief(GTK_BUTTON(close_btn), GTK_RELIEF_NONE);
    gtk_widget_set_focus_on_click(close_btn, FALSE);

    // Pack label and button into header box
    gtk_box_pack_start(GTK_BOX(tab_box), tab_label, TRUE, TRUE, 0);
    gtk_box_pack_end(GTK_BOX(tab_box), close_btn, FALSE, FALSE, 0);
    gtk_widget_show_all(tab_box);

    // Attach child container reference to the close button for instant lookup
    g_object_set_data(G_OBJECT(close_btn), "tab-page-child", term_scroll);

    // Connect close button click event
    g_signal_connect(close_btn, "clicked", G_CALLBACK(on_tab_close_clicked), app);

    // 3. Inject composite header page into the notebook layout
    gint index = gtk_notebook_append_page(GTK_NOTEBOOK(app->gui.notebook), term_scroll, tab_box);
    gtk_widget_show_all(term_scroll);

    // 4. Update internal TabSettings array state in AppContext
    if (index >= 0 && index < MAX_TABS) {
        app->tabs[index].tab_label_box = tab_box;
        app->tabs[index].label = tab_label;
        app->tabs[index].close_btn = close_btn;
        app->tabs[index].is_active = TRUE;
        app->tabs[index].close_tab_button_enabled = TRUE;
        app->tabs[index].double_click_new_tab = TRUE;
    }

    // 5. Ensure window interceptor handles input routing on the new console layer
    g_signal_connect(new_terminal, "key-press-event", G_CALLBACK(on_window_key_press), app);

    // 6. Jump focus directly to our newly allocated workspace
    gtk_notebook_set_current_page(GTK_NOTEBOOK(app->gui.notebook), index);
    gtk_widget_grab_focus(new_terminal);
}

// Added 0.9.6
// Callback triggered when the 'X' button on a tab header is clicked
void on_tab_close_clicked(GtkButton *button, gpointer user_data) {
    AppContext *app = (AppContext *)user_data;
    if (!app || !app->gui.notebook) return;

    // Retrieve the associated terminal container widget attached to this close button
    GtkWidget *term_scroll = GTK_WIDGET(g_object_get_data(G_OBJECT(button), "tab-page-child"));
    if (!term_scroll) return;

    // Dynamically resolve the live page index in the notebook (0-based)
    gint page_num = gtk_notebook_page_num(GTK_NOTEBOOK(app->gui.notebook), term_scroll);
    if (page_num != -1) {
        gint total_pages = gtk_notebook_get_n_pages(GTK_NOTEBOOK(app->gui.notebook));

        DEBUG_PRINT("[DEBUG]: [Tabs] Closing GTK page index #%d (Total open: %d)\n", page_num, total_pages);

        // 1. Remove the page from the GTK Notebook container
        gtk_notebook_remove_page(GTK_NOTEBOOK(app->gui.notebook), page_num);

        // 2. Shift app->tabs array elements left to keep tracking in sync with GTK indices
        for (int i = page_num; i < total_pages - 1 && i < MAX_TABS - 1; i++) {
            app->tabs[i] = app->tabs[i + 1];
        }

        // Clear the newly vacated last slot
        if (total_pages - 1 < MAX_TABS) {
            memset(&app->tabs[total_pages - 1], 0, sizeof(TabSettings));
        }

        // 3. Safety net: If all tabs are closed, spawn a fresh default terminal tab
        if (gtk_notebook_get_n_pages(GTK_NOTEBOOK(app->gui.notebook)) == 0) {
            add_terminal_tab(app);
        }
    }
}

void on_upload_clicked(GtkButton *button, gpointer data) {
    AppContext *app = (AppContext *)data;
    GtkWidget *dialog = gtk_file_chooser_dialog_new("Analyze File",
                                                    GTK_WINDOW(app->gui.window),
                                                    GTK_FILE_CHOOSER_ACTION_OPEN,
                                                    "_Cancel", GTK_RESPONSE_CANCEL,
                                                    "_Open", GTK_RESPONSE_ACCEPT,
                                                    NULL);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        char *file_text = read_file_to_string(filename);

        if (file_text) {
            char *prompt = g_strdup_printf("FILE ANALYSIS (%s):\n\n%s", filename, file_text);

            if (!g_atomic_int_compare_and_exchange(&app->sys.is_processing, 0, 1)) {
                write_to_ai_pane(app, "System: ", "AI is already busy. File analysis deferred.", "cmd_tag", "cmd_tag");
                g_free(prompt);
                g_free(file_text);
                g_free(filename);
                gtk_widget_destroy(dialog);
                return;
            }

            AIThreadData *td = g_malloc0(sizeof(AIThreadData));
            td->app = app;
            td->prompt = prompt;
            td->terminal_context = terminal_capture_context(app);
            g_thread_unref(g_thread_new("ai_worker", (GThreadFunc)ai_thread_func, td));

            append_ai_text(app, "System: ", "system_tag");
            append_ai_text(app, " Uploading file for analysis...\n", "body_tag");

            free(file_text);
        }
        g_free(filename);
    }
    gtk_widget_destroy(dialog);
}

gboolean on_notebook_button_press(GtkWidget *widget, GdkEventButton *event, gpointer user_data) {
    AppContext *app_ctx = (AppContext *)user_data;

    if (!app_ctx) {
        return FALSE;
    }

    // Check for a left double-click (GDK_2BUTTON_PRESS + Button 1)
    if (event->type == GDK_2BUTTON_PRESS && event->button == GDK_BUTTON_PRIMARY) {
        
        // Call your existing helper function to allocate and attach a new tab
        add_terminal_tab(app_ctx);

        // Return TRUE to stop further event propagation
        return TRUE;
    }

    // Return FALSE to allow standard tab switching/dragging behavior on single clicks
    return FALSE;
}

void on_copy_clicked(GtkButton *button, gpointer data) {
    AppContext *app = (AppContext *)data;
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(app->gui.gemini_view));

    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(buffer, &start, &end);
    char *full_text = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);

    if (full_text && strlen(full_text) > 0) {
        GtkClipboard *clipboard = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
        gtk_clipboard_set_text(clipboard, full_text, -1);
        update_status_label(app, "History copied to clipboard");
    }
    g_free(full_text);
}

void set_icon(AppContext *app) {
    GError *icon_error = NULL;
    GdkPixbuf *icon = gdk_pixbuf_new_from_resource("/com/aiterm/app/aiterm-icon.png", &icon_error);

    if (icon) {
        gtk_window_set_icon(GTK_WINDOW(app->gui.window), icon);
        g_object_unref(icon);
        DEBUG_PRINT("[DEBUG]: [Embedded icon]: loaded from GResource successfully.\n");
    } else {
        g_warning("Could not load embedded icon: %s", icon_error->message);
        if (icon_error) g_error_free(icon_error);
    }
}

gboolean scroll_to_bottom_idle(gpointer data) {
    AppContext *app = (AppContext *)data;
    GtkWidget *parent = gtk_widget_get_parent(app->gui.gemini_view);

    if (GTK_IS_SCROLLED_WINDOW(parent)) {
        GtkAdjustment *adj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(parent));
        gtk_adjustment_set_value(adj, gtk_adjustment_get_upper(adj));
    }
    return FALSE; // Remove the idle source
}

// The listener for the buffer changed signal
void on_buffer_changed_scroll(GtkTextBuffer *buffer, gpointer data) {
    // We defer the scroll slightly to ensure the TextView has updated its layout
    g_idle_add(scroll_to_bottom_idle, data);
}

/*
 * SNMP ticker implementation
 *
 * IMPORTANT: Do not use GtkLabel for the scrolling payload.  A GtkLabel's
 * natural size is based on its complete text.  Our SNMP payload can contain
 * dozens of targets and very long values (Cisco IOS descriptions are a good
 * example), which can make GTK request a widget tens of thousands of pixels
 * wide.  That was producing:
 *   Gdk-WARNING: Native Windows wider or taller than 32767 pixels are not supported
 *
 * The ticker is therefore a fixed-size GtkDrawingArea.  Only the visible
 * portion is rendered, so the full payload never becomes the widget's natural
 * width and GTK never has to allocate a giant native window.
 */
static gboolean snmp_ticker_draw(GtkWidget *widget, cairo_t *cr, gpointer user_data) {
    AppContext *app = (AppContext *)user_data;
    if (!app) return FALSE;

    if (!app->sys.snmp_ticker_enabled) {
        // You could draw with a different color or style
        GtkStyleContext *context = gtk_widget_get_style_context(widget);
        gtk_render_background(context, cr, 0, 0, 
                              gtk_widget_get_allocated_width(widget),
                              gtk_widget_get_allocated_height(widget));
    }

    GtkAllocation allocation;
    gtk_widget_get_allocation(widget, &allocation);
    if (allocation.width <= 0 || allocation.height <= 0) return FALSE;

    int width_px = allocation.width;
    int height_px = allocation.height;

    /*
     * The old draw path converted the ENTIRE ticker payload from UTF-8 to
     * UCS-4 on every animation frame.  With a large SNMP payload that meant
     * repeatedly allocating and walking tens of thousands of characters from
     * the GTK main thread.  The ticker is animated at 150 ms, so this could
     * easily dominate CPU usage.
     *
     * The payload is now converted once, when it changes, and the draw path
     * only walks the visible window.  This is deliberately kept on the GTK
     * thread, so no additional locking is required for the cached fields.
     */
    static const char idle_text[] = "SNMP: Idle";
    const gunichar *chars = app->gui.snmp_ticker_chars;
    glong len = app->gui.snmp_ticker_len;

    gunichar idle_chars[16] = { 0 };
    if (!chars || len <= 0) {
        gunichar *idle_utf32 = g_utf8_to_ucs4_fast(idle_text, -1, NULL);
        if (!idle_utf32) return FALSE;
        len = (glong)g_utf8_strlen(idle_text, -1);
        if (len > (glong)(G_N_ELEMENTS(idle_chars) - 1))
            len = G_N_ELEMENTS(idle_chars) - 1;
        memcpy(idle_chars, idle_utf32, (size_t)len * sizeof(gunichar));
        g_free(idle_utf32);
        chars = idle_chars;
        app->gui.snmp_ticker_offset = 0;
    }

    PangoLayout *measure = gtk_widget_create_pango_layout(widget, "M");
    int char_width = 0;
    int char_height = 0;
    pango_layout_get_pixel_size(measure, &char_width, &char_height);
    g_object_unref(measure);

    if (char_width < 1) char_width = 8;
    if (char_height < 1) char_height = 14;

    int window_size = (width_px > 4) ? (width_px - 4) / char_width : 1;
    if (window_size < 1) window_size = 1;

    if ((glong)app->gui.snmp_ticker_offset >= len)
        app->gui.snmp_ticker_offset = 0;

    GString *visible = g_string_sized_new((gsize)window_size * 4 + 1);
    for (int i = 0; i < window_size; i++) {
        gunichar c = chars[(app->gui.snmp_ticker_offset + (size_t)i) % (size_t)len];
        g_string_append_unichar(visible, c);
    }

    PangoLayout *layout = gtk_widget_create_pango_layout(widget, visible->str);
    pango_layout_set_single_paragraph_mode(layout, TRUE);
    pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_NONE);

    /* Never let the ticker draw outside its allocated pane. */
    cairo_save(cr);
    cairo_rectangle(cr, 0, 0, width_px, height_px);
    cairo_clip(cr);

    int text_width = 0, text_height = 0;
    pango_layout_get_pixel_size(layout, &text_width, &text_height);
    int y = (height_px - text_height) / 2;
    if (y < 0) y = 0;

    gtk_render_layout(gtk_widget_get_style_context(widget), cr, 2, y, layout);
    cairo_restore(cr);

    g_object_unref(layout);
    g_string_free(visible, TRUE);
    /* chars is cached in app->gui and must not be freed here. */

    return FALSE;
}

GtkWidget *create_snmp_ticker(AppContext *app) {
    GtkWidget *ticker = gtk_drawing_area_new();
    app->gui.snmp_ticker_label = ticker;

    gtk_widget_set_name(ticker, "snmp-ticker");
    gtk_widget_set_hexpand(ticker, TRUE);
    gtk_widget_set_halign(ticker, GTK_ALIGN_FILL);
    gtk_widget_set_vexpand(ticker, FALSE);
    gtk_widget_set_valign(ticker, GTK_ALIGN_CENTER);

    /* A drawing area has no natural width based on the ticker payload. */
    gtk_widget_set_size_request(ticker, 1, 22);
    g_signal_connect(ticker, "draw", G_CALLBACK(snmp_ticker_draw), app);

    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(provider,
        "#snmp-ticker {\n"
        "    font-family: 'Monospace';\n"
        "    font-size: 9pt;\n"
        "    color: #00FF00;\n"
        "    background-color: transparent;\n"
        "}\n", -1, NULL);

    GtkStyleContext *context = gtk_widget_get_style_context(ticker);
    gtk_style_context_add_provider(context, GTK_STYLE_PROVIDER(provider),
                                   GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);

    return ticker;
}

gboolean update_snmp_ticker_scroll(gpointer user_data) {
    AppContext *app = (AppContext *)user_data;

    if (!app || !app->gui.snmp_ticker_label) return FALSE;

    // ADD THIS CHECK HERE:
    if (!app->sys.snmp_ticker_enabled) {
        // Set a static "disabled" message
        const char *disabled_text = "SNMP: Ticker Disabled";
        
        // Update payload to show disabled message
        g_free(app->gui.snmp_ticker_text);
        app->gui.snmp_ticker_text = g_strdup(disabled_text);
        
        // Convert to UCS-4 for drawing
        g_free(app->gui.snmp_ticker_chars);
        app->gui.snmp_ticker_chars = g_utf8_to_ucs4_fast(
            app->gui.snmp_ticker_text, -1, &app->gui.snmp_ticker_len);
        
        app->gui.snmp_ticker_offset = 0;
        app->aiterm_runtime.ticker_completed = TRUE;
        
        // Force redraw with disabled message
        gtk_widget_queue_draw(app->gui.snmp_ticker_label);
        
        // Return TRUE to keep timer running, but show disabled state
        return TRUE;
    }

    const char *text = app->gui.snmp_ticker_text;
    if (!text || !*text) {
        app->gui.snmp_ticker_offset = 0;
        app->aiterm_runtime.ticker_completed = TRUE;
        gtk_widget_queue_draw(app->gui.snmp_ticker_label);
        return TRUE;
    }

    glong len = g_utf8_strlen(text, -1);
    if (len <= 0) {
        app->gui.snmp_ticker_offset = 0;
        app->aiterm_runtime.ticker_completed = TRUE;
        gtk_widget_queue_draw(app->gui.snmp_ticker_label);
        return TRUE;
    }

    if ((glong)app->gui.snmp_ticker_offset >= len)
        app->gui.snmp_ticker_offset = 0;

    /* Draw the current position first, then advance.  Once the offset has
     * traversed the complete payload, mark the ticker complete.  The next
     * SNMP poll may replace the payload only after this flag becomes TRUE. */
    gtk_widget_queue_draw(app->gui.snmp_ticker_label);

    app->gui.snmp_ticker_offset++;
    if ((glong)app->gui.snmp_ticker_offset >= len) {
        app->gui.snmp_ticker_offset = 0;
        app->aiterm_runtime.ticker_completed = TRUE;
        DEBUG_PRINT("[DEBUG]: [SNMP Ticker] Full payload completed one pass. Ready for next poll.\n");
    }

    return TRUE;
}

void update_snmp_ticker_payload(AppContext *app, const char *payload_summary) {
    if (!app) return;

    // Don't update payload if ticker is disabled
    if (!app->sys.snmp_ticker_enabled) {
        DEBUG_PRINT("[DEBUG]: [SNMP Ticker] Ignoring payload update - ticker disabled\n");
        return;
    }

    /* A new payload starts a new complete ticker pass.  The SNMP poller
     * must not replace the payload while the previous one is still scrolling. */
    app->aiterm_runtime.ticker_completed = FALSE;

    /* Payload ownership lives entirely on the GTK/main thread. */
    g_free(app->gui.snmp_ticker_text);
    app->gui.snmp_ticker_text = NULL;
    g_free(app->gui.snmp_ticker_chars);
    app->gui.snmp_ticker_chars = NULL;
    app->gui.snmp_ticker_len = 0;

    GString *ticker = g_string_new("  [SNMP PAYLOAD]: ");
    g_string_append(ticker, payload_summary ? payload_summary : "IDLE");

    /* A few spaces create a clean visual break before the loop.  We do not
     * size this from the payload and we never put the payload into a widget's
     * natural-size calculation. */
    g_string_append(ticker, "     ");

    app->gui.snmp_ticker_text = g_string_free(ticker, FALSE);

    /* Convert once per payload update instead of once per animation frame. */
    app->gui.snmp_ticker_chars = g_utf8_to_ucs4_fast(
        app->gui.snmp_ticker_text, -1, &app->gui.snmp_ticker_len);
    if (!app->gui.snmp_ticker_chars) {
        app->gui.snmp_ticker_len = 0;
    }

    app->gui.snmp_ticker_offset = 0;

    if (app->gui.snmp_ticker_label)
        gtk_widget_queue_draw(app->gui.snmp_ticker_label);

    DEBUG_PRINT("[DEBUG]: [SNMP Ticker] Payload length=%zu chars=%zu\n",
                strlen(app->gui.snmp_ticker_text),
                (size_t)g_utf8_strlen(app->gui.snmp_ticker_text, -1));
}

// Self explainatory!! Totally Revised 0.9.4
void setup_gui(AppContext *app) {
    apply_custom_theme();

    // 1. Create Window Base Framework
    app->gui.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    app->ui.window = app->gui.window;
    g_signal_connect_after(app->gui.window, "key-press-event", G_CALLBACK(on_window_key_press), app);
    set_icon(app);

    GdkScreen *screen = gtk_window_get_screen(GTK_WINDOW(app->gui.window));
    GdkVisual *visual = gdk_screen_get_rgba_visual(screen);
    if (visual) {
        gtk_widget_set_visual(app->gui.window, visual);
    }
    gtk_widget_set_app_paintable(app->gui.window, TRUE);

    // Updated 0.9.7-delta Setting WM_ROLE and WM_CLASS
    gtk_window_set_title(GTK_WINDOW(app->gui.window), "AI-Term C/GTK Edition");
    gtk_window_set_role(GTK_WINDOW(app->gui.window), AITERM_WM_ROLE);
    gtk_window_set_wmclass(GTK_WINDOW(app->gui.window), AITERM_WM_CLASS, "Aiterm");
    DEBUG_PRINT("[DEBUG]: [Setup Gui] Set Window Title, Role and wmclass\n");

    gtk_window_set_default_size(GTK_WINDOW(app->gui.window), 1000, 600);
    g_signal_connect(app->gui.window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    // 2. Primary Structural Box Alignment
    GtkWidget *main_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_add(GTK_CONTAINER(app->gui.window), main_vbox);

    GtkWidget *menubar = create_menu_bar(app);
    gtk_box_pack_start(GTK_BOX(main_vbox), menubar, FALSE, FALSE, 0);

    GtkWidget *paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(main_vbox), paned, TRUE, TRUE, 0);

    // --- LEFT PANE UPGRADE: Dynamic GtkNotebook Container Setup ---
    app->gui.notebook = gtk_notebook_new();
    gtk_notebook_set_tab_pos(GTK_NOTEBOOK(app->gui.notebook), GTK_POS_TOP);
    gtk_notebook_set_scrollable(GTK_NOTEBOOK(app->gui.notebook), TRUE);

    // Ensure the notebook widget receives button press events
    gtk_widget_add_events(app->gui.notebook, GDK_BUTTON_PRESS_MASK);

    // Connect the button-press-event to the notebook for double-click tab creation
    g_signal_connect(app->gui.notebook, "button-press-event", G_CALLBACK(on_notebook_button_press), app);

    // Connect tracker handler ensuring app->gui.terminal_view shifts variables on page selection flips
    g_signal_connect(app->gui.notebook, "switch-page", G_CALLBACK(on_tab_changed), app);
    gtk_paned_pack1(GTK_PANED(paned), app->gui.notebook, TRUE, FALSE);

    // Allocate our initial bootup console instance inside the array matrix
    add_terminal_tab(app);
    
    // --- RIGHT PANE (AI History Console View with Token Tracker Bar) ---
    GtkWidget *right_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);

    // Instantiate and pack the token bar at the top of the right pane layout
    app->tokens.bar = gtk_progress_bar_new();
    gtk_widget_set_name(app->tokens.bar, "token-bar");
    gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(app->tokens.bar), TRUE);

    char initial_text[128];
    snprintf(initial_text, sizeof(initial_text),
             "Current Tokens: %ld \t Last AI Process Used: %ld Tokens",
             app->tokens.current, app->tokens.last);
    gtk_progress_bar_set_text(GTK_PROGRESS_BAR(app->tokens.bar), initial_text);

    gtk_box_pack_start(GTK_BOX(right_vbox), app->tokens.bar, FALSE, FALSE, 2);

    // SNMP Ticker Label
    // Use the helper so the ticker receives its CSS/theme setup in one place.
    create_snmp_ticker(app);
    gtk_box_pack_start(GTK_BOX(right_vbox), app->gui.snmp_ticker_label, FALSE, FALSE, 2);

    // Start the scrolling animation.  The payload is replaced by the SNMP
    // poll completion callback, while this timer only handles animation.
    app->gui.snmp_ticker_timer_id = g_timeout_add(150, update_snmp_ticker_scroll, app);
    g_source_set_name_by_id(app->gui.snmp_ticker_timer_id, "aiterm-snmp-ticker");

    // 3. AI Text View Container
    GtkWidget *gem_scroll = gtk_scrolled_window_new(NULL, NULL);
    app->gui.gemini_view = gtk_text_view_new();
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(app->gui.gemini_view));
    setup_tags(buffer);

    g_signal_connect(buffer, "changed", G_CALLBACK(on_buffer_changed_scroll), app);

    gtk_text_view_set_editable(GTK_TEXT_VIEW(app->gui.gemini_view), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(app->gui.gemini_view), GTK_WRAP_WORD);
    gtk_container_add(GTK_CONTAINER(gem_scroll), app->gui.gemini_view);

    // Pack the text scrolling area underneath the token bar and SNMP ticker
    gtk_box_pack_start(GTK_BOX(right_vbox), gem_scroll, TRUE, TRUE, 0);


    // Pack the complete right vertical stack into the right side of the split pane
    gtk_paned_pack2(GTK_PANED(paned), right_vbox, TRUE, FALSE);
    gtk_paned_set_position(GTK_PANED(paned), 550);

    app->gui.ai_css_provider = gtk_css_provider_new();
    gtk_style_context_add_provider(
        gtk_widget_get_style_context(app->gui.gemini_view),
        GTK_STYLE_PROVIDER(app->gui.ai_css_provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION
    );

    // 3. BOTTOM UTILITY INTERFACES
    GtkWidget *bottom_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(main_vbox), bottom_hbox, FALSE, FALSE, 5);

    app->gui.entry = gtk_entry_new();
    apply_block_cursor_to_input(app->gui.entry);
    gtk_entry_set_placeholder_text(GTK_ENTRY(app->gui.entry), "Ask AI...");
    gtk_box_pack_start(GTK_BOX(bottom_hbox), app->gui.entry, TRUE, TRUE, 5);

    GtkWidget *copy_btn = gtk_button_new_from_icon_name("edit-copy", GTK_ICON_SIZE_BUTTON);
    gtk_widget_set_tooltip_text(copy_btn, "Copy AI History");
    g_signal_connect(copy_btn, "clicked", G_CALLBACK(on_copy_clicked), app);
    gtk_box_pack_start(GTK_BOX(bottom_hbox), copy_btn, FALSE, FALSE, 5);

    extern void on_input_activate(GtkEntry *entry, gpointer data);
    g_signal_connect(app->gui.entry, "activate", G_CALLBACK(on_input_activate), app);
    g_signal_connect(G_OBJECT(app->gui.entry), "key-press-event", G_CALLBACK(on_entry_key_press), app);

    GtkWidget *upload_btn = gtk_button_new_from_icon_name("mail-attachment", GTK_ICON_SIZE_BUTTON);
    gtk_widget_set_tooltip_text(upload_btn, "Upload file for AI analysis");
    g_signal_connect(upload_btn, "clicked", G_CALLBACK(on_upload_clicked), app);
    gtk_box_pack_start(GTK_BOX(bottom_hbox), upload_btn, FALSE, FALSE, 5);

    app->gui.status_label = gtk_label_new("Ready");
    gtk_box_pack_start(GTK_BOX(bottom_hbox), app->gui.status_label, FALSE, FALSE, 5);

    // 4. Force state engine visual refresh adjustments
    apply_visual_settings(app);

    // Ensure keyboard focus hooks run accurately across active boundaries
    g_signal_connect(app->gui.entry, "key-press-event", G_CALLBACK(on_window_key_press), app);
    g_signal_connect(app->gui.gemini_view, "key-press-event", G_CALLBACK(on_window_key_press), app);

    gtk_widget_show_all(app->gui.window);
}

gboolean scroll_ai_pane_to_bottom(AppContext *app) {
    if (!app || !app->gui.gemini_view) {
        if (app) app->gui.ai_scroll_pending = FALSE;
        return FALSE;
    }

    GtkWidget *parent = gtk_widget_get_parent(app->gui.gemini_view);
    if (GTK_IS_SCROLLED_WINDOW(parent)) {
        GtkAdjustment *adj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(parent));
        gdouble upper = gtk_adjustment_get_upper(adj);
        gdouble page_size = gtk_adjustment_get_page_size(adj);
        gdouble target = upper - page_size;
        if (target < 0) target = 0;
        gtk_adjustment_set_value(adj, target);
    }

    /* This callback is deliberately one-shot. */
    app->gui.ai_scroll_pending = FALSE;
    return FALSE;
}

static void queue_ai_scroll(AppContext *app) {
    if (!app || !app->gui.gemini_view) return;
    if (app->gui.ai_scroll_pending) return;
    app->gui.ai_scroll_pending = TRUE;
    g_idle_add((GSourceFunc)scroll_ai_pane_to_bottom, app);
}

void append_ai_text(AppContext *app, const char *text, const char *tag_name) {
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(app->gui.gemini_view));
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(buffer, &end);

    if (tag_name) {
        gtk_text_buffer_insert_with_tags_by_name(buffer, &end, text, -1, tag_name, NULL);
    } else {
        gtk_text_buffer_insert(buffer, &end, text, -1);
    }

    /* Coalesce scroll requests.  AI/Tee/auto-exec can append many fragments
     * in one main-loop turn; one idle callback is enough for the final state. */
    queue_ai_scroll(app);
}

/* Helper callback triggered when set_command_policy_async finishes */
void on_policy_saved_callback(gboolean success, gpointer user_data) {
    PolicyRecord *p = (PolicyRecord *)user_data;
    
    if (!success) {
        g_warning("[aiterm] Failed to persist command policy record for: %s", 
                  p && p->name ? p->name : "unknown");
    } else {
        g_info("[aiterm] Successfully saved policy [%s] for command: %s", 
               p->type, p->name);
    }

    // Clean up heap allocated policy record created prior to dispatching async thread
    if (p) {
        free_policy_record(p);
    }
}

void on_exec_confirm_response(GtkDialog *dialog, gint response_id, gpointer user_data) {
    AppContext *app = (AppContext *)user_data;
    int slot_id = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(dialog), "slot_id"));

    if (!app || slot_id < 0 || slot_id >= MAX_DLG) {
        gtk_widget_destroy(GTK_WIDGET(dialog));
        return;
    }

    exe_dlg *dlg = &app->exec_dialog[slot_id];

    if (response_id == GTK_RESPONSE_ACCEPT) {
        gboolean save_to_policy = dlg->check_policy ? 
            gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(dlg->check_policy)) : FALSE;

        char *selected_action = dlg->combo_action ? 
            gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(dlg->combo_action)) : NULL;

        if (save_to_policy && dlg->command_text && selected_action) {
            char *binary = extract_binary_name(dlg->command_text);
            if (binary) {
                PolicyRecord rec = {
                    .name = binary,
                    .type = selected_action,
                    .risk = 0
                };
                set_command_policy(app, &rec);
                g_free(binary);
            }
        }

        if (selected_action) {
            if (g_ascii_strcasecmp(selected_action, "ALLOW") == 0 || 
                g_ascii_strncasecmp(selected_action, "ALLOW", 5) == 0) {
                if (dlg->command_text) {
                    feed_command_to_vte(app, dlg->command_text);
                }
            } else {
                g_info("[Policy Blocked]: Command execution blocked by user choice: %s", 
                       dlg->command_text ? dlg->command_text : "");
            }
            g_free(selected_action);
        }
    } else {
        g_info("[Policy Rejected]: User cancelled execution: %s", 
               dlg->command_text ? dlg->command_text : "");
    }

    // Reset array slot state
    if (dlg->command_text) {
        g_free(dlg->command_text);
        dlg->command_text = NULL;
    }
    dlg->dialog = NULL;
    dlg->check_policy = NULL;
    dlg->combo_action = NULL;
    dlg->target_pane_id = 0;
    dlg->active = FALSE;

    // Decrement counter inside aiterm_runtime substructure
    if (app->aiterm_runtime.active_dialog_count > 0) {
        app->aiterm_runtime.active_dialog_count--;
    }

    gtk_widget_destroy(GTK_WIDGET(dialog));
}

/* VTE Context Menu Handler for Close/Rename Tab (Requirement 4) */
void on_vte_populate_popup(VteTerminal *vte, GtkWidget *popup_menu, gpointer user_data) {
    AppContext *app = (AppContext *)user_data;

    GtkWidget *rename_tab_item = gtk_menu_item_new_with_label("Rename Tab");
    GtkWidget *close_tab_item = gtk_menu_item_new_with_label("Close Tab");

    extern void on_menu_rename_tab(GtkMenuItem *item, gpointer data);
    extern void on_menu_close_tab(GtkMenuItem *item, gpointer data);

    g_signal_connect(rename_tab_item, "activate", G_CALLBACK(on_menu_rename_tab), app);
    g_signal_connect(close_tab_item, "activate", G_CALLBACK(on_menu_close_tab), app);

    gtk_widget_show(rename_tab_item);
    gtk_widget_show(close_tab_item);

    GList *children = gtk_container_get_children(GTK_CONTAINER(popup_menu));
    int insert_idx = 1; // Fallback right under item 0
    int idx = 0;

    for (GList *iter = children; iter != NULL; iter = iter->next, idx++) {
        GtkWidget *item = GTK_WIDGET(iter->data);
        const char *label = gtk_menu_item_get_label(GTK_MENU_ITEM(item));
        if (label && g_ascii_strcasecmp(label, "New Tab") == 0) {
            insert_idx = idx + 1;
            break;
        }
    }
    g_list_free(children);

    gtk_menu_shell_insert(GTK_MENU_SHELL(popup_menu), rename_tab_item, insert_idx);
    gtk_menu_shell_insert(GTK_MENU_SHELL(popup_menu), close_tab_item, insert_idx + 1);
}


