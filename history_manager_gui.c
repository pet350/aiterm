// part of aiterm project
// history_manager_gui.c
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

#include "history_manager_gui.h"
#include "gui.h"
#include "utils.h"
#include "commands.h"

void on_current_session_toggled(GtkToggleButton *togglebutton, gpointer user_data) {
    HistoryManagerDialog *dlg = (HistoryManagerDialog *)user_data;
    refresh_history_list(dlg);
}

void refresh_history_list(HistoryManagerDialog *dlg) {
    if (!dlg || !dlg->app) return;

    // Clear existing items out of the list store
    gtk_list_store_clear(dlg->list_store);

    gboolean filter_current = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(dlg->chk_current_session));
    char *query = NULL;

    if (filter_current) {
        const char *current_uuid = dlg->app->session.session_uuid; 
        if (!current_uuid || strlen(current_uuid) == 0) {
            current_uuid = "00000000-0000-0000-0000-000000000000"; // Fallback safe empty UUID
        }
        query = g_strdup_printf(
            "SELECT id, role, content, session_uuid FROM aiterm_history "
            "WHERE session_uuid = '%s' ORDER BY id DESC LIMIT 500", current_uuid);
    } else {
        query = g_strdup("SELECT id, role, content, session_uuid FROM aiterm_history ORDER BY id DESC LIMIT 500");
    }

    pthread_mutex_lock(&dlg->app->db_mutex);
    if (!dlg->app->global_db_conn) {
        pthread_mutex_unlock(&dlg->app->db_mutex);
        g_free(query);
        g_printerr("[ERROR]: History Manager: Database connection not active.\n");
        return;
    }

    if (mysql_query(dlg->app->global_db_conn, query) != 0) {
        g_printerr("[ERROR]: MySQL History query failed: %s\n", mysql_error(dlg->app->global_db_conn));
        pthread_mutex_unlock(&dlg->app->db_mutex);
        g_free(query);
        return;
    }

    g_free(query);

    MYSQL_RES *result = mysql_store_result(dlg->app->global_db_conn);
    if (!result) {
        pthread_mutex_unlock(&dlg->app->db_mutex);
        return;
    }

    MYSQL_ROW row;
    GtkTreeIter iter;

    while ((row = mysql_fetch_row(result))) {
        int id = atoi(row[0]);
        const char *role = row[1] ? row[1] : "N/A";
        const char *content = row[2] ? row[2] : "";
        const char *uuid = row[3] ? row[3] : "N/A";

        gtk_list_store_append(dlg->list_store, &iter);
        gtk_list_store_set(dlg->list_store, &iter,
                           COL_ID, id,
                           COL_ROLE, role,
                           COL_CONTENT, content,
                           COL_SESSION_UUID, uuid,
                           -1);
    }

    mysql_free_result(result);
    pthread_mutex_unlock(&dlg->app->db_mutex);
}

void on_delete_selected_clicked(GtkWidget *button, gpointer user_data) {
    HistoryManagerDialog *dlg = (HistoryManagerDialog *)user_data;
    GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(dlg->tree_view));
    GtkTreeModel *model;
    GtkTreeIter iter;

    if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
        int id;
        gtk_tree_model_get(model, &iter, COL_ID, &id, -1);

        pthread_mutex_lock(&dlg->app->db_mutex);
        if (dlg->app->global_db_conn) {
            char *query = g_strdup_printf("DELETE FROM aiterm_history WHERE id = %d", id);
            if (mysql_query(dlg->app->global_db_conn, query) != 0) {
                g_printerr("[ERROR]: Failed to delete history item: %s\n", mysql_error(dlg->app->global_db_conn));
            }
            g_free(query);
        }
        pthread_mutex_unlock(&dlg->app->db_mutex);

        refresh_history_list(dlg);
    }
}

