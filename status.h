// part of the aiterm project
// utils.h
// Header file for status display in  aiterm
// By: Peter Talbott
// Assisted by: Gemini and OpenAI
// aiterm The terminal emulator with an AI Pane
// April 2026, MMay 2026, June 2026

#ifndef STATUS_H
#define STATUS_H

#include <pthread.h>
#include <vte/vte.h>
#include <json-c/json.h>
#include "gui.h"

void display_status(AppContext *app);

#endif