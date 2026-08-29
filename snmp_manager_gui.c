// part of aiterm project
// snmp_manager_gui.c
// Graphical interface for SNMP target management
// By: Peter Talbott
// Assisted by: Gemini
// August 2026

#include <mariadb/mysql.h>
#include <gtk/gtk.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "snmp_manager_gui.h"
#include "snmp_manager.h"
#include "gui.h"
#include "utils.h"
#include "commands.h" 

extern AppContext *global_app;

// OID Preset Registry Definition
typedef struct {
    const char *label;
    const char *oid;
} OidPreset;

static const OidPreset OID_REGISTRY[] = {
    {"System Description (sysDescr.0)", "1.3.6.1.2.1.1.1.0"},
    {"System Uptime (sysUpTime.0)",     "1.3.6.1.2.1.1.3.0"},
    {"Host CPU 1-Min Load",             "1.3.6.1.4.1.2021.10.1.3.1"},
    {"Host CPU 5-Min Load",             "1.3.6.1.4.1.2021.10.1.3.2"},
    {"Host CPU 15-Min Load",            "1.3.6.1.4.1.2021.10.1.3.3"},
    {"Total Free RAM (memTotalFree.0)", "1.3.6.1.4.1.2021.4.6.0"},
    {"Total Free Swap (memSwapError.0)","1.3.6.1.4.1.2021.4.4.0"},
    {"Custom OID...",                   ""}
};
#define OID_REGISTRY_COUNT (sizeof(OID_REGISTRY) / sizeof(OID_REGISTRY[0]))

// TreeView Column Definitions
enum {
    COL_ID = 0,
    COL_LABEL,
    COL_IP,
    COL_COMMUNITY,
    COL_OID,
    COL_VALUE,
    COL_ACTIVE,
    NUM_COLUMNS
};

// Forward declarations
void refresh_snmp_target_list(SnmpManagerDialog *dlg);

// Handler for OID Preset Combo Box selection change
void on_oid_combo_changed(GtkComboBox *combo, gpointer user_data) {
    GtkWidget *oid_entry = GTK_WIDGET(user_data);
    gint active = gtk_combo_box_get_active(combo);
    if (active >= 0 && active < (gint)(OID_REGISTRY_COUNT - 1)) {
        gtk_entry_set_text(GTK_ENTRY(oid_entry), OID_REGISTRY[active].oid);
    }
}

// Refresh table contents from MariaDB database
void refresh_snmp_target_list(SnmpManagerDialog *dlg) {
    if (!dlg || !dlg->app) return;

    gtk_list_store_clear(dlg->list_store);

    pthread_mutex_lock(&dlg->app->access.db_mutex);
    if (!dlg->app->database.global_db_conn) {
        pthread_mutex_unlock(&dlg->app->access.db_mutex);
        g_printerr("[ERROR]: SNMP Manager: Database connection not active.\n");
        return;
    }

    const char *query = "SELECT id, label, ip_address, community, oid_str, last_value, is_active FROM snmp_targets ORDER BY id ASC";

    if (mysql_query(dlg->app->database.global_db_conn, query) != 0) {
        g_printerr("[ERROR]: MySQL SNMP query failed: %s\n", mysql_error(dlg->app->database.global_db_conn));
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
        int id                  = atoi(row[0]);
        const char *label       = row[1] ? row[1] : "N/A";
        const char *ip          = row[2] ? row[2] : "N/A";
        const char *community   = row[3] ? row[3] : "public";
        const char *oid         = row[4] ? row[4] : "N/A";
        const char *value       = row[5] ? row[5] : "PENDING";
        int is_active_int       = row[6] ? atoi(row[6]) : 0;
        const char *active_str  = is_active_int ? "Yes" : "No";

        gtk_list_store_append(dlg->list_store, &iter);
        gtk_list_store_set(dlg->list_store, &iter,
                           COL_ID, id,
                           COL_LABEL, label,
                           COL_IP, ip,
                           COL_COMMUNITY, community,
                           COL_OID, oid,
                           COL_VALUE, value,
                           COL_ACTIVE, active_str,
                           -1);
    }

    mysql_free_result(result);
    pthread_mutex_unlock(&dlg->app->access.db_mutex);

    // Sync in-memory poller array with database state
    snmp_load_targets_from_db(dlg->app);
}

