// part of the aiterm project
// config.h
// Header file for config file access
// By: Peter Talbott
// Assisted by: Gemini and OpenAI
// aiterm The terminal emulator with an AI Pane
// April 2026, MMay 2026, June 2026

#ifndef CONFIG_H
#define CONFIG_H
#include <pthread.h>
#include <vte/vte.h>
#include <json-c/json.h>
#include "gui.h"

void save_config(AppContext *app);
void load_config(AppContext *app);

#endif