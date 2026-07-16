// part of aiterm project
// policy_manager_gui.h
// Header File used to manage policies in a gui window
// By: Peter Talbott
// Assisted by: Gemini
// May - July  2026

#ifndef POLICY_MANAGER_GUI_H
#define POLICY_MANAGER_GUI_H

#include <gtk/gtk.h>
#include "gui.h"
enum {
    COL_POLICY_COMMAND = 0,
    COL_POLICY_TYPE,
    COL_POLICY_RISK,
    NUM_POLICY_COLUMNS
};

typedef struct {
    AppContext *app;
    GtkWidget *dialog;
    GtkWidget *tree_view;
    GtkListStore *list_store;

    GtkWidget *lbl_command;     // Displays currently selected command
    GtkWidget *combo_type;      // Dropdown selector: ALLOW, BLOCK, APPROVE
    GtkWidget *spin_risk;       // Numeric step adjustments for Risk Level

    char *selected_command;     // Stores active row string selection
} PolicyDialog;

// Function Prototypes
void refresh_policy_list(PolicyDialog *dlg);
void on_row_selected(GtkTreeSelection *selection, gpointer user_data);
void on_save_policy_clicked(GtkWidget *button, gpointer user_data);
void on_delete_policy_clicked(GtkWidget *button, gpointer user_data);
void on_dialog_policy_manager_gui_destroy(GtkWidget *widget, gpointer user_data);
void open_policy_manager_window(AppContext *app);
void close_policy_manager(AppContext *app);

#endif // POLICY_MANAGER_GUI_H