// Show Add SNMP Target Modal Dialog
void show_add_snmp_dialog(SnmpManagerDialog *dlg) {
    GtkWidget *dialog = gtk_dialog_new_with_buttons(
        "Add SNMP Target",
        GTK_WINDOW(dlg->dialog),
        GTK_DIALOG_DESTROY_WITH_PARENT,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Save", GTK_RESPONSE_ACCEPT,
        NULL
    );

    // Apply 'session-dialog' theme class to sub-dialog
    GtkStyleContext *context = gtk_widget_get_style_context(dialog);
    gtk_style_context_add_class(context, "session-dialog");

    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_style_context_add_class(gtk_widget_get_style_context(content), "session-dialog");

    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
    gtk_container_set_border_width(GTK_CONTAINER(grid), 12);

    GtkWidget *label_entry = gtk_entry_new();
    GtkWidget *ip_entry = gtk_entry_new();
    GtkWidget *community_entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(community_entry), "public");
    GtkWidget *oid_entry = gtk_entry_new();

    GtkWidget *oid_combo = gtk_combo_box_text_new();
    for (size_t i = 0; i < OID_REGISTRY_COUNT; i++) {
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(oid_combo), OID_REGISTRY[i].label);
    }
    gtk_combo_box_set_active(GTK_COMBO_BOX(oid_combo), 0);
    gtk_entry_set_text(GTK_ENTRY(oid_entry), OID_REGISTRY[0].oid);

    g_signal_connect(oid_combo, "changed", G_CALLBACK(on_oid_combo_changed), oid_entry);

    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Label:"), 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), label_entry, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("IP / Hostname:"), 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), ip_entry, 1, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Community String:"), 0, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), community_entry, 1, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("OID Preset:"), 0, 3, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), oid_combo, 1, 3, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("OID String:"), 0, 4, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), oid_entry, 1, 4, 1, 1);

    gtk_container_add(GTK_CONTAINER(content), grid);
    gtk_widget_show_all(dialog);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        const char *lbl  = gtk_entry_get_text(GTK_ENTRY(label_entry));
        const char *ip   = gtk_entry_get_text(GTK_ENTRY(ip_entry));
        const char *comm = gtk_entry_get_text(GTK_ENTRY(community_entry));
        const char *oid  = gtk_entry_get_text(GTK_ENTRY(oid_entry));

        if (lbl && strlen(lbl) > 0 && ip && strlen(ip) > 0 && oid && strlen(oid) > 0) {
            pthread_mutex_lock(&dlg->app->access.db_mutex);
            if (dlg->app->database.global_db_conn) {
                char query[1024];
                snprintf(query, sizeof(query),
                         "INSERT INTO snmp_targets (label, ip_address, community, oid_str, is_active) "
                         "VALUES ('%s', '%s', '%s', '%s', 1);",
                         lbl, ip, comm, oid);
                if (mysql_query(dlg->app->database.global_db_conn, query) != 0) {
                    g_printerr("[ERROR]: Failed to insert SNMP target: %s\n", mysql_error(dlg->app->database.global_db_conn));
                }
            }
            pthread_mutex_unlock(&dlg->app->access.db_mutex);

            refresh_snmp_target_list(dlg);
        }
    }
    gtk_widget_destroy(dialog);
}

