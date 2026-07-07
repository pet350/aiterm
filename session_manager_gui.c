// part of aiterm project
// session_manager.c

#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <vte/vte.h>
#include <mariadb/mysql.h>

#include "gui.h"
#include "session_manager.h"
#include "session_manager_gui.h"
#include "utils.h"
#include "commands.h"

enum { COLUMN_CURRENT, COLUMN_UUID, COLUMN_DESC, COLUMN_COUNT, NUM_COLS };

void refresh_session_list(AppContext *app, GtkListStore *store) {
    if (store == NULL || !GTK_IS_LIST_STORE(store)) {
        DEBUG_PRINT("[DEBUG]: REFRESH_SESSION_LIST: ERROR: Invalid list store provided to refresh_session_list!\n");
        return;
    }

    gtk_list_store_clear(store);
    pthread_mutex_lock(&app->db_mutex);
    DEBUG_PRINT("[DEBUG]: REFRESH_SESSION_LIST: Locked DB mutex\n");

    char *query = "SELECT uuid, description, (SELECT COUNT(*) FROM aiterm_history WHERE session_uuid = s.uuid) FROM sessions s";
    DEBUG_PRINT("[DEBUG]: REFRESH_SESSION_LIST: Running Query %s\n", query);

    if (mysql_query(app->global_db_conn, query) == 0) {
        MYSQL_RES *res = mysql_store_result(app->global_db_conn);
        if (res) {
            MYSQL_ROW row;
            GtkTreeIter iter;
            while ((row = mysql_fetch_row(res))) {
                char *uuid = row[0];

                // Determine if this row matches our currently active session
                // Adjust app->current_session_uuid if your variable name differs!
                const char *current_marker = "";
                if (uuid && app->session.session_uuid && strcmp(uuid, app->session.session_uuid) == 0) {
                    current_marker = "*";
                }

                gtk_list_store_append(store, &iter);
                gtk_list_store_set(store, &iter,
                                   COLUMN_CURRENT, current_marker,
                                   COLUMN_UUID, uuid,
                                   COLUMN_DESC, row[1],
                                   COLUMN_COUNT, atoi(row[2]),
                                   -1);
            }
            mysql_free_result(res);
        }
    } else {
        g_print("Error fetching sessions: %s\n", mysql_error(app->global_db_conn));
    }
    pthread_mutex_unlock(&app->db_mutex);
    DEBUG_PRINT("[DEBUG]: REFRESH_SESSION_LIST: Unlocked DB mutex\n");
}

gboolean refresh_list_callback(gpointer data) {
    AppContext *app = (AppContext *)data;
    if (app && app->session_list_store) {
        DEBUG_PRINT("[DEBUG]: Refresh list callback: storing list\n");
        gtk_list_store_clear(app->session_list_store);
        refresh_session_list(app, app->session_list_store);
    }
    else {
        DEBUG_PRINT("[DEBUG]: Refresh list callback: Nothing to store!\n");
    }
    return FALSE;
}

gboolean timed_refresh_list_callback(gpointer data) {
    AppContext *app = (AppContext *)data;
    gboolean RV = TRUE;
    if (app && app->session_list_store) {
        DEBUG_PRINT("[DEBUG]: Timed Refresh list callback: storing list\n");
        gtk_list_store_clear(app->session_list_store);
        refresh_session_list(app, app->session_list_store);
    }
    else {
        DEBUG_PRINT("[DEBUG]: Timed Refresh list callback: Nothing to store!\n");
        RV = FALSE;
    }
    return RV;
}

void on_menu_session_manager(GtkMenuItem *item, gpointer data) {
    AppContext *app = (AppContext *)data;
    DEBUG_PRINT("[DEBUG]: Session Manager menu item clicked!\n");
    open_session_manager_window(app);
}

/* FIX #1: Extract the tree model from user_data and trigger an immediate refresh */
void on_add_clicked(GtkButton *btn, gpointer user_data) {
    if (user_data == NULL) {
        DEBUG_PRINT("[DEBUG]: on_add_clicked: user_data is NULL!\n");
        return;
    }
    if (global_app->global_db_conn == NULL) {
        DEBUG_PRINT("ERROR: global_app->global_db_conn is NULL\n");
        return;
    }

    DEBUG_PRINT("[DEBUG]: UI: Triggering CMD_SESSION_NEW\n");
    cmd_session_new(global_app, "New Session");

    // Re-fetch model from the incoming TreeView widget argument safely
    GtkTreeView *tree = GTK_TREE_VIEW(user_data);
    GtkTreeModel *model = gtk_tree_view_get_model(tree);
    if (model && GTK_IS_LIST_STORE(model)) {
        refresh_session_list(global_app, GTK_LIST_STORE(model));
    }
}

void on_load_clicked(GtkButton *btn, gpointer user_data) {
    GtkTreeView *tree = GTK_TREE_VIEW(user_data);
    GtkTreeSelection *sel = gtk_tree_view_get_selection(tree);
    GtkTreeModel *model;
    GtkTreeIter iter;

    if (gtk_tree_selection_get_selected(sel, &model, &iter)) {
        char *uuid;
        gtk_tree_model_get(model, &iter, COLUMN_UUID, &uuid, -1);
        DEBUG_PRINT("[DEBUG]: SESSION_MANAGER: Extracted UUID is: '%s'\n", uuid ? uuid : "NULL");
        cmd_session_load(global_app, uuid);
        g_free(uuid);
        DEBUG_PRINT("[DEBUG]: Loading session: %s\n", uuid);
        // Instant visual update when selection changes
        refresh_session_list(global_app, GTK_LIST_STORE(model));
    }
}

