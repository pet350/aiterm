// part of aiterm project
// menu.c
// Function for handling GUI Menu events
// By: Peter Talbott
// Assisted by: Gemini
// May 2026

#include <vte/vte.h>
#include <mariadb/mysql.h>
#include <gtk/gtk.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "menu.h"
#include "commands.h"
#include "gui.h"
#include "update.h"
#include "help.h"
#include "utils.h"
#include "terminal.h"
#include "tee_handler.h"
#include "toggles.h"
#include "config.h"
#include "history_manager_gui.h"
#include "noise_filter_manager_gui.h"
#include "policy_manager_gui.h"

// Memory clean up helper for signal hooks
void free_menu_data(gpointer data, GClosure *closure) {
    MenuCommandData *mdata = (MenuCommandData *)data;
    if (mdata) {
        g_free(mdata->cmd_base);
        g_free(mdata);
    }
}

// Modal dialog generator to fetch runtime parameters for specialized commands
char* prompt_for_argument(GtkWindow *parent, char *action_title, char *placeholder) {
    GtkWidget *dialog = gtk_dialog_new_with_buttons(
        action_title,
        parent,
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Execute", GTK_RESPONSE_OK,
        NULL
    );

    GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *label = gtk_label_new(placeholder);
    GtkWidget *entry = gtk_entry_new();

    gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
    gtk_box_pack_start(GTK_BOX(content_area), label, FALSE, FALSE, 8);
    gtk_box_pack_start(GTK_BOX(content_area), entry, FALSE, FALSE, 8);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);

    gtk_widget_show_all(dialog);

    char *result = NULL;
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
        const char *text = gtk_entry_get_text(GTK_ENTRY(entry));
        if (text && strlen(text) > 0) {
            result = g_strdup(text);
        }
    }

    gtk_widget_destroy(dialog);
    return result;
}

// --- Menu Callbacks ---

void on_menu_history_manager_activate(GtkMenuItem *menuitem, gpointer user_data) {
    AppContext *app = (AppContext *)user_data;
    open_history_manager_window(app);
}

void on_menu_noise_filter_manager_activate(GtkMenuItem *menuitem, gpointer user_data) {
    AppContext *app = (AppContext *)user_data;
    open_noise_filter_manager_window(app);
}

void on_menu_policy_manager_activate(GtkMenuItem *menuitem, gpointer user_data) {
    AppContext *app = (AppContext *)user_data;
    open_policy_manager_window(app);
}

void on_clear(GtkWidget *widget, gpointer data) {
    AppContext *app = (AppContext *)data;
    GtkTextBuffer *buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(app->gemini_view));
    gtk_text_buffer_set_text(buf, "", -1);
}

void on_menu_exit(GtkWidget *widget, gpointer data) {
    gtk_main_quit();
}

void on_help(GtkWidget *widget, gpointer data) {
    AppContext *app = (AppContext *)data;
    write_to_ai_pane(app, "[ Help System ]\n", get_help_text(), "cmd_tag", "cmd_tag");
}

void on_about(GtkWidget *widget, gpointer data) {
    AppContext *app = (AppContext *)data;
    write_to_ai_pane(app, "[ Version Info ]\n", get_version_info(), "cmd_tag", "cmd_tag");
}

void on_copy(GtkWidget *widget, gpointer data) {
    AppContext *app = (AppContext *)data;
    vte_terminal_copy_clipboard_format(VTE_TERMINAL(app->terminal_view), VTE_FORMAT_TEXT);
}

void on_paste(GtkWidget *widget, gpointer data) {
    AppContext *app = (AppContext *)data;
    vte_terminal_paste_clipboard(VTE_TERMINAL(app->terminal_view));
}

// Forward declarations for mutual dependency blocking
void on_tee_toggle(GtkWidget *widget, gpointer data);
void on_autoreply_toggle(GtkWidget *widget, gpointer data);

