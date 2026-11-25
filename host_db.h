// part of the aiterm project
// host_db.h Header file
// aiterm The terminal emulator with an AI Pane
// AITerm Zeta-1: single-owner Host Analysis MariaDB worker.
// No SQL is issued from the GTK thread or while holding the host mutex.
// All queue jobs own their data.
// Enables host analysis rom multiple sources
// host_analysis.c - additive host telemetry collection
// Existing SNMP polling and terminal/TEE workflows remain independent.
// By: Peter Talbott
// Assisted by: OpenAI and MS Copilot
// October 2026

#ifndef HOST_DB_H
#define HOST_DB_H

#include "appcontext.h"

typedef enum {
    DB_RESTORE,
    DB_SCHEDULE,
    DB_TIMING,
    DB_SAMPLE,
    DB_COLLECTION,
    DB_HISTORY_QUERY,
    DB_CLEANUP,
    DB_OBSERVATION,
    DB_QUIT
} HostJobType;

typedef struct {
    HostJobType type;
    HostDetails host;
    gboolean enabled;
    gboolean success;
    guint duration_ms;
    char error_message[256];
    char collector[24];
    char metric[192];
    gchar *payload;
    time_t sampled_at;
    guint hours;
    guint limit;
    void (*report_callback)(AppContext *, const char *, gpointer);
    gpointer callback_data;
} HostDBJob;

typedef struct {
    AppContext *app;
    GPtrArray *hosts;
    GPtrArray *timings;
    GPtrArray *snapshots; /* saved SSH samples, owned by restore result */
} RestoreResult;

// Function prototypes
gboolean host_db_init(AppContext *app);
void host_db_shutdown(AppContext *app);
void host_db_save_local(AppContext *app, const HostDetails *host);
void host_db_save_snmp(AppContext *app, const HostSnmpObservation *observation);
void host_db_save_schedule(AppContext *app, const char *hostname, gboolean enabled);
void host_db_save_timing(AppContext *app, const HostDetails *host);
void host_db_save_sample(AppContext *app, const HostDetails *host);
/* Zeta-2: one atomic historical event per completed SSH Linux collection. */
void host_db_save_collection_result(AppContext *app, const HostDetails *host,
                                    gboolean success, guint duration_ms,
                                    const char *error_message);

/* Query results are returned on the GTK main context. */
typedef void (*HostDBReportCallback)(AppContext *, const char *, gpointer);
void host_db_query_history(AppContext *app, const char *hostname, guint hours,
                           guint limit, HostDBReportCallback callback, gpointer data);
void host_db_queue_cleanup(AppContext *app);
gchar *host_db_format_status(AppContext *app);
#endif
