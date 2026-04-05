#include "menu.h"
#include "update.h"
#include "help.h"
#include "utils.h"

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

// Tee controls (reuse your logic style)
extern void flush_to_ai(AppContext *app);

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

static void on_tee_flush(GtkWidget *widget, gpointer data) {
    AppContext *app = (AppContext *)data;
    flush_to_ai(app);
    append_to_view(app->gemini_view, "System: ", "Tee buffer flushed");
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

// --- Build Menu ---

GtkWidget* create_menu_bar(AppContext *app) {
    GtkWidget *menubar = gtk_menu_bar_new();

    // --- File ---
    GtkWidget *file_menu = gtk_menu_new();
    GtkWidget *file_item = gtk_menu_item_new_with_label("File");
    GtkWidget *clear_item = gtk_menu_item_new_with_label("Clear");
    GtkWidget *exit_item = gtk_menu_item_new_with_label("Exit");

    gtk_menu_shell_append(GTK_MENU_SHELL(file_menu), clear_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(file_menu), exit_item);
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(file_item), file_menu);

    g_signal_connect(clear_item, "activate", G_CALLBACK(on_clear), app);
    g_signal_connect(exit_item, "activate", G_CALLBACK(on_menu_exit), app);

    // --- Tools ---
    GtkWidget *tools_menu = gtk_menu_new();
    GtkWidget *tools_item = gtk_menu_item_new_with_label("Tools");
    GtkWidget *tee_toggle = gtk_menu_item_new_with_label("Toggle Tee");
    GtkWidget *tee_flush = gtk_menu_item_new_with_label("Flush Tee");

    gtk_menu_shell_append(GTK_MENU_SHELL(tools_menu), tee_toggle);
    gtk_menu_shell_append(GTK_MENU_SHELL(tools_menu), tee_flush);
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(tools_item), tools_menu);

    g_signal_connect(tee_toggle, "activate", G_CALLBACK(on_tee_toggle), app);
    g_signal_connect(tee_flush, "activate", G_CALLBACK(on_tee_flush), app);

    // --- Help ---
    GtkWidget *help_menu = gtk_menu_new();
    GtkWidget *help_item = gtk_menu_item_new_with_label("Help");
    GtkWidget *help_btn = gtk_menu_item_new_with_label("Help");
    GtkWidget *about_btn = gtk_menu_item_new_with_label("About");

    gtk_menu_shell_append(GTK_MENU_SHELL(help_menu), help_btn);
    gtk_menu_shell_append(GTK_MENU_SHELL(help_menu), about_btn);
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(help_item), help_menu);

    g_signal_connect(help_btn, "activate", G_CALLBACK(on_help), app);
    g_signal_connect(about_btn, "activate", G_CALLBACK(on_about), app);

    // Add to menubar
    gtk_menu_shell_append(GTK_MENU_SHELL(menubar), file_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(menubar), tools_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(menubar), help_item);

    return menubar;
}