// Show Edit Selected SNMP Target Dialog
void show_edit_snmp_dialog(SnmpManagerDialog *dlg) {
    GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(dlg->tree_view));
    GtkTreeModel *model;
    GtkTreeIter iter;

    if (!gtk_tree_selection_get_selected(selection, &model, &iter)) return;

    int id = 0;
    gchar *lbl = NULL, *ip = NULL, *comm = NULL, *oid = NULL;

    gtk_tree_model_get(model, &iter,
                       COL_ID, &id,
                       COL_LABEL, &lbl,
                       COL_IP, &ip,
                       COL_COMMUNITY, &comm,
                       COL_OID, &oid,
                       -1);

    GtkWidget *dialog = gtk_dialog_new_with_buttons(
        "Edit SNMP Target",
        GTK_WINDOW(dlg->dialog),
        GTK_DIALOG_DESTROY_WITH_PARENT,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Save", GTK_RESPONSE_ACCEPT,
        NULL
    );

    GtkStyleContext *context = gtk_widget_get_style_context(dialog);
    gtk_style_context_add_class(context, "session-dialog");

    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_style_context_add_class(gtk_widget_get_style_context(content), "session-dialog");

    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
    gtk_container_set_border_width(GTK_CONTAINER(grid), 12);

    GtkWidget *label_entry = gtk_entry_new();
    GtkWidget *ip_entry = gtk_entry_new();
    GtkWidget *community_entry = gtk_entry_new();
    GtkWidget *oid_entry = gtk_entry_new();

    gtk_entry_set_text(GTK_ENTRY(label_entry), lbl ? lbl : "");
    gtk_entry_set_text(GTK_ENTRY(ip_entry), ip ? ip : "");
    gtk_entry_set_text(GTK_ENTRY(community_entry), comm ? comm : "public");
    gtk_entry_set_text(GTK_ENTRY(oid_entry), oid ? oid : "");

    GtkWidget *oid_combo = gtk_combo_box_text_new();
    int match_idx = -1;
    for (size_t i = 0; i < OID_REGISTRY_COUNT; i++) {
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(oid_combo), OID_REGISTRY[i].label);
        if (oid && strcmp(oid, OID_REGISTRY[i].oid) == 0) match_idx = (int)i;
    }
    gtk_combo_box_set_active(GTK_COMBO_BOX(oid_combo), match_idx >= 0 ? match_idx : (int)(OID_REGISTRY_COUNT - 1));

    g_signal_connect(oid_combo, "changed", G_CALLBACK(on_oid_combo_changed), oid_entry);

    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Label:"), 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), label_entry, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("IP / Hostname:"), 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), ip_entry, 1, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Community String:"), 0, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), community_entry, 1, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("OID Preset:"), 0, 3, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), oid_combo, 1, 3, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("OID String:"), 0, 4, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), oid_entry, 1, 4, 1, 1);

    gtk_container_add(GTK_CONTAINER(content), grid);
    gtk_widget_show_all(dialog);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        const char *new_lbl  = gtk_entry_get_text(GTK_ENTRY(label_entry));
        const char *new_ip   = gtk_entry_get_text(GTK_ENTRY(ip_entry));
        const char *new_comm = gtk_entry_get_text(GTK_ENTRY(community_entry));
        const char *new_oid  = gtk_entry_get_text(GTK_ENTRY(oid_entry));

        if (new_lbl && strlen(new_lbl) > 0 && new_ip && strlen(new_ip) > 0 && new_oid && strlen(new_oid) > 0) {
            pthread_mutex_lock(&dlg->app->access.db_mutex);
            if (dlg->app->database.global_db_conn) {
                char query[1024];
                snprintf(query, sizeof(query),
                         "UPDATE snmp_targets SET label='%s', ip_address='%s', community='%s', oid_str='%s' WHERE id=%d;",
                         new_lbl, new_ip, new_comm, new_oid, id);
                if (mysql_query(dlg->app->database.global_db_conn, query) != 0) {
                    g_printerr("[ERROR]: Failed to update SNMP target: %s\n", mysql_error(dlg->app->database.global_db_conn));
                }
            }
            pthread_mutex_unlock(&dlg->app->access.db_mutex);

            refresh_snmp_target_list(dlg);
        }
    }

    g_free(lbl); g_free(ip); g_free(comm); g_free(oid);
    gtk_widget_destroy(dialog);
}

// Handler for adding target
void on_snmp_add_clicked(GtkWidget *button, gpointer user_data) {
    SnmpManagerDialog *dlg = (SnmpManagerDialog *)user_data;
    show_add_snmp_dialog(dlg);
}

// Handler for editing target
void on_snmp_edit_clicked(GtkWidget *button, gpointer user_data) {
    SnmpManagerDialog *dlg = (SnmpManagerDialog *)user_data;
    show_edit_snmp_dialog(dlg);
}

// Handler for refreshing the view
void on_snmp_refresh_clicked(GtkWidget *button, gpointer user_data) {
    SnmpManagerDialog *dlg = (SnmpManagerDialog *)user_data;
    refresh_snmp_target_list(dlg);
}

// Handler for toggling target active state
void on_snmp_toggle_active_clicked(GtkWidget *button, gpointer user_data) {
    SnmpManagerDialog *dlg = (SnmpManagerDialog *)user_data;
    GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(dlg->tree_view));
    GtkTreeModel *model;
    GtkTreeIter iter;

    if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
        int id;
        gtk_tree_model_get(model, &iter, COL_ID, &id, -1);

        pthread_mutex_lock(&dlg->app->access.db_mutex);
        if (dlg->app->database.global_db_conn) {
            char *query = g_strdup_printf("UPDATE snmp_targets SET is_active = NOT is_active WHERE id = %d", id);
            if (mysql_query(dlg->app->database.global_db_conn, query) != 0) {
                g_printerr("[ERROR]: Failed to toggle SNMP target: %s\n", mysql_error(dlg->app->database.global_db_conn));
            }
            g_free(query);
        }
        pthread_mutex_unlock(&dlg->app->access.db_mutex);

        refresh_snmp_target_list(dlg);
    }
}

