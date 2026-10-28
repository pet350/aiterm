// part of the aiterm project
// host_analysis.h
// Header file for AI Host analysis
// By: Peter Talbott
// Assisted by: MS Copilot
// aiterm The terminal emulator with an AI Pane
// October 2026

#include "host_analysis.h"

int host_analysis_init(
    AppContext *app)
{
    app->host_analysis.count = 0;

    pthread_mutex_init(
        &app->host_analysis.mutex,
        NULL);

    app->host_analysis.running = FALSE;

    app->host_analysis.refresh_interval = 300;

    app->host_analysis.analysis_interval = 600;

    return 0;
}