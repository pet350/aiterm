// part of the aiterm project
// host_linux_remote.c
// Delta-4: manually triggered remote Linux telemetry via Delta-3 OpenSSH transport.
// aiterm The terminal emulator with an AI Pane
// AITerm Delta-2: read-only Host Analysis Manager.
// Shares the existing .session-dialog CSS class with other managers.
// GTK access is confined to the main thread; collector data is snapshotted.
// By: Peter Talbott
// Assisted by: OpenAI
// October - November 2026

#ifndef AITERM_HOST_LINUX_REMOTE_H
#define AITERM_HOST_LINUX_REMOTE_H

#include "appcontext.h"

// Function prototypes
gboolean host_linux_remote_collect(AppContext *app, const char *hostname, GError **error);
gboolean host_linux_remote_collect_scheduled(AppContext *app, const char *hostname, GError **error);

#endif