// ADJUSTED: Includes a confirmation dialog and checks the filter criteria state
void on_clear_all_clicked(GtkWidget *button, gpointer user_data) {
    HistoryManagerDialog *dlg = (HistoryManagerDialog *)user_data;
    if (!dlg || !dlg->app) return;

    gboolean filter_current = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(dlg->chk_current_session));
    const char *msg = filter_current 
        ? "Are you sure you want to permanently clear history logs for the CURRENT SESSION ONLY?" 
        : "Are you sure you want to permanently wipe the ENTIRE global history log table?";

    // Create confirmation window pop-up
    GtkWidget *confirm_dialog = gtk_message_dialog_new(
        GTK_WINDOW(dlg->dialog),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        GTK_MESSAGE_QUESTION,
        GTK_BUTTONS_YES_NO,
        "%s", msg
    );
    gtk_window_set_title(GTK_WINDOW(confirm_dialog), "Confirm Data Destruction");

    // Halt thread sequence until choice is registered
    gint response = gtk_dialog_run(GTK_DIALOG(confirm_dialog));
    gtk_widget_destroy(confirm_dialog);

    // Escape code sequence if user backs out
    if (response != GTK_RESPONSE_YES) {
        return;
    }

    char *query = NULL;
    if (filter_current) {
        const char *current_uuid = dlg->app->session.session_uuid;
        if (!current_uuid || strlen(current_uuid) == 0) {
            current_uuid = "00000000-0000-0000-0000-000000000000";
        }
        query = g_strdup_printf("DELETE FROM aiterm_history WHERE session_uuid = '%s'", current_uuid);
    } else {
        query = g_strdup("TRUNCATE TABLE aiterm_history");
    }

    pthread_mutex_lock(&dlg->app->db_mutex);
    if (dlg->app->global_db_conn) {
        if (mysql_query(dlg->app->global_db_conn, query) != 0) {
            // Safe fallback condition for TRUNCATE failure cases
            if (!filter_current) {
                mysql_query(dlg->app->global_db_conn, "DELETE FROM aiterm_history");
            } else {
                g_printerr("[ERROR]: Failed to clear target data criteria: %s\n", mysql_error(dlg->app->global_db_conn));
            }
        }
    }
    pthread_mutex_unlock(&dlg->app->db_mutex);
    g_free(query);

    refresh_history_list(dlg);
}

void on_dialog_destroy(GtkWidget *widget, gpointer user_data) {
    HistoryManagerDialog *dlg = (HistoryManagerDialog *)user_data;
    g_free(dlg);
}

