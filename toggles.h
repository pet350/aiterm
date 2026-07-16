// Part of project: aiterm
// toggles.h
// C Program header file for menu functions
// By: Peter Talbott
// With assistance from Gemini and OpenAI
// April, May 2026

#ifndef TOGGLES_H
#define TOGGLES_H

#include <stdio.h>
#include <gtk/gtk.h>
#include <string.h>
#include <stdlib.h>
#include "gui.h"
#include "menu.h"
#include "commands.h"

typedef enum {
    TOGGLE_AUTO_ALL,
    TOGGLE_AUTOREPLY,
    TOGGLE_AUTOEXE,
    TOGGLE_DEBUG,
    TOGGLE_NOISE_FILTER,
    TOGGLE_SESSION_READ_GLOBAL,
    TOGGLE_SESSION_WRITE_GLOBAL,
    TOGGLE_SMART_CACHE,
    TOGGLE_RATELIMIT,
    TOGGLE_TEE,
    TOGGLE_XML
} ToggleType;


gboolean toggle_function(AppContext *app, ToggleType toggle_type, GtkCheckMenuItem *menu_item, const char *args);

void on_menu_item_toggled(GtkCheckMenuItem *menu_item, gpointer user_data);
void on_menu_toggle_item_toggled(GtkCheckMenuItem *menu_item, gpointer user_data);
void setup_menu_toggle(GtkWidget *menu_item, AppContext *app, ToggleType type, gboolean initial_state);

#endif