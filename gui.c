// gui.c
// C Program file for gui functions
// By: Peter Talbott
// With assistance from Gemini and OpenAI
// May 2026

#include <ctype.h>
#include <json-c/json.h>
#include "utils.h"
#include "gui.h"
#include "openai.h"
#include <mariadb/mysql.h>
#include "crypto.h"
#include "update.h"
#include "terminal.h"
#include "menu.h"
#include "gemini.h"

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

/*
 * 0.8.3 FIX: Enhanced CSS for GtkNotebook (Tab Bar)
 * Targets internal node names (header, stack, tab) to override
 * default white themes when running as root/Adwaita-light.
 */
void apply_custom_theme() {
    GtkCssProvider *provider = gtk_css_provider_new();
    const char *css =
        "window, box, grid { background-color: transparent; }"
        "menubar, menu, menuitem { background-color: #000000; color: #ffffff; }"

        /* The main Notebook container */
        "notebook { background-color: rgba(20, 20, 20, 0.6); border: none; }"

        /* The header (the actual bar where tabs sit) */
        "notebook header { background-color: rgba(30, 30, 30, 0.8); border-bottom: 1px solid #333333; padding: 2px; }"

        /* The content area underneath the tabs */
        "notebook stack { background-color: transparent; }"

        /* The individual tabs */
        "notebook header tabs tab { "
        "  background-color: rgba(45, 45, 45, 0.5); "
        "  color: #aaaaaa; "
        "  padding: 6px 12px; "
        "  border: 1px solid #333333; "
        "  margin: 0 2px; "
        "}"

        /* The active selected tab */
        "notebook header tabs tab:checked { "
        "  background-color: #000000; "
        "  color: #00FF00; "
        "  font-weight: bold; "
        "  border-bottom: 2px solid #00FF00; "
        "}"

        "textview { "
        "  background-color: transparent; "
        "  color: #dcdcdc; "
        "  font-family: monospace; "
        "  font-size: 10pt; "
        "}"
        "entry { "
        "  background-color: #1a1a1a; "
        "  color: #ffffff; "
        "  border: 1px solid #333333; "
        "}"
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
        if (gtk_widget_has_focus(app->entry)) {
            if (app->terminal_view) gtk_widget_grab_focus(app->terminal_view);
            DEBUG_PRINT("Focus: Active Terminal Tab\n");
        } else if (app->terminal_view && gtk_widget_has_focus(app->terminal_view)) {
            gtk_widget_grab_focus(app->gemini_view);
            DEBUG_PRINT("Focus: AI View\n");
        } else {
            gtk_widget_grab_focus(app->entry);
            DEBUG_PRINT("Focus: Input\n");
        }
        return TRUE;
    }
    return FALSE;
}

// NEW: Lifecycle tracking handler syncing app->terminal_view during tab adjustments
static void on_tab_changed(GtkNotebook *notebook, GtkWidget *page, guint page_num, gpointer data) {
    AppContext *app = (AppContext *)data;

    // The page added inside our scroll windows is the scrolled window container.
    // We dig out its child to grab the pure active VteTerminal instance.
    GtkWidget *scrolled_win = page;
    GtkWidget *terminal = gtk_bin_get_child(GTK_BIN(scrolled_win));

    if (VTE_IS_TERMINAL(terminal)) {
        app->terminal_view = terminal;
        DEBUG_PRINT("0.8.3: Focused tab shifted to Page #%d (Widget: %p)\n", page_num, (void*)terminal);

        // Push current fonts and transparency settings dynamically down to the new pane
        apply_visual_settings(app);
    }
}

