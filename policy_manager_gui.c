// part of aiterm project
// policy_manager_gui.c
// Various utilities used for policy management
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

#include "policy_manager_gui.h"
#include "policy_dao.h"
#include "gui.h"
#include "utils.h"
#include "commands.h"


void refresh_policy_list(PolicyDialog *dlg) {
    if (!dlg || !dlg->app) return;

    gtk_list_store_clear(dlg->list_store);

    g_free(dlg->selected_command);
    dlg->selected_command = NULL;

    gtk_label_set_text(GTK_LABEL(dlg->lbl_command), "None Selected");
    gtk_combo_box_set_active(GTK_COMBO_BOX(dlg->combo_type), -1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(dlg->spin_risk), 0);

    // Call our shared structural data layout utility
    GList *policies = get_all_policies(dlg->app);
    if (!policies) return;

    GtkTreeIter iter;
    for (GList *l = policies; l != NULL; l = l->next) {
        PolicyRecord *p = (PolicyRecord *)l->data;

        gtk_list_store_append(dlg->list_store, &iter);
        gtk_list_store_set(dlg->list_store, &iter,
                           COL_POLICY_COMMAND, p->name,
                           COL_POLICY_TYPE, p->type,
                           COL_POLICY_RISK, p->risk,
                           -1);

        free_policy_record(p);
    }
    g_list_free(policies);
}

void on_row_selected(GtkTreeSelection *selection, gpointer user_data) {
    PolicyDialog *dlg = (PolicyDialog *)user_data;
    GtkTreeModel *model;
    GtkTreeIter iter;

    if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
        char *cmd = NULL;
        char *type = NULL;
        int risk = 0;

        gtk_tree_model_get(model, &iter,
                           COL_POLICY_COMMAND, &cmd,
                           COL_POLICY_TYPE, &type,
                           COL_POLICY_RISK, &risk, -1);

        if (cmd) {
            g_free(dlg->selected_command);
            dlg->selected_command = g_strdup(cmd);
            gtk_label_set_text(GTK_LABEL(dlg->lbl_command), cmd);
            g_free(cmd);
        }

        if (type) {
            if (strcmp(type, "ALLOW") == 0) gtk_combo_box_set_active(GTK_COMBO_BOX(dlg->combo_type), 0);
            else if (strcmp(type, "BLOCK") == 0) gtk_combo_box_set_active(GTK_COMBO_BOX(dlg->combo_type), 1);
            else if (strcmp(type, "APPROVE") == 0) gtk_combo_box_set_active(GTK_COMBO_BOX(dlg->combo_type), 2);
            g_free(type);
        }

        gtk_spin_button_set_value(GTK_SPIN_BUTTON(dlg->spin_risk), risk);
    }
}

void on_save_policy_clicked(GtkWidget *button, gpointer user_data) {
    PolicyDialog *dlg = (PolicyDialog *)user_data;
    if (!dlg->selected_command) return;

    char *selected_type = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(dlg->combo_type));
    if (!selected_type) return;

    int selected_risk = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(dlg->spin_risk));

    PolicyRecord p;
    p.name = dlg->selected_command;
    p.type = selected_type;
    p.risk = selected_risk;

    if (set_command_policy(dlg->app, &p)) {
        refresh_policy_list(dlg);
    } else {
        g_printerr("[ERROR]: Policy UI: Failed to write rule changes to backend storage.\n");
    }

    g_free(selected_type);
}

void on_delete_policy_clicked(GtkWidget *button, gpointer user_data) {
    PolicyDialog *dlg = (PolicyDialog *)user_data;
    if (!dlg->selected_command) return;

    if (delete_command_policy(dlg->app, dlg->selected_command)) {
        refresh_policy_list(dlg);
    }
}

