// menu.c
// Part of the aiterm project
// C Program file for GUI Mennu functions
// By: Peter Talbott
// With assistance from Gemini and OpenAI
// May 2026

#include <vte/vte.h>
#include <mariadb/mysql.h>
#include "gui.h"
#include "menu.h"
#include "update.h"
#include "help.h"
#include "utils.h"
#include "terminal.h"
#include "tee_handler.h"

// --- Menu Callbacks ---

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
    write_to_ai_pane(app, "[ Help System ]\n", get_help_text(), "cmd_tag", "cmd_tag");
}

static void on_about(GtkWidget *widget, gpointer data) {
    AppContext *app = (AppContext *)data;
    write_to_ai_pane(app, "[ Version Info ]\n", get_version_info(), "cmd_tag", "cmd_tag");
}

static void on_copy(GtkWidget *widget, gpointer data) {
    AppContext *app = (AppContext *)data;
    vte_terminal_copy_clipboard_format(VTE_TERMINAL(app->terminal_view), VTE_FORMAT_TEXT);
}

static void on_paste(GtkWidget *widget, gpointer data) {
    AppContext *app = (AppContext *)data;
    vte_terminal_paste_clipboard(VTE_TERMINAL(app->terminal_view));
}

// Forward declarations for mutual dependency blocking
static void on_tee_toggle(GtkWidget *widget, gpointer data);
static void on_autoreply_toggle(GtkWidget *widget, gpointer data);

static void on_tee_toggle(GtkWidget *widget, gpointer data) {
    AppContext *app = (AppContext *)data;
    app->tee_enabled = gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(widget));

    // LOGIC: If Tee goes OFF, Autoreply MUST go OFF
    if (!app->tee_enabled && app->autoreply_enabled) {
        app->autoreply_enabled = FALSE;
        if (app->autoreply_menu_item) {
            g_signal_handlers_block_by_func(app->autoreply_menu_item, on_autoreply_toggle, app);
            gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(app->autoreply_menu_item), FALSE);
            g_signal_handlers_unblock_by_func(app->autoreply_menu_item, on_autoreply_toggle, app);
        }
    }
    write_to_ai_pane(app, "System: ", app->tee_enabled ? "Tee Mode ENABLED" : "Tee & Autoreply DISABLED", "cmd_tag", "cmd_tag");
}

static void on_autoreply_toggle(GtkWidget *widget, gpointer data) {
    AppContext *app = (AppContext *)data;
    app->autoreply_enabled = gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(widget));

    // LOGIC: If Autoreply goes ON, Tee MUST go ON
    if (app->autoreply_enabled && !app->tee_enabled) {
        app->tee_enabled = TRUE;
        if (app->tee_menu_item) {
            g_signal_handlers_block_by_func(app->tee_menu_item, on_tee_toggle, app);
            gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(app->tee_menu_item), TRUE);
            g_signal_handlers_unblock_by_func(app->tee_menu_item, on_tee_toggle, app);
        }
    }
    write_to_ai_pane(app, "System: ", app->autoreply_enabled ? "Autoreply & Tee ENABLED" : "Autoreply DISABLED", "cmd_tag", "cmd_tag");
}

// --- Visual Preferences Callbacks (RESTORED) ---

static void on_transparency_changed(GtkRange *range, gpointer data) {
    AppContext *app = (AppContext *)data;
    app->transparency = gtk_range_get_value(range);
    apply_visual_settings(app);
}

