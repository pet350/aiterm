// part of the aiterm project
// noisefilter.h
// Header file for noisefilters used in aiterm
// By: Peter Talbott
// Assisted by: Gemini and OpenAI
// aiterm The terminal emulator with an AI Pane
// April 2026, MMay 2026, June 2026

#ifndef NOISEFILTER_H
#define NOISEFILTER_H
#include <pthread.h>
#include <vte/vte.h>
#include <json-c/json.h>
#include "gui.h"

void noise_filter_list(AppContext *app);
void noise_filter_add(AppContext *app, const char *filter_data);
gboolean ignore_tee_line(AppContext *app, const char *line);
void refresh_pattern_cache(AppContext *app);

#endif