// NEW: Modular spawner adding fully functional isolated terminals into the notebook array
void add_terminal_tab(AppContext *app) {
    static int tab_counter = 0;
    tab_counter++;

    // 1. Build infrastructure elements
    GtkWidget *term_scroll = gtk_scrolled_window_new(NULL, NULL);
    GtkWidget *new_terminal = setup_terminal(app); // Spawns VTE + execs shell hook
    gtk_container_add(GTK_CONTAINER(term_scroll), new_terminal);

    // 2. Build label string
    char tab_label_text[32];
    snprintf(tab_label_text, sizeof(tab_label_text), "pts/%d", tab_counter);
    GtkWidget *tab_label = gtk_label_new(tab_label_text);

    // 3. Inject page structure into the notebook layout
    gint index = gtk_notebook_append_page(GTK_NOTEBOOK(app->notebook), term_scroll, tab_label);
    gtk_widget_show_all(term_scroll);

    // 4. Ensure window interceptor handles input routing on the new console layer
    g_signal_connect(new_terminal, "key-press-event", G_CALLBACK(on_window_key_press), app);

    // 5. Jump focus directly to our newly allocated workspace
    gtk_notebook_set_current_page(GTK_NOTEBOOK(app->notebook), index);
    gtk_widget_grab_focus(new_terminal);
}

void on_upload_clicked(GtkButton *button, gpointer data) {
    AppContext *app = (AppContext *)data;
    GtkWidget *dialog = gtk_file_chooser_dialog_new("Analyze File",
                                                    GTK_WINDOW(app->window),
                                                    GTK_FILE_CHOOSER_ACTION_OPEN,
                                                    "_Cancel", GTK_RESPONSE_CANCEL,
                                                    "_Open", GTK_RESPONSE_ACCEPT,
                                                    NULL);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        char *file_text = read_file_to_string(filename);

        if (file_text) {
            char *prompt = g_strdup_printf("FILE ANALYSIS (%s):\n\n%s", filename, file_text);

            AIThreadData *td = g_malloc0(sizeof(AIThreadData));
            td->app = app;
            td->prompt = prompt;
            g_thread_new("ai_worker", (GThreadFunc)ai_thread_func, td);

            append_ai_text(app, "System: ", "system_tag");
            append_ai_text(app, " Uploading file for analysis...\n", "body_tag");

            free(file_text);
        }
        g_free(filename);
    }
    gtk_widget_destroy(dialog);
}

void on_copy_clicked(GtkButton *button, gpointer data) {
    AppContext *app = (AppContext *)data;
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(app->gemini_view));

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
        gtk_window_set_icon(GTK_WINDOW(app->window), icon);
        g_object_unref(icon);
        DEBUG_PRINT("Embedded icon loaded from GResource successfully.\n");
    } else {
        g_warning("Could not load embedded icon: %s", icon_error->message);
        if (icon_error) g_error_free(icon_error);
    }
}

