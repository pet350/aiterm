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
    pthread_mutex_lock(&app->access.db_mutex);
    DEBUG_PRINT("[DEBUG]: REFRESH_SESSION_LIST: Locked DB mutex\n");

    char *query = "SELECT uuid, description, (SELECT COUNT(*) FROM aiterm_history WHERE session_uuid = s.uuid) FROM sessions s";
    DEBUG_PRINT("[DEBUG]: REFRESH_SESSION_LIST: Running Query %s\n", query);

    if (mysql_query(app->database.global_db_conn, query) == 0) {
        MYSQL_RES *res = mysql_store_result(app->database.global_db_conn);
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
        g_print("Error fetching sessions: %s\n", mysql_error(app->database.global_db_conn));
    }
    pthread_mutex_unlock(&app->access.db_mutex);
    DEBUG_PRINT("[DEBUG]: REFRESH_SESSION_LIST: Unlocked DB mutex\n");
}

gboolean refresh_list_callback(gpointer data) {
    AppContext *app = (AppContext *)data;
    if (app && app->session.session_list_store) {
        DEBUG_PRINT("[DEBUG]: Refresh list callback: storing list\n");
        gtk_list_store_clear(app->session.session_list_store);
        refresh_session_list(app, app->session.session_list_store);
    }
    else {
        DEBUG_PRINT("[DEBUG]: Refresh list callback: Nothing to store!\n");
    }
    return FALSE;
}