void open_history_manager_window(AppContext *app) {
    HistoryManagerDialog *dlg = g_new0(HistoryManagerDialog, 1);
    dlg->app = app;

    dlg->dialog = gtk_dialog_new();
    gtk_window_set_title(GTK_WINDOW(dlg->dialog), "History Manager");

    //if (app->window) {
    //    gtk_window_set_transient_for(GTK_WINDOW(dlg->dialog), GTK_WINDOW(app->window));
    //    gtk_window_set_destroy_with_parent(GTK_WINDOW(dlg->dialog), TRUE);
    // }

    gtk_window_set_default_size(GTK_WINDOW(dlg->dialog), 840, 480);
    gtk_window_set_skip_taskbar_hint(GTK_WINDOW(dlg->dialog), FALSE);
    gtk_window_set_type_hint(GTK_WINDOW(dlg->dialog), GDK_WINDOW_TYPE_HINT_NORMAL);

    GtkStyleContext *context = gtk_widget_get_style_context(dlg->dialog);
    gtk_style_context_add_class(context, "session-dialog");

    GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(dlg->dialog));
    gtk_style_context_add_class(gtk_widget_get_style_context(content_area), "session-dialog");

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 12);
    gtk_style_context_add_class(gtk_widget_get_style_context(vbox), "session-dialog");
    gtk_box_pack_start(GTK_BOX(content_area), vbox, TRUE, TRUE, 0);

    GtkWidget *filter_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_pack_start(GTK_BOX(vbox), filter_hbox, FALSE, FALSE, 0);

    dlg->chk_current_session = gtk_check_button_new_with_label("Show Current Session Only");
    gtk_box_pack_start(GTK_BOX(filter_hbox), dlg->chk_current_session, FALSE, FALSE, 0);
    g_signal_connect(dlg->chk_current_session, "toggled", G_CALLBACK(on_current_session_toggled), dlg);

    dlg->list_store = gtk_list_store_new(NUM_COLUMNS, G_TYPE_INT, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);

    dlg->tree_view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(dlg->list_store));
    g_object_unref(dlg->list_store);
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(dlg->tree_view), TRUE);

    GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
    GtkTreeViewColumn *column;

    // 1. ID Column
    column = gtk_tree_view_column_new_with_attributes("ID", renderer, "text", COL_ID, NULL);
    gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_FIXED);
    gtk_tree_view_column_set_fixed_width(column, 60);
    gtk_tree_view_append_column(GTK_TREE_VIEW(dlg->tree_view), column);

    // 2. Role Column
    column = gtk_tree_view_column_new_with_attributes("Role", renderer, "text", COL_ROLE, NULL);
    gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_FIXED);
    gtk_tree_view_column_set_fixed_width(column, 90);
    gtk_tree_view_append_column(GTK_TREE_VIEW(dlg->tree_view), column);

    // 3. Content Column
    renderer = gtk_cell_renderer_text_new();
    g_object_set(renderer, "ellipsize", PANGO_ELLIPSIZE_END, NULL);
    column = gtk_tree_view_column_new_with_attributes("Content Log", renderer, "text", COL_CONTENT, NULL);
    gtk_tree_view_column_set_resizable(column, TRUE);
    gtk_tree_view_column_set_expand(column, TRUE);
    gtk_tree_view_append_column(GTK_TREE_VIEW(dlg->tree_view), column);

    // 4. Session UUID Column
    renderer = gtk_cell_renderer_text_new();
    g_object_set(renderer, "ellipsize", PANGO_ELLIPSIZE_END, NULL);
    column = gtk_tree_view_column_new_with_attributes("Session UUID", renderer, "text", COL_SESSION_UUID, NULL);
    gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_FIXED);
    gtk_tree_view_column_set_fixed_width(column, 170);
    gtk_tree_view_append_column(GTK_TREE_VIEW(dlg->tree_view), column);

    GtkWidget *scrolled_window = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_window), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(scrolled_window), dlg->tree_view);
    gtk_box_pack_start(GTK_BOX(vbox), scrolled_window, TRUE, TRUE, 0);

    GtkWidget *button_box = gtk_button_box_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_button_box_set_layout(GTK_BUTTON_BOX(button_box), GTK_BUTTONBOX_END);
    gtk_box_set_spacing(GTK_BOX(button_box), 6);
    gtk_box_pack_start(GTK_BOX(vbox), button_box, FALSE, FALSE, 0);

    GtkWidget *btn_delete = gtk_button_new_with_label("Delete Selected");
    GtkWidget *btn_clear  = gtk_button_new_with_label("Clear History");
    GtkWidget *btn_close  = gtk_button_new_with_label("Close");

    gtk_container_add(GTK_CONTAINER(button_box), btn_delete);
    gtk_container_add(GTK_CONTAINER(button_box), btn_clear);
    gtk_container_add(GTK_CONTAINER(button_box), btn_close);

    g_signal_connect(btn_delete, "clicked", G_CALLBACK(on_delete_selected_clicked), dlg);
    g_signal_connect(btn_clear, "clicked", G_CALLBACK(on_clear_all_clicked), dlg);
    g_signal_connect_swapped(btn_close, "clicked", G_CALLBACK(gtk_widget_destroy), dlg->dialog);
    g_signal_connect(dlg->dialog, "destroy", G_CALLBACK(on_dialog_destroy), dlg);

    refresh_history_list(dlg);

    gtk_widget_show_all(dlg->dialog);
}
