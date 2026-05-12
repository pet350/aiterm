/* gui.c
* C Program file for gui functions
* By: Peter Talbott
* With assistance from Gemini and OpenAI
* May 2026
*/

#include <ctype.h>
#include <json-c/json.h>
#include "utils.h"
#include "gui.h"
#include "openai.h"
#include <mariadb/mysql.h>
#include "crypto.h" // Add this include
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

    /*
       In older GTK3, we use 'cursor-aspect-ratio'.
       0.5 is usually a thick block. 1.0 is a full square.
    */
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

// 1. Define the theme outside so it's clean.
// This replaces the deprecated 'override' functions.
void apply_custom_theme() {
    GtkCssProvider *provider = gtk_css_provider_new();
    const char *css =
        "window, box, grid { background-color: transparent; }"
/* FIX: Specifically target the menubar and menus to be solid black */
        "menubar, menu, menuitem { background-color: #000000; color: #ffffff; }"
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

static gboolean on_window_key_press(GtkWidget *widget, GdkEventKey *event, gpointer data) {
    AppContext *app = (AppContext *)data;

    // Detect Ctrl + Tab
    if ((event->state & GDK_CONTROL_MASK) && event->keyval == GDK_KEY_Tab) {

        if (gtk_widget_has_focus(app->entry)) {
            // 1. From Input -> Jump to Terminal
            gtk_widget_grab_focus(app->terminal_view);
            DEBUG_PRINT("Focus: Terminal\n");
        } else if (gtk_widget_has_focus(app->terminal_view)) {
            // 2. From Terminal -> Jump to AI History View
            gtk_widget_grab_focus(app->gemini_view);
            DEBUG_PRINT("Focus: AI View\n");
        } else {
            // 3. From AI View (or anywhere else) -> Back to Input
            gtk_widget_grab_focus(app->entry);
            DEBUG_PRINT("Focus: Input\n");
        }

        return TRUE; // Consume event
    }
    return FALSE;
}

// the "Paperclip" button callback
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

            // Re-use your threaded AI caller
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

        // Visual feedback
        update_status_label(app, "History copied to clipboard");
    }
    g_free(full_text);
}

void set_icon(AppContext *app) {
    GError *icon_error = NULL;

    // Load from RESOURCE instead of FILE
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

    // 1. Create the Main Window FIRST
    app->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	g_signal_connect_after(app->window, "key-press-event", G_CALLBACK(on_window_key_press), app);

    set_icon(app);

    // 2. NOW apply transparency support while app->window is a valid object
    GdkScreen *screen = gtk_window_get_screen(GTK_WINDOW(app->window));
    GdkVisual *visual = gdk_screen_get_rgba_visual(screen);
    if (visual) {
        gtk_widget_set_visual(app->window, visual);
    }
    gtk_widget_set_app_paintable(app->window, TRUE);

    // 3. Continue with window properties and layout
    gtk_window_set_title(GTK_WINDOW(app->window), "AI-Term C/GTK Edition");
    gtk_window_set_default_size(GTK_WINDOW(app->window), 1000, 600);
    g_signal_connect(app->window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    // ... (rest of your packing and setup code) ...
    // 2. Layout
    GtkWidget *main_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_add(GTK_CONTAINER(app->window), main_vbox);

    // --- ADD MENU BAR HERE ---
    GtkWidget *menubar = create_menu_bar(app);
    gtk_box_pack_start(GTK_BOX(main_vbox), menubar, FALSE, FALSE, 0);

    GtkWidget *paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(main_vbox), paned, TRUE, TRUE, 0);

    // --- LEFT PANE (Terminal) ---
    app->terminal_view = setup_terminal(app);
    GtkWidget *term_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(term_scroll), app->terminal_view);
    gtk_paned_pack1(GTK_PANED(paned), term_scroll, TRUE, FALSE);

    // --- RIGHT PANE (Gemini) ---
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

    // 4. BOTTOM AREA
    GtkWidget *bottom_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(main_vbox), bottom_hbox, FALSE, FALSE, 5);

    app->entry = gtk_entry_new();
    apply_block_cursor_to_input(app->entry);
    gtk_entry_set_placeholder_text(GTK_ENTRY(app->entry), "Ask Gemini...");
    gtk_box_pack_start(GTK_BOX(bottom_hbox), app->entry, TRUE, TRUE, 5);

    GtkWidget *copy_btn = gtk_button_new_from_icon_name("edit-copy", GTK_ICON_SIZE_BUTTON);
    gtk_widget_set_tooltip_text(copy_btn, "Copy AI History");
    g_signal_connect(copy_btn, "clicked", G_CALLBACK(on_copy_clicked), app);
    gtk_box_pack_start(GTK_BOX(bottom_hbox), copy_btn, FALSE, FALSE, 5);

    // Signal Connection for the input
    extern void on_input_activate(GtkEntry *entry, gpointer data);
    g_signal_connect(app->entry, "activate", G_CALLBACK(on_input_activate), app);

    // Inside setup_gui, near your other buttons
    GtkWidget *upload_btn = gtk_button_new_from_icon_name("mail-attachment", GTK_ICON_SIZE_BUTTON);
    gtk_widget_set_tooltip_text(upload_btn, "Upload file for AI analysis");
    g_signal_connect(upload_btn, "clicked", G_CALLBACK(on_upload_clicked), app);
    gtk_box_pack_start(GTK_BOX(bottom_hbox), upload_btn, FALSE, FALSE, 5);

    app->status_label = gtk_label_new("Ready");
    gtk_box_pack_start(GTK_BOX(bottom_hbox), app->status_label, FALSE, FALSE, 5);

    // 4. Final step: ensure the terminal transparency is applied
    apply_visual_settings(app);

    // Inside setup_gui
    g_signal_connect(app->window, "key-press-event", G_CALLBACK(on_window_key_press), app);
    g_signal_connect(app->terminal_view, "key-press-event", G_CALLBACK(on_window_key_press), app);
    g_signal_connect(app->entry, "key-press-event", G_CALLBACK(on_window_key_press), app);
    g_signal_connect(app->gemini_view, "key-press-event", G_CALLBACK(on_window_key_press), app);

    gtk_widget_show_all(app->window);
}

void append_ai_text(AppContext *app, const char *text, const char *tag_name) {
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(app->gemini_view));
    GtkTextIter end;
 
    gtk_text_buffer_get_end_iter(buffer, &end);

    // If tag_name is provided, use it. Otherwise, print normal.
    if (tag_name) {
        gtk_text_buffer_insert_with_tags_by_name(buffer, &end, text, -1, tag_name, NULL);
    } else {
        gtk_text_buffer_insert(buffer, &end, text, -1);
    }

    // Auto-scroll to the bottom
    GtkTextMark *mark = gtk_text_buffer_get_insert(buffer);
    gtk_text_view_scroll_mark_onscreen(GTK_TEXT_VIEW(app->gemini_view), mark);
}