static void on_ai_transparency_changed(GtkRange *range, gpointer data) {
    AppContext *app = (AppContext *)data;
    app->ai_transparency = gtk_range_get_value(range);
    apply_visual_settings(app);
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

static void on_preferences(GtkWidget *widget, gpointer data) {
    AppContext *app = (AppContext *)data;
    GtkWidget *dialog = gtk_dialog_new_with_buttons("Preferences", GTK_WINDOW(app->window),
                                                  GTK_DIALOG_MODAL, "Save", GTK_RESPONSE_ACCEPT,
                                                  "Close", GTK_RESPONSE_CLOSE, NULL);

    GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 15);
    gtk_container_add(GTK_CONTAINER(content_area), vbox);

    gtk_box_pack_start(GTK_BOX(vbox), gtk_label_new("Terminal Transparency:"), FALSE, FALSE, 0);
    GtkWidget *s1 = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.1, 1.0, 0.05);
    gtk_range_set_value(GTK_RANGE(s1), app->transparency);
    gtk_box_pack_start(GTK_BOX(vbox), s1, FALSE, FALSE, 0);
    g_signal_connect(s1, "value-changed", G_CALLBACK(on_transparency_changed), app);

    gtk_box_pack_start(GTK_BOX(vbox), gtk_label_new("AI Pane Transparency:"), FALSE, FALSE, 0);
    GtkWidget *s2 = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.1, 1.0, 0.05);
    gtk_range_set_value(GTK_RANGE(s2), app->ai_transparency);
    gtk_box_pack_start(GTK_BOX(vbox), s2, FALSE, FALSE, 0);
    g_signal_connect(s2, "value-changed", G_CALLBACK(on_ai_transparency_changed), app);

    gtk_box_pack_start(GTK_BOX(vbox), gtk_label_new("Terminal Font:"), FALSE, FALSE, 0);
    GtkWidget *f1 = gtk_font_button_new_with_font(app->terminal_font);
    gtk_box_pack_start(GTK_BOX(vbox), f1, FALSE, FALSE, 0);
    g_signal_connect(f1, "font-set", G_CALLBACK(on_terminal_font_set), app);

    gtk_box_pack_start(GTK_BOX(vbox), gtk_label_new("AI Pane Font:"), FALSE, FALSE, 0);
    GtkWidget *f2 = gtk_font_button_new_with_font(app->ai_font);
    gtk_box_pack_start(GTK_BOX(vbox), f2, FALSE, FALSE, 0);
    g_signal_connect(f2, "font-set", G_CALLBACK(on_ai_font_set), app);

    gtk_widget_show_all(dialog);
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) save_config(app);
    gtk_widget_destroy(dialog);
}

static void on_tee_flush(GtkWidget *widget, gpointer data) {
    AppContext *app = (AppContext *)data;
    tee_flush_timed(app);
    write_to_ai_pane(app, "System: ", "Tee buffer manual flush triggered.", "cmd_tag", "cmd_tag");
}

// --- Menu Builder ---

GtkWidget* create_menu_bar(AppContext *app) {
    GtkWidget *menubar = gtk_menu_bar_new();

    // Setup Menus
    GtkWidget *file_menu = gtk_menu_new();
    GtkWidget *file_item = gtk_menu_item_new_with_label("File");
    GtkWidget *clear_item = gtk_menu_item_new_with_label("Clear AI History");
    GtkWidget *exit_item = gtk_menu_item_new_with_label("Exit");

    GtkWidget *edit_menu = gtk_menu_new();
    GtkWidget *edit_item = gtk_menu_item_new_with_label("Edit");
    GtkWidget *copy_item = gtk_menu_item_new_with_label("Copy Terminal");
    GtkWidget *paste_item = gtk_menu_item_new_with_label("Paste Terminal");

    GtkWidget *tools_menu = gtk_menu_new();
    GtkWidget *tools_item = gtk_menu_item_new_with_label("Tools");

    // Store in struct
    app->tee_menu_item = gtk_check_menu_item_new_with_label("Toggle Tee Mode");
    app->autoreply_menu_item = gtk_check_menu_item_new_with_label("Toggle Autoreply Mode");
    GtkWidget *tee_flush = gtk_menu_item_new_with_label("Flush Tee Buffer");
    GtkWidget *pref_item = gtk_menu_item_new_with_label("Preferences");

    GtkWidget *help_menu = gtk_menu_new();
    GtkWidget *help_item = gtk_menu_item_new_with_label("Help");
    GtkWidget *help_btn = gtk_menu_item_new_with_label("Help Content");
    GtkWidget *about_btn = gtk_menu_item_new_with_label("About");

    // Sync Checkboxes with App State
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(app->tee_menu_item), app->tee_enabled);
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(app->autoreply_menu_item), app->autoreply_enabled);

    // Assembly
    gtk_menu_shell_append(GTK_MENU_SHELL(file_menu), clear_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(file_menu), exit_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(edit_menu), copy_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(edit_menu), paste_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(tools_menu), app->tee_menu_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(tools_menu), app->autoreply_menu_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(tools_menu), tee_flush);
    gtk_menu_shell_append(GTK_MENU_SHELL(tools_menu), pref_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(help_menu), help_btn);
    gtk_menu_shell_append(GTK_MENU_SHELL(help_menu), about_btn);

    gtk_menu_shell_append(GTK_MENU_SHELL(menubar), file_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(menubar), edit_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(menubar), tools_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(menubar), help_item);

    // Signals
    g_signal_connect(clear_item, "activate", G_CALLBACK(on_clear), app);
    g_signal_connect(exit_item, "activate", G_CALLBACK(on_menu_exit), app);
    g_signal_connect(copy_item, "activate", G_CALLBACK(on_copy), app);
    g_signal_connect(paste_item, "activate", G_CALLBACK(on_paste), app);
    g_signal_connect(app->tee_menu_item, "activate", G_CALLBACK(on_tee_toggle), app);
    g_signal_connect(app->autoreply_menu_item, "activate", G_CALLBACK(on_autoreply_toggle), app);
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
