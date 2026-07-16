// part of aiterm project
// noise_filter_manager_gui.c
// Various utilities used for session management
// By: Peter Talbott
// Assisted by: Gemini
// May 2026, June 2026

#include <mariadb/mysql.h>
#include <gtk/gtk.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "noise_filter_manager_gui.h"
#include "gui.h"
#include "utils.h"
#include "commands.h"

void refresh_noise_list(NoiseFilterDialog *dlg) {
    if (!dlg || !dlg->app) return;
    gtk_list_store_clear(dlg->list_store);

    pthread_mutex_lock(&dlg->app->access.db_mutex);
    if (!dlg->app->database.global_db_conn) {
        pthread_mutex_unlock(&dlg->app->access.db_mutex);
        g_printerr("[ERROR]: Noise Filter Manager: Database connection not active.\n");
        return;
    }

    const char *query = "SELECT id, pattern, uuid FROM noise_filters ORDER BY id ASC";
    if (mysql_query(dlg->app->database.global_db_conn, query) != 0) {
        g_printerr("[ERROR]: MySQL Noise Filter query failed: %s\n", mysql_error(dlg->app->database.global_db_conn));
        pthread_mutex_unlock(&dlg->app->access.db_mutex);
        return;
    }

    MYSQL_RES *result = mysql_store_result(dlg->app->database.global_db_conn);
    if (!result) {
        pthread_mutex_unlock(&dlg->app->access.db_mutex);
        return;
    }

    MYSQL_ROW row;
    GtkTreeIter iter;
    while ((row = mysql_fetch_row(result))) {
        int id = atoi(row[0]);
        const char *pattern = row[1] ? row[1] : "";
        const char *uuid = row[2] ? row[2] : "";

        gtk_list_store_append(dlg->list_store, &iter);
        gtk_list_store_set(dlg->list_store, &iter,
                           COL_NOISE_ID, id,
                           COL_NOISE_PATTERN, pattern,
                           COL_NOISE_UUID, uuid,
                           -1);
    }
    mysql_free_result(result);
    pthread_mutex_unlock(&dlg->app->access.db_mutex);
}

void on_add_pattern_clicked(GtkWidget *button, gpointer user_data) {
    NoiseFilterDialog *dlg = (NoiseFilterDialog *)user_data;
    const char *pattern = gtk_entry_get_text(GTK_ENTRY(dlg->entry_pattern));
    if (!pattern || strlen(pattern) == 0) return;

    pthread_mutex_lock(&dlg->app->access.db_mutex);
    if (dlg->app->database.global_db_conn) {
        unsigned long len = strlen(pattern);
        char *escaped = g_malloc(len * 2 + 1);
        mysql_real_escape_string(dlg->app->database.global_db_conn, escaped, pattern, len);

        char *query = g_strdup_printf("INSERT INTO noise_filters (pattern, uuid) VALUES ('%s', UUID())", escaped);
        if (mysql_query(dlg->app->database.global_db_conn, query) != 0) {
            g_printerr("[ERROR]: Failed to insert noise filter: %s\n", mysql_error(dlg->app->database.global_db_conn));
        }
        g_free(query);
        g_free(escaped);
    }
    pthread_mutex_unlock(&dlg->app->access.db_mutex);

    gtk_entry_set_text(GTK_ENTRY(dlg->entry_pattern), "");
    refresh_noise_list(dlg);
}

void on_delete_noise_clicked(GtkWidget *button, gpointer user_data) {
    NoiseFilterDialog *dlg = (NoiseFilterDialog *)user_data;
    GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(dlg->tree_view));
    GtkTreeModel *model;
    GtkTreeIter iter;

    if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
        int id;
        gtk_tree_model_get(model, &iter, COL_NOISE_ID, &id, -1);

        pthread_mutex_lock(&dlg->app->access.db_mutex);
        if (dlg->app->database.global_db_conn) {
            char *query = g_strdup_printf("DELETE FROM noise_filters WHERE id = %d", id);
            if (mysql_query(dlg->app->database.global_db_conn, query) != 0) {
                g_printerr("[ERROR]: Failed to delete noise filter: %s\n", mysql_error(dlg->app->database.global_db_conn));
            }
            g_free(query);
        }
        pthread_mutex_unlock(&dlg->app->access.db_mutex);

        refresh_noise_list(dlg);
    }
}

void on_dialog_noise_filter_Manager_destroy(GtkWidget *widget, gpointer user_data) {
    NoiseFilterDialog *dlg = (NoiseFilterDialog *)user_data;
    g_free(dlg);
    global_app->manager.noise = NULL;
}