void on_tee_toggle(GtkWidget *widget, gpointer data) {
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

void on_autoreply_toggle(GtkWidget *widget, gpointer data) {
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


// --- Visual Preferences Callbacks ---
void on_transparency_changed(GtkRange *range, gpointer data) {
    AppContext *app = (AppContext *)data;
    app->transparency = gtk_range_get_value(range);
    apply_visual_settings(app);
}

void on_ai_transparency_changed(GtkRange *range, gpointer data) {
    AppContext *app = (AppContext *)data;
    app->ai_transparency = gtk_range_get_value(range);
    apply_visual_settings(app);
}

void on_terminal_font_set(GtkFontButton *btn, gpointer data) {
    AppContext *app = (AppContext *)data;
    if (app->terminal_font) free(app->terminal_font);
    app->terminal_font = strdup(gtk_font_chooser_get_font(GTK_FONT_CHOOSER(btn)));
    apply_visual_settings(app);
}

void on_ai_font_set(GtkFontButton *btn, gpointer data) {
    AppContext *app = (AppContext *)data;
    if (app->ai_font) free(app->ai_font);
    app->ai_font = strdup(gtk_font_chooser_get_font(GTK_FONT_CHOOSER(btn)));
    apply_visual_settings(app);
}

static void on_preferences_response(GtkDialog *dialog, gint response_id, gpointer data) {
    AppContext *app = (AppContext *)data;

    if (response_id == GTK_RESPONSE_ACCEPT) {
        save_config(app);
    }
    gtk_widget_destroy(GTK_WIDGET(dialog));
}

void on_preferences(GtkWidget *widget, gpointer data) {
    AppContext *app = (AppContext *)data;

    GtkWidget *dialog = gtk_dialog_new_with_buttons("Preferences", 
                                                  GTK_WINDOW(app->window),
                                                  GTK_DIALOG_DESTROY_WITH_PARENT,
                                                  "Save", GTK_RESPONSE_ACCEPT,
                                                  "Close", GTK_RESPONSE_CLOSE, 
                                                  NULL);

    GtkStyleContext *dialog_context = gtk_widget_get_style_context(dialog);
    gtk_style_context_add_class(dialog_context, "session-dialog");

    GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkStyleContext *content_context = gtk_widget_get_style_context(content_area);
    gtk_style_context_add_class(content_context, "session-dialog");

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    GtkStyleContext *vbox_context = gtk_widget_get_style_context(vbox);
    gtk_style_context_add_class(vbox_context, "session-dialog");

    gtk_container_set_border_width(GTK_CONTAINER(vbox), 15);
    gtk_box_pack_start(GTK_BOX(content_area), vbox, TRUE, TRUE, 0);

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

    g_signal_connect(dialog, "response", G_CALLBACK(on_preferences_response), app);
    gtk_widget_show_all(dialog);
}

void on_tee_flush(GtkWidget *widget, gpointer data) {
    AppContext *app = (AppContext *)data;
    tee_flush_timed(app);
    write_to_ai_pane(app, "System: ", "Tee buffer manual flush triggered.", "cmd_tag", "cmd_tag");
}

// Unified menu action handler routing commands back into the main engine pipeline
void on_menu_command_clicked(GtkMenuItem *menuitem, gpointer user_data) {
    MenuCommandData *data = (MenuCommandData *)user_data;
    AppContext *app = data->app;

    if (data->requires_arg) {
        char *prompt_msg = g_strdup_printf("Provide parameters for: /%s", data->cmd_base);
        char *arg = prompt_for_argument(GTK_WINDOW(app->window), data->cmd_base, prompt_msg);
        g_free(prompt_msg);

        if (arg) {
            char *full_cmd = g_strdup_printf("%s %s", data->cmd_base, arg);
            execute_command(app, full_cmd);
            g_free(full_cmd);
            g_free(arg);
        }
    } else {
        execute_command(app, data->cmd_base);
    }
}

// Core abstract factory helper to map items safely into target submenus
void append_ai_action(GtkWidget *menu, const char *label, const char *cmd, gboolean requires_arg, AppContext *app) {
    GtkWidget *item = gtk_menu_item_new_with_label(label);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

    MenuCommandData *data = g_new0(MenuCommandData, 1);
    data->app = app;
    data->cmd_base = g_strdup(cmd);
    data->requires_arg = requires_arg;

    g_signal_connect_data(item, "activate", G_CALLBACK(on_menu_command_clicked), data, (GClosureNotify)free_menu_data, 0);
}


void sync_toggle_ui_elements(AppContext *app) {
    if (!app) return;


    // Sync Autoreply Checkbox
    if (app->ui.toggle_autoreply) {
        g_signal_handlers_block_by_func(app->ui.toggle_autoreply, G_CALLBACK(on_menu_toggle_item_toggled), NULL);
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(app->ui.toggle_autoreply), app->autoreply_enabled);
        g_signal_handlers_unblock_by_func(app->ui.toggle_autoreply, G_CALLBACK(on_menu_toggle_item_toggled), NULL);
    }

    // Sync Autoexe Checkbox
    if (app->ui.toggle_autoexe) {
        g_signal_handlers_block_by_func(app->ui.toggle_autoexe, G_CALLBACK(on_menu_toggle_item_toggled), NULL);
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(app->ui.toggle_autoexe), app->auto_execute_enabled);
        g_signal_handlers_unblock_by_func(app->ui.toggle_autoexe, G_CALLBACK(on_menu_toggle_item_toggled), NULL);
    }

    // Sync Tee Checkbox
    if (app->ui.toggle_tee) {
        g_signal_handlers_block_by_func(app->ui.toggle_tee, G_CALLBACK(on_menu_toggle_item_toggled), NULL);
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(app->ui.toggle_tee), app->tee_enabled);
        g_signal_handlers_unblock_by_func(app->ui.toggle_tee, G_CALLBACK(on_menu_toggle_item_toggled), NULL);
    }

    // Sync Noise Filter Checkbox
    if (app->ui.toggle_noise_filter) {
        g_signal_handlers_block_by_func(app->ui.toggle_noise_filter, G_CALLBACK(on_menu_toggle_item_toggled), NULL);
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(app->ui.toggle_noise_filter), app->noise_filter_enabled);
        g_signal_handlers_unblock_by_func(app->ui.toggle_noise_filter, G_CALLBACK(on_menu_toggle_item_toggled), NULL);
    }

    // Sync Smart Cache Checkbox
    if (app->ui.toggle_smart_cache) {
        g_signal_handlers_block_by_func(app->ui.toggle_smart_cache, G_CALLBACK(on_menu_toggle_item_toggled), NULL);
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(app->ui.toggle_smart_cache), app->smart_cache_enabled);
        g_signal_handlers_unblock_by_func(app->ui.toggle_smart_cache, G_CALLBACK(on_menu_toggle_item_toggled), NULL);
    }

    // Sync ratelimit Checkbox
    if (app->ui.toggle_ratelimit) {
        g_signal_handlers_block_by_func(app->ui.toggle_ratelimit, G_CALLBACK(on_menu_toggle_item_toggled), NULL);
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(app->ui.toggle_ratelimit), app->ratelimit_enabled);
        g_signal_handlers_unblock_by_func(app->ui.toggle_ratelimit, G_CALLBACK(on_menu_toggle_item_toggled), NULL);
    }

    // Sync session_write_global Checkbox
    if (app->ui.toggle_session_write_global) {
        g_signal_handlers_block_by_func(app->ui.toggle_session_write_global, G_CALLBACK(on_menu_toggle_item_toggled), NULL);
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(app->ui.toggle_session_write_global), app->session.write_to_global);
        g_signal_handlers_unblock_by_func(app->ui.toggle_session_write_global, G_CALLBACK(on_menu_toggle_item_toggled), NULL);
    }

    // Sync session_read_global Checkbox
    if (app->ui.toggle_session_read_global) {
        g_signal_handlers_block_by_func(app->ui.toggle_session_read_global, G_CALLBACK(on_menu_toggle_item_toggled), NULL);
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(app->ui.toggle_session_read_global), app->session.read_from_global);
        g_signal_handlers_unblock_by_func(app->ui.toggle_session_read_global, G_CALLBACK(on_menu_toggle_item_toggled), NULL);
    }

}


