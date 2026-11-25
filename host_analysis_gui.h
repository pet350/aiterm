// part of the aiterm project
// host_analysis_gui.c - additive host telemetry collection
// aiterm The terminal emulator with an AI Pane
// AITerm Delta-2: read-only Host Analysis Manager.
// Shares the existing .session-dialog CSS class with other managers.
// GTK access is confined to the main thread; collector data is snapshotted.
// By: Peter Talbott
// Assisted by: OpenAI
// October 2026

#ifndef HOST_ANALYSIS_GUI_H
#define HOST_ANALYSIS_GUI_H

#include "appcontext.h"

typedef struct {
    AppContext *app;
    GtkWidget *window;
    GtkListStore *store;
    GtkWidget *details;
    GtkWidget *summary;
} HostManager;

// Function Prototypes
void host_analysis_manager_show(AppContext *app);

#endif