void close_noise_manager(AppContext *app) {
    if (app->manager.noise != NULL) {
        // Destroying the window will trigger the "destroy" signal, 
        // which runs your existing on_dialog_noise_manager_gui_destroy
        gtk_widget_destroy(app->manager.noise);
        app->manager.noise = NULL;
        write_to_ai_pane(app, "System", "Closed noise Manager Window.", "ai_tag", "cmd_tag");
    } else {
        write_to_ai_pane(app, "System", "noise Manager is not open.", "ai_tag", "cmd_tag");
    }
}



void open_noise_filter_manager_window(AppContext *app) {
    NoiseFilterDialog *dlg = g_new0(NoiseFilterDialog, 1);
    dlg->app = app;

    // FIX: Instantiate as a native Top-Level Window for flawless KDE taskbar tracking
    dlg->dialog = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    app->manager.noise = dlg->dialog;
    gtk_window_set_title(GTK_WINDOW(dlg->dialog), "Noise Filter Manager");
    gtk_window_set_default_size(GTK_WINDOW(dlg->dialog), 600, 400);

    GtkStyleContext *context = gtk_widget_get_style_context(dlg->dialog);
    gtk_style_context_add_class(context, "session-dialog");

    // Master container packed directly into the window canvas area
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 12);
    gtk_style_context_add_class(gtk_widget_get_style_context(vbox), "session-dialog");
    gtk_container_add(GTK_CONTAINER(dlg->dialog), vbox);

    // Input Area for New Filtering Pattern
    GtkWidget *input_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *lbl_pattern = gtk_label_new("Filter Pattern:");
    dlg->entry_pattern = gtk_entry_new();
    GtkWidget *btn_add = gtk_button_new_with_label("Add Filter");

    gtk_box_pack_start(GTK_BOX(input_hbox), lbl_pattern, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(input_hbox), dlg->entry_pattern, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(input_hbox), btn_add, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), input_hbox, FALSE, FALSE, 4);

    // List Store Setup matching your exact headers
    dlg->list_store = gtk_list_store_new(NUM_NOISE_COLUMNS, G_TYPE_INT, G_TYPE_STRING, G_TYPE_STRING);
    dlg->tree_view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(dlg->list_store));
    g_object_unref(dlg->list_store);

    GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
    GtkTreeViewColumn *col_id = gtk_tree_view_column_new_with_attributes("ID", renderer, "text", COL_NOISE_ID, NULL);
    gtk_tree_view_column_set_fixed_width(col_id, 50);
    gtk_tree_view_append_column(GTK_TREE_VIEW(dlg->tree_view), col_id);

    GtkTreeViewColumn *col_pat = gtk_tree_view_column_new_with_attributes("Regex / Match Pattern", renderer, "text", COL_NOISE_PATTERN, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(dlg->tree_view), col_pat);

    GtkWidget *scrolled_window = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_window), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(scrolled_window), dlg->tree_view);
    gtk_box_pack_start(GTK_BOX(vbox), scrolled_window, TRUE, TRUE, 0);

    // Bottom Action Management Buttons Area
    GtkWidget *button_box = gtk_button_box_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_button_box_set_layout(GTK_BUTTON_BOX(button_box), GTK_BUTTONBOX_END);
    gtk_box_set_spacing(GTK_BOX(button_box), 6);
    gtk_box_pack_start(GTK_BOX(vbox), button_box, FALSE, FALSE, 0);

    GtkWidget *btn_delete = gtk_button_new_with_label("Delete Selected");
    GtkWidget *btn_close = gtk_button_new_with_label("Close");

    gtk_container_add(GTK_CONTAINER(button_box), btn_delete);
    gtk_container_add(GTK_CONTAINER(button_box), btn_close);

    // Wire signals up clean
    g_signal_connect(btn_add, "clicked", G_CALLBACK(on_add_pattern_clicked), dlg);
    g_signal_connect(btn_delete, "clicked", G_CALLBACK(on_delete_noise_clicked), dlg);
    g_signal_connect_swapped(btn_close, "clicked", G_CALLBACK(gtk_widget_destroy), dlg->dialog);
    g_signal_connect(dlg->dialog, "destroy", G_CALLBACK(on_dialog_noise_filter_Manager_destroy), dlg);

    refresh_noise_list(dlg);
    gtk_widget_show_all(dlg->dialog);
}