void on_dialog_policy_manager_gui_destroy(GtkWidget *widget, gpointer user_data) {
    PolicyDialog *dlg = (PolicyDialog *)user_data;
    g_free(dlg->selected_command);
    g_free(dlg);
}
void open_policy_manager_window(AppContext *app) {
    PolicyDialog *dlg = g_new0(PolicyDialog, 1);
    dlg->app = app;
    dlg->selected_command = NULL;

    // FIX: Instantiate as a native Top-Level Window instead of a child Dialog
    dlg->dialog = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(dlg->dialog), "Command Security Policy Rules");
    gtk_window_set_default_size(GTK_WINDOW(dlg->dialog), 700, 460);

    // Style the window window framework
    GtkStyleContext *context = gtk_widget_get_style_context(dlg->dialog);
    gtk_style_context_add_class(context, "session-dialog");

    // Create the master vertical arrangement box
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 12);
    
    // FIX: Pack directly into the window container instead of the old dialog content area
    gtk_container_add(GTK_CONTAINER(dlg->dialog), vbox);

    // Setup Storage Model Backend
    dlg->list_store = gtk_list_store_new(NUM_POLICY_COLUMNS, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INT);
    dlg->tree_view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(dlg->list_store));
    g_object_unref(dlg->list_store);

    GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
    GtkTreeViewColumn *col;
    
    // 1. Command Name Column
    col = gtk_tree_view_column_new_with_attributes("Intercepted Command Binary", renderer, "text", COL_POLICY_COMMAND, NULL);
    gtk_tree_view_column_set_expand(col, TRUE);
    gtk_tree_view_column_set_resizable(col, TRUE);
    gtk_tree_view_append_column(GTK_TREE_VIEW(dlg->tree_view), col);

    // 2. Policy Action State Column
    col = gtk_tree_view_column_new_with_attributes("Action Behavior", renderer, "text", COL_POLICY_TYPE, NULL);
    gtk_tree_view_column_set_fixed_width(col, 120);
    gtk_tree_view_column_set_sizing(col, GTK_TREE_VIEW_COLUMN_FIXED);
    gtk_tree_view_append_column(GTK_TREE_VIEW(dlg->tree_view), col);

    // 3. Risk Level Evaluation Column
    col = gtk_tree_view_column_new_with_attributes("Risk Matrix Level", renderer, "text", COL_POLICY_RISK, NULL);
    gtk_tree_view_column_set_fixed_width(col, 130);
    gtk_tree_view_column_set_sizing(col, GTK_TREE_VIEW_COLUMN_FIXED);
    gtk_tree_view_append_column(GTK_TREE_VIEW(dlg->tree_view), col);

    GtkWidget *scrolled_window = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_window), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(scrolled_window), dlg->tree_view);
    gtk_box_pack_start(GTK_BOX(vbox), scrolled_window, TRUE, TRUE, 0);

    // Parameter Grid Form Editor Panel
    GtkWidget *editor_frame = gtk_frame_new("Edit Selected Command Directives");
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
    gtk_container_set_border_width(GTK_CONTAINER(grid), 8);
    gtk_container_add(GTK_CONTAINER(editor_frame), grid);
    gtk_box_pack_start(GTK_BOX(vbox), editor_frame, FALSE, FALSE, 4);

    // Row 1: Target Command Output Label
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Selected Binary Target:"), 0, 0, 1, 1);
    dlg->lbl_command = gtk_label_new("None Selected");
    gtk_widget_set_halign(dlg->lbl_command, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(grid), dlg->lbl_command, 1, 0, 1, 1);

    // Row 2: Combobox Dropdown configuration
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Firewall Policy Rule Action:"), 0, 1, 1, 1);
    dlg->combo_type = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(dlg->combo_type), "ALLOW");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(dlg->combo_type), "BLOCK");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(dlg->combo_type), "APPROVE");
    gtk_grid_attach(GTK_GRID(grid), dlg->combo_type, 1, 1, 1, 1);

    // Row 3: Risk Spin adjustment bounding
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Assigned Threat Vector Risk:"), 0, 2, 1, 1);
    GtkAdjustment *adj = gtk_adjustment_new(0, 0, 100, 1, 10, 0);
    dlg->spin_risk = gtk_spin_button_new(adj, 1, 0);
    gtk_grid_attach(GTK_GRID(grid), dlg->spin_risk, 1, 2, 1, 1);

    // Bottom Control Buttons Row Layout
    GtkWidget *button_box = gtk_button_box_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_button_box_set_layout(GTK_BUTTON_BOX(button_box), GTK_BUTTONBOX_END);
    gtk_box_set_spacing(GTK_BOX(button_box), 6);
    gtk_box_pack_start(GTK_BOX(vbox), button_box, FALSE, FALSE, 0);

    GtkWidget *btn_save   = gtk_button_new_with_label("Apply Directive Changes");
    GtkWidget *btn_delete = gtk_button_new_with_label("Delete Rule Context");
    GtkWidget *btn_close  = gtk_button_new_with_label("Close Window");

    gtk_container_add(GTK_CONTAINER(button_box), btn_save);
    gtk_container_add(GTK_CONTAINER(button_box), btn_delete);
    gtk_container_add(GTK_CONTAINER(button_box), btn_close);

    // Wiring Signal Attachments
    GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(dlg->tree_view));
    g_signal_connect(selection, "changed", G_CALLBACK(on_row_selected), dlg);
    g_signal_connect(btn_save, "clicked", G_CALLBACK(on_save_policy_clicked), dlg);
    g_signal_connect(btn_delete, "clicked", G_CALLBACK(on_delete_policy_clicked), dlg);
    g_signal_connect_swapped(btn_close, "clicked", G_CALLBACK(gtk_widget_destroy), dlg->dialog);
    g_signal_connect(dlg->dialog, "destroy", G_CALLBACK(on_dialog_policy_manager_gui_destroy), dlg);

    refresh_policy_list(dlg);
    gtk_widget_show_all(dlg->dialog);
}
