#ifndef TERMINAL_H
#define TERMINAL_H

#include <vte/vte.h>

// Forward declaration: tells the compiler AppContext exists 
// without needing the full definition from gui.h yet.
typedef struct AppContext AppContext; 

GtkWidget* setup_terminal(AppContext *app);
void apply_terminal_transparency(AppContext *app);
void apply_visual_settings(AppContext *app);

#endif


