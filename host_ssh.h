// part of the aiterm project
// host_ssh.h - additive host telemetry collection
// AITerm 0.9.14-delta-3: asynchronous OpenSSH transport.
// No passwords, no interactive prompts, no host registry writes.
// aiterm The terminal emulator with an AI Pane
// AITerm Delta-2: read-only Host Analysis Manager.
// Shares the existing .session-dialog CSS class with other managers.
// GTK access is confined to the main thread; collector data is snapshotted.
// By: Peter Talbott
// Assisted by: OpenAI
// October 2026

#ifndef AITERM_HOST_SSH_H
#define AITERM_HOST_SSH_H

#include <gio/gio.h>

#include "appcontext.h"
#include "host_db.h"
#include "host_analysis.h"

/* Delta-3 transport only: does not mutate host records or poll SNMP.
 * All callbacks run in the caller's GLib main context. */
typedef struct {
    gboolean success;
    gboolean timed_out;
    guint elapsed_ms;
    gint exit_status;
    gchar *stdout_text;
    gchar *stderr_text;
    gchar *error_message;
} HostSSHResult;

typedef void (*HostSSHCompletion) (
    AppContext *app,
    const HostSSHResult *result,
    gpointer user_data
);

typedef struct {
    AppContext *app;
    GSubprocess *process;
    GCancellable *cancellable;
    HostSSHCompletion completion;
    gpointer user_data;
    guint timeout_id;
    gboolean timed_out;
    gint64 started_us;
    gchar *hostname;
    guint timeout_seconds;
} SSHJob;

/* Per-host adaptive timeout. A test always gets a generous calibration window.
 * Collection starts at 60s until a successful sample exists; then uses
 * recent maximum + 5s, bounded to [15, 120]. */
guint host_ssh_adaptive_timeout(AppContext *app, const char *hostname, gboolean test);

/* Returns FALSE for invalid configuration or process launch failures.
 * A TRUE return guarantees a completion callback unless the main loop exits. */
gboolean host_ssh_execute_async(AppContext *app, const HostSSHProfile *profile,
                                const gchar *remote_command, guint timeout_seconds,
                                HostSSHCompletion completion, gpointer user_data,
                                GError **error);

void record_ssh_timing(SSHJob *job, const HostSSHResult *result);
gboolean ssh_timeout(gpointer data);
void ssh_complete(GObject *source, GAsyncResult *async, gpointer data);

#endif
