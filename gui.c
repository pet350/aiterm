#include "gui.h"
#include "terminal.h"
#include "menu.h"

// 1. Define the theme outside so it's clean.
// This replaces the deprecated 'override' functions.
void apply_custom_theme() {
    GtkCssProvider *provider = gtk_css_provider_new();
    const char *css = 
        "window, box, grid { background-color: #000000; }"
        "textview { "
        "  background-color: #0d0d0d; "
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

void setup_gui(AppContext *app) {
    // Apply the CSS first
    apply_custom_theme();

    // 1. Create the Main Window
    app->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(app->window), "AI-Term C/GTK Edition");
    gtk_window_set_default_size(GTK_WINDOW(app->window), 1000, 600);
    g_signal_connect(app->window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

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
    gtk_text_view_set_editable(GTK_TEXT_VIEW(app->gemini_view), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(app->gemini_view), GTK_WRAP_WORD);
    gtk_container_add(GTK_CONTAINER(gem_scroll), app->gemini_view);
    gtk_paned_pack2(GTK_PANED(paned), gem_scroll, TRUE, FALSE);

    // 4. BOTTOM AREA
    GtkWidget *bottom_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(main_vbox), bottom_hbox, FALSE, FALSE, 5);

    app->entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(app->entry), "Ask Gemini...");
    gtk_box_pack_start(GTK_BOX(bottom_hbox), app->entry, TRUE, TRUE, 5);

    app->status_label = gtk_label_new("Ready");
    gtk_box_pack_start(GTK_BOX(bottom_hbox), app->status_label, FALSE, FALSE, 5);

    // Signal Connection for the input
    extern void on_input_activate(GtkEntry *entry, gpointer data);
    g_signal_connect(app->entry, "activate", G_CALLBACK(on_input_activate), app);

    gtk_widget_show_all(app->window);
} // <--- This was the missing bracket causing the "end of input" error.