// Handler for deleting target
void on_snmp_delete_clicked(GtkWidget *button, gpointer user_data) {
    SnmpManagerDialog *dlg = (SnmpManagerDialog *)user_data;
    GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(dlg->tree_view));
    GtkTreeModel *model;
    GtkTreeIter iter;

    if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
        int id;
        gtk_tree_model_get(model, &iter, COL_ID, &id, -1);

        pthread_mutex_lock(&dlg->app->access.db_mutex);
        if (dlg->app->database.global_db_conn) {
            char *query = g_strdup_printf("DELETE FROM snmp_targets WHERE id = %d", id);
            if (mysql_query(dlg->app->database.global_db_conn, query) != 0) {
                g_printerr("[ERROR]: Failed to delete SNMP target: %s\n", mysql_error(dlg->app->database.global_db_conn));
            }
            g_free(query);
        }
        pthread_mutex_unlock(&dlg->app->access.db_mutex);

        refresh_snmp_target_list(dlg);
    }
}

// Window destroy callback
void on_snmp_dialog_destroy(GtkWidget *widget, gpointer user_data) {
    SnmpManagerDialog *dlg = (SnmpManagerDialog *)user_data;
    g_free(dlg);
    if (global_app) {
        global_app->manager.snmp = NULL;
    }
}

