// part of the aiterm project
// host_analysis.h
// Header file for AI Host analysis
// By: Peter Talbott
// Assisted by: OpenAI and MS Copilot
// aiterm The terminal emulator with an AI Pane
// October 2026

#ifndef HOST_ANALYSIS_H
#define HOST_ANALYSIS_H

#include "appcontext.h"

/* Caller MUST hold app->host_analysis.mutex while using returned pointer. */
HostDetails *host_analysis_find_or_create_locked(HostAnalysisContext *ctx, const char *hostname);
int  host_analysis_init(AppContext *app);
void host_analysis_shutdown(AppContext *app);
void host_analysis_start(AppContext *app);
void host_analysis_stop(AppContext *app);
void host_analysis_analyze_all(AppContext *app); /* compatibility */
void host_analysis_refresh_all(AppContext *app);
void host_analysis_import_snmp(AppContext *app);
/* Local systemd collection is part of host_analysis_refresh_all(). */

/* Thread-safe copy of a host record; caller owns the output. */
gboolean host_analysis_get_host(AppContext *app, guint index, HostDetails *out);

/* Formats a snapshot for future AI-context integration. Caller frees with g_free(). */
gchar *host_analysis_format_host(const HostDetails *host);
gchar *host_analysis_format_all(AppContext *app);
gchar *host_analysis_format_status(AppContext *app);

#endif

