// part of aiterm project
// noise_filter_manager_gui.h
// Header File used to manage noise filters in a gui window
// By: Peter Talbott
// Assisted by: Gemini
// May - July  2026

#ifndef NOISE_FILTER_MANAGER_GUI_H
#define NOISE_FILTER_MANAGER_GUI_H

#include <gtk/gtk.h>
#include "gui.h"

enum {
    COL_NOISE_ID = 0,
    COL_NOISE_PATTERN,
    COL_NOISE_UUID,
    NUM_NOISE_COLUMNS
};

typedef struct {
    AppContext *app;
    GtkWidget *dialog;
    GtkWidget *tree_view;
    GtkListStore *list_store;
    GtkWidget *entry_pattern;
} NoiseFilterDialog;

// Function prototypes
void refresh_noise_list(NoiseFilterDialog *dlg);
void on_add_pattern_clicked(GtkWidget *button, gpointer user_data);
void on_delete_noise_clicked(GtkWidget *button, gpointer user_data);
void on_dialog_noise_filter_Manager_destroy(GtkWidget *widget, gpointer user_data);
void open_noise_filter_manager_window(AppContext *app);
void close_noise_manager(AppContext *app);

#endif // NOISE_FILTER_MANAGER_GUI_H