void setup_gui(AppContext *app) {
    apply_custom_theme();

    // 1. Create Window Base Framework
    app->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    g_signal_connect_after(app->window, "key-press-event", G_CALLBACK(on_window_key_press), app);
    set_icon(app);

    GdkScreen *screen = gtk_window_get_screen(GTK_WINDOW(app->window));
    GdkVisual *visual = gdk_screen_get_rgba_visual(screen);
    if (visual) {
        gtk_widget_set_visual(app->window, visual);
    }
    gtk_widget_set_app_paintable(app->window, TRUE);

    gtk_window_set_title(GTK_WINDOW(app->window), "AI-Term C/GTK Edition");
    gtk_window_set_default_size(GTK_WINDOW(app->window), 1000, 600);
    g_signal_connect(app->window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    // 2. Primary Structural Box Alignment
    GtkWidget *main_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_add(GTK_CONTAINER(app->window), main_vbox);

    GtkWidget *menubar = create_menu_bar(app);
    gtk_box_pack_start(GTK_BOX(main_vbox), menubar, FALSE, FALSE, 0);

    GtkWidget *paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(main_vbox), paned, TRUE, TRUE, 0);

    // --- LEFT PANE UPGRADE: Dynamic GtkNotebook Container Setup ---
    app->notebook = gtk_notebook_new();
    gtk_notebook_set_tab_pos(GTK_NOTEBOOK(app->notebook), GTK_POS_TOP);
    gtk_notebook_set_scrollable(GTK_NOTEBOOK(app->notebook), TRUE);

    // Connect tracker handler ensuring app->terminal_view shifts variables on page selection flips
    g_signal_connect(app->notebook, "switch-page", G_CALLBACK(on_tab_changed), app);
    gtk_paned_pack1(GTK_PANED(paned), app->notebook, TRUE, FALSE);

    // Allocate our initial bootup console instance inside the array matrix
    add_terminal_tab(app);

    // --- RIGHT PANE (AI History Console View) ---
    GtkWidget *gem_scroll = gtk_scrolled_window_new(NULL, NULL);
    app->gemini_view = gtk_text_view_new();
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(app->gemini_view));
    setup_tags(buffer);

    gtk_text_view_set_editable(GTK_TEXT_VIEW(app->gemini_view), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(app->gemini_view), GTK_WRAP_WORD);
    gtk_container_add(GTK_CONTAINER(gem_scroll), app->gemini_view);
    gtk_paned_pack2(GTK_PANED(paned), gem_scroll, TRUE, FALSE);

    app->ai_css_provider = gtk_css_provider_new();
    gtk_style_context_add_provider(
        gtk_widget_get_style_context(app->gemini_view),
        GTK_STYLE_PROVIDER(app->ai_css_provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION
    );

    // 3. BOTTOM UTILITY INTERFACES
    GtkWidget *bottom_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(main_vbox), bottom_hbox, FALSE, FALSE, 5);

    app->entry = gtk_entry_new();
    apply_block_cursor_to_input(app->entry);
    gtk_entry_set_placeholder_text(GTK_ENTRY(app->entry), "Ask AI...");
    gtk_box_pack_start(GTK_BOX(bottom_hbox), app->entry, TRUE, TRUE, 5);

    GtkWidget *copy_btn = gtk_button_new_from_icon_name("edit-copy", GTK_ICON_SIZE_BUTTON);
    gtk_widget_set_tooltip_text(copy_btn, "Copy AI History");
    g_signal_connect(copy_btn, "clicked", G_CALLBACK(on_copy_clicked), app);
    gtk_box_pack_start(GTK_BOX(bottom_hbox), copy_btn, FALSE, FALSE, 5);

    extern void on_input_activate(GtkEntry *entry, gpointer data);
    g_signal_connect(app->entry, "activate", G_CALLBACK(on_input_activate), app);

    GtkWidget *upload_btn = gtk_button_new_from_icon_name("mail-attachment", GTK_ICON_SIZE_BUTTON);
    gtk_widget_set_tooltip_text(upload_btn, "Upload file for AI analysis");
    g_signal_connect(upload_btn, "clicked", G_CALLBACK(on_upload_clicked), app);
    gtk_box_pack_start(GTK_BOX(bottom_hbox), upload_btn, FALSE, FALSE, 5);

    app->status_label = gtk_label_new("Ready");
    gtk_box_pack_start(GTK_BOX(bottom_hbox), app->status_label, FALSE, FALSE, 5);

    // 4. Force state engine visual refresh adjustments
    apply_visual_settings(app);

    // Ensure keyboard focus hooks run accurately across active boundaries
    g_signal_connect(app->entry, "key-press-event", G_CALLBACK(on_window_key_press), app);
    g_signal_connect(app->gemini_view, "key-press-event", G_CALLBACK(on_window_key_press), app);

    gtk_widget_show_all(app->window);
}

void append_ai_text(AppContext *app, const char *text, const char *tag_name) {
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(app->gemini_view));
    GtkTextIter end;

    gtk_text_buffer_get_end_iter(buffer, &end);

    if (tag_name) {
        gtk_text_buffer_insert_with_tags_by_name(buffer, &end, text, -1, tag_name, NULL);
    } else {
        gtk_text_buffer_insert(buffer, &end, text, -1);
    }

    GtkTextMark *mark = gtk_text_buffer_get_insert(buffer);
    gtk_text_view_scroll_mark_onscreen(GTK_TEXT_VIEW(app->gemini_view), mark);
}
