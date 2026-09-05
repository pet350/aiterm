// part of the aiterm project
// snmp_manager_gui.h
// Header file for snmp manager gui
// By: Peter Talbott
// Assisted by: Gemini
// aiterm The terminal emulator with an AI Pane
// August 2026

#ifndef SNMP_MANAGER_GUI_H
#define SNMP_MANAGER_GUI_H

#include <gtk/gtk.h>
#include "gui.h"

// TreeView Column Definitions for SNMP Manager
enum {
    COL_SNMP_ID = 0,
    COL_SNMP_LABEL,
    COL_SNMP_IP,
    COL_SNMP_COMMUNITY,
    COL_SNMP_OID,
    COL_SNMP_VALUE,
    COL_SNMP_ACTIVE,
    NUM_SNMP_COLUMNS
};

// Internal Dialog Structure with named struct tag
typedef struct SnmpManagerDialog {
    AppContext *app;
    GtkWidget *dialog;
    GtkWidget *tree_view;
    GtkListStore *list_store;
} SnmpManagerDialog;

// Function prototypes
gchar *on_snmp_delay_format_value(GtkScale *scale, gdouble value, gpointer user_data);

void on_snmp_delay_scale_changed(GtkRange *range, gpointer user_data);
void refresh_snmp_target_list(SnmpManagerDialog *dlg);
void show_add_snmp_dialog(SnmpManagerDialog *dlg);
void show_edit_snmp_dialog(SnmpManagerDialog *dlg);
void open_snmp_manager_window(AppContext *app);
void close_snmp_manager(AppContext *app);

#endif // SNMP_MANAGER_GUI_H