void on_default_clicked(GtkButton *btn, gpointer user_data) {
    GtkTreeView *tree = GTK_TREE_VIEW(user_data);
    GtkTreeSelection *sel = gtk_tree_view_get_selection(tree);
    GtkTreeModel *model;
    GtkTreeIter iter;

    if (gtk_tree_selection_get_selected(sel, &model, &iter)) {
        char *uuid;
        gtk_tree_model_get(model, &iter, COLUMN_UUID, &uuid, -1);
        DEBUG_PRINT("[DEBUG]: SESSION_MANAGER: Extracted UUID is: '%s'\n", uuid ? uuid : "NULL");
        cmd_session_default(global_app, uuid);
        g_free(uuid);
        
        // Force the layout to instantly update the asterisk location
        refresh_session_list(global_app, GTK_LIST_STORE(model));
    }
}

void on_delete_clicked(GtkButton *btn, gpointer user_data) {
    GtkTreeView *tree = GTK_TREE_VIEW(user_data);
    GtkTreeSelection *sel = gtk_tree_view_get_selection(tree);
    GtkTreeModel *model;
    GtkTreeIter iter;

    if (gtk_tree_selection_get_selected(sel, &model, &iter)) {
        char *uuid;
        gtk_tree_model_get(model, &iter, COLUMN_UUID, &uuid, -1);
        DEBUG_PRINT("[DEBUG]: SESSION_MANAGER: Extracted UUID is: '%s'\n", uuid ? uuid : "NULL");
        cmd_session_delete(global_app, uuid);
        g_free(uuid);
        refresh_session_list(global_app, GTK_LIST_STORE(model));
    }
}

void open_session_manager_window(AppContext *app) {
    GtkWidget *win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(win), "Session Manager");
    gtk_window_set_default_size(GTK_WINDOW(win), 600, 400);

    /* FIX #2: Apply class directly onto 'win' and its internal container structure */
    GtkStyleContext *context = gtk_widget_get_style_context(win);
    gtk_style_context_add_class(context, "session-dialog");

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    GtkStyleContext *vbox_context = gtk_widget_get_style_context(vbox);
    gtk_style_context_add_class(vbox_context, "session-dialog");

    gtk_container_add(GTK_CONTAINER(win), vbox);

    // List View setup
    GtkListStore *store = gtk_list_store_new(NUM_COLS,
                                         G_TYPE_STRING,  // COLUMN_CURRENT
                                         G_TYPE_STRING,  // COLUMN_UUID
                                         G_TYPE_STRING,  // COLUMN_DESC
                                         G_TYPE_INT);    // COLUMN_COUNT
    GtkWidget *tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));

    // Add Columns
    GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree), gtk_tree_view_column_new_with_attributes("Current", renderer, "text", COLUMN_CURRENT, NULL));
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree), gtk_tree_view_column_new_with_attributes("UUID", renderer, "text", COLUMN_UUID, NULL));
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree), gtk_tree_view_column_new_with_attributes("Description", renderer, "text", COLUMN_DESC, NULL));
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree), gtk_tree_view_column_new_with_attributes("Rows", renderer, "text", COLUMN_COUNT, NULL));

    GtkWidget *swin = gtk_scrolled_window_new(NULL, NULL);
    GtkStyleContext *swin_context = gtk_widget_get_style_context(swin);
    gtk_style_context_add_class(swin_context, "session-dialog");

    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(swin), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(swin), tree);
    gtk_box_pack_start(GTK_BOX(vbox), swin, TRUE, TRUE, 0);

    // Buttons
    GtkWidget *hbox = gtk_button_box_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 5);

    GtkWidget *btn_add = gtk_button_new_with_label("New Session");
    GtkWidget *btn_load = gtk_button_new_with_label("Load Session");
    GtkWidget *btn_default = gtk_button_new_with_label("Set Default");
    GtkWidget *btn_delete = gtk_button_new_with_label("Delete Session");

    gtk_box_pack_start(GTK_BOX(hbox), btn_add, FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(hbox), btn_load, FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(hbox), btn_default, FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(hbox), btn_delete, FALSE, FALSE, 5);

    // Connect signals
    g_signal_connect(btn_add, "clicked", G_CALLBACK(on_add_clicked), tree);
    g_signal_connect(btn_load, "clicked", G_CALLBACK(on_load_clicked), tree);
    g_signal_connect(btn_default, "clicked", G_CALLBACK(on_default_clicked), tree);
    g_signal_connect(btn_delete, "clicked", G_CALLBACK(on_delete_clicked), tree);

    refresh_session_list(global_app, store);

    // Store back context pointers if needed by the threaded background callback routine
    app->session_list_store = store; 

    g_timeout_add_seconds(30, timed_refresh_list_callback, app);
    gtk_widget_show_all(win);
}
