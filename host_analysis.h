// part of the aiterm project
// host_analysis.h
// Header file for AI Host analysis
// By: Peter Talbott
// Assisted by: MS Copilot
// aiterm The terminal emulator with an AI Pane
// October 2026

#ifndef HOST_ANALYSIS_H
#define HOST_ANALYSIS_H

#include "appcontext.h"

int  host_analysis_init(AppContext *app);
void host_analysis_shutdown(AppContext *app);
void host_analysis_start(AppContext *app);
void host_analysis_stop(AppContext *app);
void host_analysis_analyze_all(AppContext *app);

#endif

