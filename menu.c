#include <vte/vte.h>
#include "menu.h"
#include "update.h"
#include "help.h"
#include "utils.h"
#include "terminal.h"
#include "tee_handler.h" // ADD THIS

// --- Callbacks ---

static void on_clear(GtkWidget *widget, gpointer data) {
    AppContext *app = (AppContext *)data;
    GtkTextBuffer *buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(app->gemini_view));
    gtk_text_buffer_set_text(buf, "", -1);
}

static void on_menu_exit(GtkWidget *widget, gpointer data) {
    gtk_main_quit();
}

static void on_help(GtkWidget *widget, gpointer data) {
    AppContext *app = (AppContext *)data;
    append_to_view(app->gemini_view, "System: ", get_help_text());
}

static void on_about(GtkWidget *widget, gpointer data) {
    AppContext *app = (AppContext *)data;
    append_to_view(app->gemini_view, "System: ", get_version_info());
}

static void on_copy(GtkWidget *widget, gpointer data) {
    AppContext *app = (AppContext *)data;
    vte_terminal_copy_clipboard_format(VTE_TERMINAL(app->terminal_view), VTE_FORMAT_TEXT);
}

static void on_paste(GtkWidget *widget, gpointer data) {
    AppContext *app = (AppContext *)data;
    vte_terminal_paste_clipboard(VTE_TERMINAL(app->terminal_view));
}

static void on_tee_toggle(GtkWidget *widget, gpointer data) {
    static int tee_enabled_local = 0;
    AppContext *app = (AppContext *)data;

    tee_enabled_local = !tee_enabled_local;

    if (tee_enabled_local) {
        append_to_view(app->gemini_view, "System: ", "AI Tee ENABLED");
    } else {
        append_to_view(app->gemini_view, "System: ", "AI Tee DISABLED");
    }
}

static void on_transparency_changed(GtkRange *range, gpointer data) {
    AppContext *app = (AppContext *)data;
    app->transparency = gtk_range_get_value(range);

    // Call the helper we just fixed
    //apply_terminal_transparency(app);
    apply_visual_settings(app);
}

// In menu.c
static void on_tee_flush(GtkWidget *widget, gpointer data) {
    AppContext *app = (AppContext *)data;
    tee_flush_timed(); // Call the new handler directly
    append_to_view(app->gemini_view, "System: ", "Tee buffer manual flush triggered.");
}

static void on_terminal_font_set(GtkFontButton *btn, gpointer data) {
    AppContext *app = (AppContext *)data;
    if (app->terminal_font) free(app->terminal_font);
    app->terminal_font = strdup(gtk_font_chooser_get_font(GTK_FONT_CHOOSER(btn)));
    apply_visual_settings(app);
}

static void on_ai_font_set(GtkFontButton *btn, gpointer data) {
    AppContext *app = (AppContext *)data;
    if (app->ai_font) free(app->ai_font);
    app->ai_font = strdup(gtk_font_chooser_get_font(GTK_FONT_CHOOSER(btn)));
    apply_visual_settings(app);
}

void on_clear_history(GtkWidget *widget, gpointer data) {
    // 1. Tell the compiler that 'data' is actually our AppContext
    AppContext *app = (AppContext *)data;

    char path[256];
    snprintf(path, sizeof(path), "%s/.aiterm_history", getenv("HOME"));

    if (remove(path) == 0) {
        append_to_view(app->gemini_view, "System: ", "Memory cleared.");
    } else {
        append_to_view(app->gemini_view, "System: ", "No history file found to clear.");
    }
}

static void on_preferences(GtkWidget *widget, gpointer data) {
    AppContext *app = (AppContext *)data;
    GtkWidget *dialog = gtk_dialog_new_with_buttons("Preferences",
                                                  GTK_WINDOW(app->window),
                                                  GTK_DIALOG_MODAL,
 												  "Save", GTK_RESPONSE_ACCEPT,
                                                  "Close", GTK_RESPONSE_CLOSE,
                                                  NULL);


    GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 15);
    gtk_container_add(GTK_CONTAINER(content_area), vbox);

    GtkWidget *label = gtk_label_new("Terminal/AI Transparency:");
    gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, FALSE, 0);

    GtkWidget *slider = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.1, 1.0, 0.05);
    gtk_range_set_value(GTK_RANGE(slider), app->transparency);
    gtk_box_pack_start(GTK_BOX(vbox), slider, FALSE, FALSE, 0);

    g_signal_connect(slider, "value-changed", G_CALLBACK(on_transparency_changed), app);

