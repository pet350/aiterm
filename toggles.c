// part of aiterm project
// toggles.c
// Function for handling Toggling Booleans
// By: Peter Talbott
// Assisted by: Gemini
// May 2026

#include <vte/vte.h>
#include <mariadb/mysql.h>
#include <gtk/gtk.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "menu.h"
#include "commands.h"
#include "toggles.h"
#include "gui.h"
#include "update.h"
#include "help.h"
#include "utils.h"
#include "terminal.h"
#include "tee_handler.h"
#include "config.h"


gboolean toggle_function(AppContext *app, ToggleType toggle_type, GtkCheckMenuItem *menu_item, const char *args) {
    if (!app) return FALSE;

    const char *state_str = NULL;

    // 1. Determine target state string ("on" or "off")
    if (args && (strcmp(args, "on") == 0 || strcmp(args, "off") == 0)) {
        state_str = args;
    } else if (menu_item) {
        gboolean is_active = gtk_check_menu_item_get_active(menu_item);
        state_str = is_active ? "on" : "off";
    } else {
        g_warning("toggle_function: Neither valid args nor a menu_item was provided.");
        return FALSE;
    }

    // 2. Build the precise string matched by the CommandRegistry array
    char command_buffer[256];
    switch (toggle_type) {
        case TOGGLE_AUTO_ALL:
            snprintf(command_buffer, sizeof(command_buffer), "auto all %s", state_str);
            break;
        case TOGGLE_AUTOREPLY:
            snprintf(command_buffer, sizeof(command_buffer), "autoreply %s", state_str);
            break;
        case TOGGLE_AUTOEXE:
            snprintf(command_buffer, sizeof(command_buffer), "autoexe %s", state_str);
            break;
        case TOGGLE_NOISE_FILTER:
            snprintf(command_buffer, sizeof(command_buffer), "noise filter %s", state_str);
            break;
        case TOGGLE_SESSION_READ_GLOBAL:
            snprintf(command_buffer, sizeof(command_buffer), "session read from global %s", state_str);
            break;
        case TOGGLE_SESSION_WRITE_GLOBAL:
            snprintf(command_buffer, sizeof(command_buffer), "session write to global %s", state_str);
            break;
        case TOGGLE_SMART_CACHE:
            snprintf(command_buffer, sizeof(command_buffer), "smart cache %s", state_str);
            break;
        case TOGGLE_RATELIMIT:
            snprintf(command_buffer, sizeof(command_buffer), "ratelimit %s", state_str);
            break;
        case TOGGLE_TEE:
            snprintf(command_buffer, sizeof(command_buffer), "tee %s", state_str);
            break;
        default:
            g_warning("toggle_function: Unknown toggle type %d", toggle_type);
            return FALSE;
    }

    // 3. Dispatch the string down the unified pipeline
    execute_command(app, command_buffer);

    return TRUE;
}

void on_menu_item_toggled(GtkCheckMenuItem *menu_item, gpointer user_data)
{
    AppContext *app = g_object_get_data(G_OBJECT(menu_item), "app-context");
    ToggleType toggle_type = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(menu_item), "toggle-type"));

    if (app) {
        toggle_function(app, toggle_type, menu_item, NULL);
        sync_toggle_ui_elements(app);
    }
}

// Unified callback shared by ALL check menu items
void on_menu_toggle_item_toggled(GtkCheckMenuItem *menu_item, gpointer user_data)
{
    AppContext *app = g_object_get_data(G_OBJECT(menu_item), "app-context");
    ToggleType toggle_type = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(menu_item), "toggle-type"));

    if (app) {
        toggle_function(app, toggle_type, menu_item, NULL);
        sync_toggle_ui_elements(app);
    }
}

// Helper function to initialize, style, and attach metadata to a menu toggle item
void setup_menu_toggle(GtkWidget *menu_item, AppContext *app, ToggleType type, gboolean initial_state)
{
    // Apply state without triggering signals prematurely during creation
    g_signal_handlers_block_by_func(menu_item, G_CALLBACK(on_menu_toggle_item_toggled), NULL);
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(menu_item), initial_state);
    g_signal_handlers_unblock_by_func(menu_item, G_CALLBACK(on_menu_toggle_item_toggled), NULL);

    // Attach contextual state objects directly into the GTK widget structure
    g_object_set_data(G_OBJECT(menu_item), "app-context", app);
    g_object_set_data(G_OBJECT(menu_item), "toggle-type", GINT_TO_POINTER(type));

    switch (type) {
        case TOGGLE_RATELIMIT:
            app->ui.toggle_ratelimit = menu_item;
            break;
        case TOGGLE_NOISE_FILTER:
            app->ui.toggle_noise_filter = menu_item;
            break;
        case TOGGLE_SMART_CACHE:
            app->ui.toggle_smart_cache = menu_item;
            break;
        case TOGGLE_AUTOREPLY:
            app->ui.toggle_autoreply = menu_item;
            break;
        case TOGGLE_TEE:
            app->ui.toggle_tee = menu_item;
            break;
        case TOGGLE_AUTOEXE:
            app->ui.toggle_autoexe = menu_item;
            break;
        case TOGGLE_AUTO_ALL:
            app->ui.toggle_auto_all = menu_item;
            break;
        case TOGGLE_SESSION_READ_GLOBAL:
            app->ui.toggle_session_read_global = menu_item;
            break;
        case TOGGLE_SESSION_WRITE_GLOBAL:
            app->ui.toggle_session_write_global = menu_item;
            break;
        default:
            // Some toggles might not have explicit UI struct members, which is fine
            break;
    }

    // Connect the uniform signal handler
    g_signal_connect(menu_item, "toggled", G_CALLBACK(on_menu_toggle_item_toggled), NULL);
}
