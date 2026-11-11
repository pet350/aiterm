#ifndef AITERM_HOST_LINUX_REMOTE_H
#define AITERM_HOST_LINUX_REMOTE_H
#include "appcontext.h"
gboolean host_linux_remote_collect(AppContext *app, const char *hostname, GError **error);
#endif