// --- NEW: Font Selection (MOVED HERE) ---
    GtkWidget *term_font_label = gtk_label_new("Terminal Font:");
    gtk_box_pack_start(GTK_BOX(vbox), term_font_label, FALSE, FALSE, 0);
    GtkWidget *term_font_btn = gtk_font_button_new_with_font(app->terminal_font);
    gtk_box_pack_start(GTK_BOX(vbox), term_font_btn, FALSE, FALSE, 0);
    g_signal_connect(term_font_btn, "font-set", G_CALLBACK(on_terminal_font_set), app);

    GtkWidget *ai_font_label = gtk_label_new("AI Pane Font:");
    gtk_box_pack_start(GTK_BOX(vbox), ai_font_label, FALSE, FALSE, 0);
    GtkWidget *ai_font_btn = gtk_font_button_new_with_font(app->ai_font);
    gtk_box_pack_start(GTK_BOX(vbox), ai_font_btn, FALSE, FALSE, 0);
    g_signal_connect(ai_font_btn, "font-set", G_CALLBACK(on_ai_font_set), app);

    gtk_widget_show_all(dialog);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        save_config(app); 
    }

    gtk_widget_destroy(dialog);
}


 
   

// --- Build Menu ---

GtkWidget* create_menu_bar(AppContext *app) {
    GtkWidget *menubar = gtk_menu_bar_new();

    // --- GtkWidget ---
    GtkWidget *file_menu = gtk_menu_new();
    GtkWidget *file_item = gtk_menu_item_new_with_label("File");
    GtkWidget *clear_item = gtk_menu_item_new_with_label("Clear");
    GtkWidget *exit_item = gtk_menu_item_new_with_label("Exit");
    
    GtkWidget *edit_menu = gtk_menu_new();
    GtkWidget *edit_item = gtk_menu_item_new_with_label("Edit");
    GtkWidget *copy_item = gtk_menu_item_new_with_label("Copy (Ctrl+Shift+C)");
    GtkWidget *paste_item = gtk_menu_item_new_with_label("Paste (Ctrl+Shift+V)");
    
    GtkWidget *tools_menu = gtk_menu_new();
    GtkWidget *tools_item = gtk_menu_item_new_with_label("Tools");
    GtkWidget *tee_toggle = gtk_menu_item_new_with_label("Toggle Tee");
    GtkWidget *tee_flush = gtk_menu_item_new_with_label("Flush Tee");
    GtkWidget *pref_item = gtk_menu_item_new_with_label("Preferences"); // ADD THIS

    GtkWidget *help_menu = gtk_menu_new();
    GtkWidget *help_item = gtk_menu_item_new_with_label("Help");
    GtkWidget *help_btn = gtk_menu_item_new_with_label("Help");
    GtkWidget *about_btn = gtk_menu_item_new_with_label("About");


    gtk_menu_shell_append(GTK_MENU_SHELL(file_menu), clear_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(file_menu), exit_item);

    gtk_menu_shell_append(GTK_MENU_SHELL(edit_menu), copy_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(edit_menu), paste_item);

    gtk_menu_shell_append(GTK_MENU_SHELL(tools_menu), tee_toggle);
    gtk_menu_shell_append(GTK_MENU_SHELL(tools_menu), tee_flush);
    gtk_menu_shell_append(GTK_MENU_SHELL(tools_menu), pref_item);

    gtk_menu_shell_append(GTK_MENU_SHELL(help_menu), help_btn);
    gtk_menu_shell_append(GTK_MENU_SHELL(help_menu), about_btn);

    gtk_menu_shell_append(GTK_MENU_SHELL(menubar), file_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(menubar), edit_item); // ADD THIS
    gtk_menu_shell_append(GTK_MENU_SHELL(menubar), tools_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(menubar), help_item);

    g_signal_connect(clear_item, "activate", G_CALLBACK(on_clear), app);
    g_signal_connect(exit_item, "activate", G_CALLBACK(on_menu_exit), app);
    g_signal_connect(copy_item, "activate", G_CALLBACK(on_copy), app);
    g_signal_connect(paste_item, "activate", G_CALLBACK(on_paste), app);
    g_signal_connect(tee_toggle, "activate", G_CALLBACK(on_tee_toggle), app);
    g_signal_connect(tee_flush, "activate", G_CALLBACK(on_tee_flush), app);
    g_signal_connect(pref_item, "activate", G_CALLBACK(on_preferences), app);
    g_signal_connect(help_btn, "activate", G_CALLBACK(on_help), app);
    g_signal_connect(about_btn, "activate", G_CALLBACK(on_about), app);
        
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(file_item), file_menu);
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(edit_item), edit_menu);
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(tools_item), tools_menu);
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(help_item), help_menu);

    return menubar;
}
