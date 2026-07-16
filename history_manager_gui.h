#ifndef HISTORY_MANAGER_GUI_H
#define HISTORY_MANAGER_GUI_H

#include <gtk/gtk.h>
#include "gui.h" // Ensures access to your global App/Aiterm structures

// TreeView Column Enums
enum {
    COL_ID = 0,
    COL_ROLE,
    COL_CONTENT,
    COL_SESSION_UUID,
    NUM_COLUMNS
};

// Internal structure to hold UI references and state
typedef struct {
    AppContext *app;
    GtkWidget *dialog;
    GtkWidget *tree_view;
    GtkListStore *list_store;
    GtkWidget *chk_current_session;
} HistoryManagerDialog;

// Function prototypes
void refresh_history_list(HistoryManagerDialog *dlg);
void on_delete_selected_clicked(GtkWidget *button, gpointer user_data);
void on_clear_all_clicked(GtkWidget *button, gpointer user_data);
void on_dialog_destroy(GtkWidget *widget, gpointer user_data);
void open_history_manager_window(AppContext *app);
void close_history_manager(AppContext *app);

#endif // HISTORY_MANAGER_GUI_H
