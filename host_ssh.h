#ifndef AITERM_HOST_SSH_H
#define AITERM_HOST_SSH_H

#include "appcontext.h"
#include <gio/gio.h>

/* Delta-3 transport only: does not mutate host records or poll SNMP.
 * All callbacks run in the caller's GLib main context. */
typedef struct {
    gboolean success;
    gboolean timed_out;
    gint exit_status;
    gchar *stdout_text;
    gchar *stderr_text;
    gchar *error_message;
} HostSSHResult;

typedef void (*HostSSHCompletion)(AppContext *app, const HostSSHResult *result,
                                  gpointer user_data);

/* Returns FALSE for invalid configuration or process launch failures.
 * A TRUE return guarantees a completion callback unless the main loop exits. */
gboolean host_ssh_execute_async(AppContext *app, const HostSSHProfile *profile,
                                const gchar *remote_command, guint timeout_seconds,
                                HostSSHCompletion completion, gpointer user_data,
                                GError **error);
#endif