// Open SNMP Manager Window (Styled identically to History Manager)
void open_snmp_manager_window(AppContext *app) {
    if (!app) return;

    // Bring existing window to focus if already open
    if (app->manager.snmp != NULL) {
        gtk_window_present(GTK_WINDOW(app->manager.snmp));
        return;
    }

    SnmpManagerDialog *dlg = g_malloc0(sizeof(SnmpManagerDialog));
    dlg->app = app;

    dlg->dialog = gtk_dialog_new_with_buttons(
        "aiterm - SNMP Target Manager",
        GTK_WINDOW(app->gui.window),
        GTK_DIALOG_DESTROY_WITH_PARENT,
        NULL,
        NULL
    );

    app->manager.snmp = dlg->dialog;

    // Window dimensions and hints (Matching history_manager_gui)
    gtk_window_set_default_size(GTK_WINDOW(dlg->dialog), 840, 480);
    gtk_window_set_skip_taskbar_hint(GTK_WINDOW(dlg->dialog), FALSE);
    gtk_window_set_type_hint(GTK_WINDOW(dlg->dialog), GDK_WINDOW_TYPE_HINT_NORMAL);

    // Apply 'session-dialog' theme styling context
    GtkStyleContext *context = gtk_widget_get_style_context(dlg->dialog);
    gtk_style_context_add_class(context, "session-dialog");

    GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(dlg->dialog));
    gtk_style_context_add_class(gtk_widget_get_style_context(content_area), "session-dialog");

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 12);
    gtk_style_context_add_class(gtk_widget_get_style_context(vbox), "session-dialog");
    gtk_box_pack_start(GTK_BOX(content_area), vbox, TRUE, TRUE, 0);

    // Setup ListStore
    dlg->list_store = gtk_list_store_new(NUM_COLUMNS,
                                         G_TYPE_INT,    // COL_ID
                                         G_TYPE_STRING, // COL_LABEL
                                         G_TYPE_STRING, // COL_IP
                                         G_TYPE_STRING, // COL_COMMUNITY
                                         G_TYPE_STRING, // COL_OID
                                         G_TYPE_STRING, // COL_VALUE
                                         G_TYPE_STRING  // COL_ACTIVE
                                        );

    dlg->tree_view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(dlg->list_store));
    g_object_unref(dlg->list_store);
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(dlg->tree_view), TRUE);

    GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
    GtkTreeViewColumn *column;

    // ID Column
    column = gtk_tree_view_column_new_with_attributes("ID", renderer, "text", COL_ID, NULL);
    gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_FIXED);
    gtk_tree_view_column_set_fixed_width(column, 40);
    gtk_tree_view_append_column(GTK_TREE_VIEW(dlg->tree_view), column);

    // Label Column
    column = gtk_tree_view_column_new_with_attributes("Label", renderer, "text", COL_LABEL, NULL);
    gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_FIXED);
    gtk_tree_view_column_set_fixed_width(column, 160);
    gtk_tree_view_append_column(GTK_TREE_VIEW(dlg->tree_view), column);

    // IP Address Column
    column = gtk_tree_view_column_new_with_attributes("IP Address", renderer, "text", COL_IP, NULL);
    gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_FIXED);
    gtk_tree_view_column_set_fixed_width(column, 110);
    gtk_tree_view_append_column(GTK_TREE_VIEW(dlg->tree_view), column);

    // Community Column
    column = gtk_tree_view_column_new_with_attributes("Community", renderer, "text", COL_COMMUNITY, NULL);
    gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_FIXED);
    gtk_tree_view_column_set_fixed_width(column, 90);
    gtk_tree_view_append_column(GTK_TREE_VIEW(dlg->tree_view), column);

    // OID String Column
    column = gtk_tree_view_column_new_with_attributes("OID String", renderer, "text", COL_OID, NULL);
    gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_FIXED);
    gtk_tree_view_column_set_fixed_width(column, 180);
    g_object_set(renderer, "ellipsize", PANGO_ELLIPSIZE_END, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(dlg->tree_view), column);

    // Last Value Column
    column = gtk_tree_view_column_new_with_attributes("Last Value", renderer, "text", COL_VALUE, NULL);
    gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_FIXED);
    gtk_tree_view_column_set_fixed_width(column, 120);
    gtk_tree_view_append_column(GTK_TREE_VIEW(dlg->tree_view), column);

    // Active Status Column
    column = gtk_tree_view_column_new_with_attributes("Active", renderer, "text", COL_ACTIVE, NULL);
    gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_FIXED);
    gtk_tree_view_column_set_fixed_width(column, 60);
    gtk_tree_view_append_column(GTK_TREE_VIEW(dlg->tree_view), column);

    // Scrolled Window Container
    GtkWidget *scrolled_window = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_window), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(scrolled_window), dlg->tree_view);
    gtk_box_pack_start(GTK_BOX(vbox), scrolled_window, TRUE, TRUE, 0);

    // Button Box Container (Horizontal layout with END alignment matching history_manager_gui)
    GtkWidget *button_box = gtk_button_box_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_button_box_set_layout(GTK_BUTTON_BOX(button_box), GTK_BUTTONBOX_END);
    gtk_box_set_spacing(GTK_BOX(button_box), 6);
    gtk_box_pack_start(GTK_BOX(vbox), button_box, FALSE, FALSE, 0);

    GtkWidget *btn_add     = gtk_button_new_with_label("Add Target");
    GtkWidget *btn_edit    = gtk_button_new_with_label("Edit");
    GtkWidget *btn_toggle  = gtk_button_new_with_label("Toggle Active");
    GtkWidget *btn_delete  = gtk_button_new_with_label("Delete Target");
    GtkWidget *btn_refresh = gtk_button_new_with_label("Refresh");
    GtkWidget *btn_close   = gtk_button_new_with_label("Close");

    gtk_container_add(GTK_CONTAINER(button_box), btn_add);
    gtk_container_add(GTK_CONTAINER(button_box), btn_edit);
    gtk_container_add(GTK_CONTAINER(button_box), btn_toggle);
    gtk_container_add(GTK_CONTAINER(button_box), btn_delete);
    gtk_container_add(GTK_CONTAINER(button_box), btn_refresh);
    gtk_container_add(GTK_CONTAINER(button_box), btn_close);

    // Signal connections
    g_signal_connect(btn_add, "clicked", G_CALLBACK(on_snmp_add_clicked), dlg);
    g_signal_connect(btn_edit, "clicked", G_CALLBACK(on_snmp_edit_clicked), dlg);
    g_signal_connect(btn_toggle, "clicked", G_CALLBACK(on_snmp_toggle_active_clicked), dlg);
    g_signal_connect(btn_delete, "clicked", G_CALLBACK(on_snmp_delete_clicked), dlg);
    g_signal_connect(btn_refresh, "clicked", G_CALLBACK(on_snmp_refresh_clicked), dlg);
    g_signal_connect_swapped(btn_close, "clicked", G_CALLBACK(gtk_widget_destroy), dlg->dialog);
    g_signal_connect(dlg->dialog, "destroy", G_CALLBACK(on_snmp_dialog_destroy), dlg);

    refresh_snmp_target_list(dlg);
    gtk_widget_show_all(dlg->dialog);

    write_to_ai_pane(app, "System: ", "Opened SNMP Target Manager window.", "ai_tag", "cmd_tag");
}

// Close function wrapper
void close_snmp_manager(AppContext *app) {
    if (!app) return;
    if (app->manager.snmp != NULL) {
        gtk_widget_destroy(GTK_WIDGET(app->manager.snmp));
        app->manager.snmp = NULL;
        write_to_ai_pane(app, "System: ", "Closed SNMP Target Manager window.", "ai_tag", "cmd_tag");
    }
}