// --- Menu Builder ---

GtkWidget* create_menu_bar(AppContext *app) {
    GtkWidget *menu_bar = gtk_menu_bar_new();
    GtkWidget *file_menu = gtk_menu_new();
    GtkWidget *file_item = gtk_menu_item_new_with_label("File");
    GtkWidget *clear_item = gtk_menu_item_new_with_label("Clear AI History");
    GtkWidget *exit_item = gtk_menu_item_new_with_label("Exit");
    GtkWidget *edit_menu = gtk_menu_new();
    GtkWidget *edit_item = gtk_menu_item_new_with_label("Edit");
    GtkWidget *copy_item = gtk_menu_item_new_with_label("Copy Terminal");
    GtkWidget *paste_item = gtk_menu_item_new_with_label("Paste Terminal");

    GtkWidget *managers_menu = gtk_menu_new();
    GtkWidget *managers_root = gtk_menu_item_new_with_label("Managers");
    GtkWidget *session_item  = gtk_menu_item_new_with_label("Session Manager");
    GtkWidget *menu_item_history = gtk_menu_item_new_with_label("History Manager");
    GtkWidget *menu_item_noise   = gtk_menu_item_new_with_label("Noise Filter Manager");
    GtkWidget *menu_item_policy  = gtk_menu_item_new_with_label("Policy Manager");

    GtkWidget *tools_menu = gtk_menu_new();
    GtkWidget *tools_item = gtk_menu_item_new_with_label("Tools");
    GtkWidget *ai_root_item = gtk_menu_item_new_with_label("AI Controls");
    GtkWidget *ai_main_menu = gtk_menu_new();
    GtkWidget *sys_item = gtk_menu_item_new_with_label("System & Help");
    GtkWidget *sys_menu = gtk_menu_new();
    GtkWidget *cfg_item = gtk_menu_item_new_with_label("Configuration");
    GtkWidget *cfg_menu = gtk_menu_new();
    GtkWidget *toggle_item = gtk_menu_item_new_with_label("Toggles");
    GtkWidget *toggle_menu = gtk_menu_new();
    GtkWidget *tee_flush = gtk_menu_item_new_with_label("Flush Tee Buffer");
    GtkWidget *pref_item = gtk_menu_item_new_with_label("Preferences");
    GtkWidget *help_menu = gtk_menu_new();
    GtkWidget *help_item = gtk_menu_item_new_with_label("Help");
    GtkWidget *help_btn = gtk_menu_item_new_with_label("Help Content");
    GtkWidget *about_btn = gtk_menu_item_new_with_label("About");
    GtkWidget *sess_item = gtk_menu_item_new_with_label("Sessions");
    GtkWidget *sess_menu = gtk_menu_new();
    GtkWidget *noise_item = gtk_menu_item_new_with_label("Noise Filters");
    GtkWidget *noise_menu = gtk_menu_new();

    append_ai_action(sys_menu, "Standard Help", "help", FALSE, app);
    append_ai_action(sys_menu, "Extended Help", "extended help", FALSE, app);
    append_ai_action(sys_menu, "Operational Status", "status", FALSE, app);
    append_ai_action(sys_menu, "Feature Changelog", "features", FALSE, app);
    append_ai_action(sys_menu, "Hardware Analysis", "hw", FALSE, app);
    append_ai_action(sys_menu, "Database History Logs", "history", FALSE, app);
    append_ai_action(sys_menu, "Application Version", "version", FALSE, app);
    append_ai_action(sys_menu, "Clear AI Response Pane", "clear", FALSE, app);
    append_ai_action(sys_menu, "Force Reset AI Async States", "reset state", FALSE, app);
    
    append_ai_action(cfg_menu, "Show Active Provider Info", "provider", FALSE, app);
    append_ai_action(cfg_menu, "Query Available Remote Models", "list models", FALSE, app);
    append_ai_action(cfg_menu, "Reload from Conf File", "load config", FALSE, app);
    append_ai_action(cfg_menu, "Save to Conf File", "save config", FALSE, app);
    append_ai_action(cfg_menu, "Configure Requests Per Minute (RPM)", "rpm", TRUE, app);
    append_ai_action(cfg_menu, "Force Re-establish Database Sockets", "reset db", FALSE, app);

    GtkWidget *item;

    // --- Toggles Submenu Population via setup_menu_toggle() ---
    item = gtk_check_menu_item_new_with_label("Toggle Real-Time Prompt Analysis");
    setup_menu_toggle(item, app, TOGGLE_AUTOREPLY, app->autoreply_enabled);
    gtk_menu_shell_append(GTK_MENU_SHELL(toggle_menu), item);
    gtk_widget_show(item);

    item = gtk_check_menu_item_new_with_label("Toggle AI Payload Auto-Execution");
    setup_menu_toggle(item, app, TOGGLE_AUTOEXE, app->auto_execute_enabled);
    gtk_menu_shell_append(GTK_MENU_SHELL(toggle_menu), item);
    gtk_widget_show(item);

    item = gtk_check_menu_item_new_with_label("Toggle Immediate Terminal Capturing (Tee)");
    setup_menu_toggle(item, app, TOGGLE_TEE, app->tee_enabled);
    gtk_menu_shell_append(GTK_MENU_SHELL(toggle_menu), item);
    gtk_widget_show(item);

    item = gtk_check_menu_item_new_with_label("Toggle Mitigation Noise Filters");
    setup_menu_toggle(item, app, TOGGLE_NOISE_FILTER, app->noise_filter_enabled);
    gtk_menu_shell_append(GTK_MENU_SHELL(toggle_menu), item);
    gtk_widget_show(item);

    item = gtk_check_menu_item_new_with_label("Toggle Semantic Local Caching");
    setup_menu_toggle(item, app, TOGGLE_SMART_CACHE, app->smart_cache_enabled);
    gtk_menu_shell_append(GTK_MENU_SHELL(toggle_menu), item);
    gtk_widget_show(item);

    item = gtk_check_menu_item_new_with_label("Toggle Active Rate Limiting Protection");
    setup_menu_toggle(item, app, TOGGLE_RATELIMIT, app->ratelimit_enabled);
    gtk_menu_shell_append(GTK_MENU_SHELL(toggle_menu), item);
    gtk_widget_show(item);

    item = gtk_check_menu_item_new_with_label("Toggle read from global database");
    setup_menu_toggle(item, app, TOGGLE_SESSION_READ_GLOBAL, app->session.read_from_global);
    gtk_menu_shell_append(GTK_MENU_SHELL(toggle_menu), item);
    gtk_widget_show(item);

    item = gtk_check_menu_item_new_with_label("Toggle write to global database");
    setup_menu_toggle(item, app, TOGGLE_SESSION_WRITE_GLOBAL, app->session.write_to_global);
    gtk_menu_shell_append(GTK_MENU_SHELL(toggle_menu), item);
    gtk_widget_show(item);

    // AI Context command shortcuts
    append_ai_action(sess_menu, "Initialize New Session Iteration", "session new", FALSE, app);
    append_ai_action(sess_menu, "List Saved Persistent Contexts", "session list", FALSE, app);
    append_ai_action(sess_menu, "Inspect Active Working Environment Metadata", "session show", FALSE, app);
    append_ai_action(sess_menu, "Load Previous Context via UUID", "session load", TRUE, app);
    append_ai_action(sess_menu, "Update Current Environment Description", "session description", TRUE, app);
    append_ai_action(sess_menu, "Persist Active Workspace Configuration As Default", "session default", TRUE, app);
    append_ai_action(sess_menu, "Clear Persistent Default Settings", "session no default", FALSE, app);
    append_ai_action(sess_menu, "Purge Historical Database Context Loop", "session delete", TRUE, app);
    append_ai_action(sess_menu, "Toggle Reading From Global Context (0000...)", "session read from global", FALSE, app);
    append_ai_action(sess_menu, "Toggle Writing Out into Global Broadcast Stream", "session write to global", FALSE, app);
    
    append_ai_action(noise_menu, "List Active Cleaning Filters", "noise list", FALSE, app);
    append_ai_action(noise_menu, "Register New Suppression Rule (Regex)", "noise add", TRUE, app);
    append_ai_action(noise_menu, "Remove Suppression Rule (ID/Pattern)", "noise delete", TRUE, app);

    // Signals Binding
    g_signal_connect(G_OBJECT(menu_item_history), "activate", G_CALLBACK(on_menu_history_manager_activate), app);
    g_signal_connect(G_OBJECT(menu_item_noise), "activate", G_CALLBACK(on_menu_noise_filter_manager_activate), app);
    g_signal_connect(G_OBJECT(menu_item_policy), "activate", G_CALLBACK(on_menu_policy_manager_activate), app);
    g_signal_connect(clear_item, "activate", G_CALLBACK(on_clear), app);
    g_signal_connect(exit_item, "activate", G_CALLBACK(on_menu_exit), app);
    g_signal_connect(copy_item, "activate", G_CALLBACK(on_copy), app);
    g_signal_connect(paste_item, "activate", G_CALLBACK(on_paste), app);
    g_signal_connect(tee_flush, "activate", G_CALLBACK(on_tee_flush), app);
    g_signal_connect(pref_item, "activate", G_CALLBACK(on_preferences), app);
    g_signal_connect(help_btn, "activate", G_CALLBACK(on_help), app);
    g_signal_connect(about_btn, "activate", G_CALLBACK(on_about), app);
    g_signal_connect(session_item, "activate", G_CALLBACK(on_menu_session_manager), app);

    // Menu Assembly: Main menu_bar mapping
    gtk_menu_shell_append(GTK_MENU_SHELL(menu_bar), file_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu_bar), edit_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu_bar), managers_root);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu_bar), tools_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu_bar), ai_root_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu_bar), help_item);

    // Menu Assembly: Submenus
    gtk_menu_shell_append(GTK_MENU_SHELL(file_menu), clear_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(file_menu), exit_item);

    gtk_menu_shell_append(GTK_MENU_SHELL(edit_menu), copy_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(edit_menu), paste_item);

    gtk_menu_shell_append(GTK_MENU_SHELL(managers_menu), session_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(managers_menu), menu_item_history);
    gtk_menu_shell_append(GTK_MENU_SHELL(managers_menu), menu_item_noise);
    gtk_menu_shell_append(GTK_MENU_SHELL(managers_menu), menu_item_policy);

    gtk_menu_shell_append(GTK_MENU_SHELL(ai_main_menu), cfg_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(ai_main_menu), toggle_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(ai_main_menu), sess_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(ai_main_menu), sys_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(ai_main_menu), noise_item);

    // Tools Submenu layout is now concise and uniform
    gtk_menu_shell_append(GTK_MENU_SHELL(tools_menu), tee_flush);
    gtk_menu_shell_append(GTK_MENU_SHELL(tools_menu), pref_item);

    gtk_menu_shell_append(GTK_MENU_SHELL(help_menu), help_btn);
    gtk_menu_shell_append(GTK_MENU_SHELL(help_menu), about_btn);

    // Connect structural branches back up to headers
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(file_item), file_menu);
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(edit_item), edit_menu);
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(tools_item), tools_menu);
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(ai_root_item), ai_main_menu);
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(managers_root), managers_menu);
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(sys_item), sys_menu);
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(cfg_item), cfg_menu);
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(toggle_item), toggle_menu);
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(noise_item), noise_menu);
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(sess_item), sess_menu);
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(help_item), help_menu);

    return menu_bar;
}