gboolean timed_refresh_list_callback(gpointer data) {
    AppContext *app = (AppContext *)data;
    gboolean RV = TRUE;
    if (app && app->session.session_list_store) {
        DEBUG_PRINT("[DEBUG]: Timed Refresh list callback: storing list\n");
        gtk_list_store_clear(app->session.session_list_store);
        refresh_session_list(app, app->session.session_list_store);
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
    if (global_app->database.global_db_conn == NULL) {
        DEBUG_PRINT("ERROR: global_app->database.global_db_conn is NULL\n");
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

        DEBUG_PRINT("[DEBUG]: Loading session: %s\n", uuid);
        g_free(uuid);
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

void on_refresh_clicked(GtkButton *btn, gpointer user_data) {
    GtkTreeView *tree = GTK_TREE_VIEW(user_data);
    GtkTreeModel *model = gtk_tree_view_get_model(tree);
    if (model && GTK_IS_LIST_STORE(model)) {
        DEBUG_PRINT("[DEBUG]: UI: User forced a manual list store refresh\n");
        refresh_session_list(global_app, GTK_LIST_STORE(model));
    }
}

void on_rename_clicked(GtkButton *btn, gpointer user_data) {
    GtkTreeView *tree = GTK_TREE_VIEW(user_data);
    GtkTreeSelection *sel = gtk_tree_view_get_selection(tree);
    GtkTreeModel *model;
    GtkTreeIter iter;

    if (gtk_tree_selection_get_selected(sel, &model, &iter)) {
        char *uuid = NULL;
        char *old_desc = NULL;
        gtk_tree_model_get(model, &iter, COLUMN_UUID, &uuid, COLUMN_DESC, &old_desc, -1);

        if (!uuid) return;

        // Create an inline popup dialog to prompt for the new name
        GtkWidget *dialog = gtk_dialog_new_with_buttons("Rename Session",
                                GTK_WINDOW(global_app->manager.session),
                                GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                "Cancel", GTK_RESPONSE_CANCEL,
                                "Save", GTK_RESPONSE_ACCEPT,
                                NULL);

        GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
        GtkWidget *label = gtk_label_new("Enter a new description for this workspace session:");
        GtkWidget *entry = gtk_entry_new();

        // Pre-populate with the current description so they don't have to retype it entirely
        if (old_desc) {
            gtk_entry_set_text(GTK_ENTRY(entry), old_desc);
        }

        gtk_box_pack_start(GTK_BOX(content_area), label, FALSE, FALSE, 10);
        gtk_box_pack_start(GTK_BOX(content_area), entry, FALSE, FALSE, 10);
        gtk_widget_show_all(dialog);

        int response = gtk_dialog_run(GTK_DIALOG(dialog));
        if (response == GTK_RESPONSE_ACCEPT) {
            const char *new_desc = gtk_entry_get_text(GTK_ENTRY(entry));
            if (new_desc && strlen(new_desc) > 0) {
                pthread_mutex_lock(&global_app->access.db_mutex);

                // Safely handle SQL escaping to protect the MariaDB string context
                char *escaped_desc = malloc(strlen(new_desc) * 2 + 1);
                mysql_real_escape_string(global_app->database.global_db_conn, escaped_desc, new_desc, strlen(new_desc));

                char query[1024];
                snprintf(query, sizeof(query), "UPDATE sessions SET description = '%s' WHERE uuid = '%s'", escaped_desc, uuid);
                free(escaped_desc);

                DEBUG_PRINT("[DEBUG]: Executing Rename Query: %s\n", query);
                if (mysql_query(global_app->database.global_db_conn, query) != 0) {
                    g_print("Error executing database rename: %s\n", mysql_error(global_app->database.global_db_conn));
                }

                pthread_mutex_unlock(&global_app->access.db_mutex);

                // Instantly sync the UI window list
                refresh_session_list(global_app, GTK_LIST_STORE(model));
            }
        }

        gtk_widget_destroy(dialog);
        if (uuid) g_free(uuid);
        if (old_desc) g_free(old_desc);
    }
}

void on_dialog_session_manager_gui_destroy(AppContext *app) {
    global_app->manager.session = NULL;
}


void close_session_manager(AppContext *app) {
    if (app->manager.session != NULL) {
        // Destroying the window will trigger the "destroy" signal, 
        // which runs your existing on_dialog_session_manager_gui_destroy
        gtk_widget_destroy(app->manager.session);
        write_to_ai_pane(app, "System", "Closed session Manager Window.", "system_tag", "body_tag");
        app->manager.session = NULL;
    } else {
        write_to_ai_pane(app, "System", "session Manager is not open.", "system_tag", "body_tag");
    }
}
void open_session_manager_window(AppContext *app) {
    GtkWidget *win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    app->manager.session = win;
    gtk_window_set_title(GTK_WINDOW(win), "Session Manager");
    gtk_window_set_default_size(GTK_WINDOW(win), 600, 400);

    /* Apply class directly onto 'win' and its internal container structure */
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

    // ====================================================================
    // BUTTONS GRID (2x3 Layout Matrix)
    // ====================================================================
    GtkWidget *btn_grid = gtk_grid_new();

    gtk_grid_set_row_spacing(GTK_GRID(btn_grid), 6);
    gtk_grid_set_column_spacing(GTK_GRID(btn_grid), 6);
    gtk_grid_set_row_homogeneous(GTK_GRID(btn_grid), TRUE);
    gtk_grid_set_column_homogeneous(GTK_GRID(btn_grid), TRUE);

    // Instantiate your 6 Workspace Management Buttons
    GtkWidget *new_btn     = gtk_button_new_with_label("New");
    GtkWidget *load_btn    = gtk_button_new_with_label("Load");
    GtkWidget *refresh_btn = gtk_button_new_with_label("Refresh");
    GtkWidget *rename_btn  = gtk_button_new_with_label("Rename");
    GtkWidget *default_btn = gtk_button_new_with_label("Set Default"); 
    GtkWidget *delete_btn  = gtk_button_new_with_label("Delete");

    // --- TOP ROW (Row 0) ---
    gtk_grid_attach(GTK_GRID(btn_grid), new_btn,     0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(btn_grid), load_btn,    1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(btn_grid), refresh_btn, 2, 0, 1, 1);

    // --- BOTTOM ROW (Row 1) ---
    gtk_grid_attach(GTK_GRID(btn_grid), rename_btn,  0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(btn_grid), default_btn, 1, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(btn_grid), delete_btn,  2, 1, 1, 1);

    // Pack the unified grid container directly into the dialog vbox
    gtk_box_pack_start(GTK_BOX(vbox), btn_grid, FALSE, FALSE, 5);

    // Connect the signals using the correct new grid button variables
    g_signal_connect(new_btn,     "clicked", G_CALLBACK(on_add_clicked), tree);
    g_signal_connect(load_btn,    "clicked", G_CALLBACK(on_load_clicked), tree);
    g_signal_connect(refresh_btn, "clicked", G_CALLBACK(on_refresh_clicked), tree);
    g_signal_connect(rename_btn,  "clicked", G_CALLBACK(on_rename_clicked), tree);
    g_signal_connect(default_btn, "clicked", G_CALLBACK(on_default_clicked), tree);
    g_signal_connect(delete_btn,  "clicked", G_CALLBACK(on_delete_clicked), tree);

    refresh_session_list(global_app, store);

    // Store back context pointers for background callback sync routines
    app->session.session_list_store = store;

    g_timeout_add_seconds(30, timed_refresh_list_callback, app);
    gtk_widget_show_all(win);
}
