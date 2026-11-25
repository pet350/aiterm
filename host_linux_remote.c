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

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "host_db.h"
#include "host_linux_remote.h"
#include "host_ssh.h"
#include "host_analysis.h"
#include "gui.h"
#include "utils.h"
#include "update.h"

typedef struct { gchar *hostname; gboolean quiet; } RemoteJob;

/* Epsilon-1a: printf %.0f avoids awk scientific notation for byte counts.
 * Output is deliberately machine-readable and the command contains no user input.
 * Requires a POSIX shell, awk, df, uname and Linux /proc files. */
static const char *remote_command =
    "LC_ALL=C; export LC_ALL; "
    "printf 'KERNEL='; uname -sr; "
    "awk '{print \"UPTIME=\" int($1)}' /proc/uptime; "
    "awk '{print \"LOAD=\" $1}' /proc/loadavg; "
    "awk '/^MemTotal:/ {printf \"MEM_TOTAL=%.0f\\n\", $2 * 1024} "
    "/^MemAvailable:/ {printf \"MEM_AVAILABLE=%.0f\\n\", $2 * 1024} "
    "/^SwapTotal:/ {printf \"SWAP_TOTAL=%.0f\\n\", $2 * 1024} "
    "/^SwapFree:/ {printf \"SWAP_FREE=%.0f\\n\", $2 * 1024}' /proc/meminfo; "
    "df -B1 -P / | awk 'NR==2 {print \"DISK_TOTAL=\" $2; print \"DISK_USED=\" $3}'";

static gboolean parse_long(const char *s, long long *out)
{
    char *end = NULL;
    errno = 0;
    long long v = g_ascii_strtoll(s, &end, 10);
    if (errno || !end || *end || end == s || v < 0) return FALSE;
    *out = v;
    return TRUE;
}
static gboolean parse_sample(const char *output, HostDetails *h)
{
    guint fields = 0;
    gchar **lines = g_strsplit(output ? output : "", "\n", -1);
    for (guint i = 0; lines[i]; i++) {
        gchar *sep = strchr(lines[i], '=');
        if (!sep) continue;
        *sep++ = '\0';
        if (!strcmp(lines[i], "KERNEL")) {
            g_strlcpy(h->remote_linux_kernel, sep, sizeof(h->remote_linux_kernel));
            if (*sep) fields |= 1;
        } else if (!strcmp(lines[i], "LOAD")) {
            gchar *end = NULL;
            double v = g_ascii_strtod(sep, &end);
            if (end != sep && end && !*end && v >= 0) { h->remote_linux_load = v; fields |= 2; }
        } else {
            long long v;
            if (!parse_long(sep, &v)) continue;
            if (!strcmp(lines[i], "UPTIME")) { h->remote_linux_uptime = v; fields |= 4; }
            else if (!strcmp(lines[i], "MEM_TOTAL")) { h->remote_linux_mem_total = v; fields |= 8; }
            else if (!strcmp(lines[i], "MEM_AVAILABLE")) { h->remote_linux_mem_available = v; fields |= 16; }
            else if (!strcmp(lines[i], "SWAP_TOTAL")) { h->remote_linux_swap_total = v; fields |= 32; }
            else if (!strcmp(lines[i], "SWAP_FREE")) { h->remote_linux_swap_free = v; fields |= 64; }
            else if (!strcmp(lines[i], "DISK_TOTAL")) { h->remote_linux_disk_total = v; fields |= 128; }
            else if (!strcmp(lines[i], "DISK_USED")) { h->remote_linux_disk_used = v; fields |= 256; }
        }
    }
    g_strfreev(lines);
    return fields == 511 && h->remote_linux_mem_available <= h->remote_linux_mem_total &&
           h->remote_linux_swap_free <= h->remote_linux_swap_total &&
           h->remote_linux_disk_used <= h->remote_linux_disk_total;
}
static void finished(AppContext *app, const HostSSHResult *result, gpointer data)
{
    RemoteJob *job = data;
    HostDetails sample = {0};
    gboolean ok = result->success && parse_sample(result->stdout_text, &sample);
    HostAnalysisContext *ctx = &app->host_analysis;
    HostDetails persistence_snapshot = {0};

    char *lt_pl   = g_strdup(global_app->ansi.lt_purple);
    char *cy      = g_strdup(global_app->ansi.cyan);
    char *yl      = g_strdup(global_app->ansi.yellow);
    char *gr      = g_strdup(global_app->ansi.green);
    char *red     = g_strdup(global_app->ansi.red);
    char *nml     = g_strdup(global_app->ansi.normal);

    if (ctx->initialized) {
        pthread_mutex_lock(&ctx->mutex);
        HostDetails *h = host_analysis_find_or_create_locked(ctx, job->hostname);
        if (h) {
            h->source_flags |= HOST_COLLECTOR_SSH;
            h->remote_linux_collected_at = time(NULL);
            h->remote_linux_result = ok ? HOST_COLLECT_OK : HOST_COLLECT_ERROR;
            h->remote_linux_in_flight = FALSE;
            if (ok) h->remote_linux_last_success = h->remote_linux_collected_at;
            if (ok) {
                h->remote_linux_valid = TRUE;
                g_strlcpy(h->remote_linux_kernel, sample.remote_linux_kernel, sizeof(h->remote_linux_kernel));
                h->remote_linux_load = sample.remote_linux_load;
                h->remote_linux_uptime = sample.remote_linux_uptime;
                h->remote_linux_mem_total = sample.remote_linux_mem_total;
                h->remote_linux_mem_available = sample.remote_linux_mem_available;
                h->remote_linux_swap_total = sample.remote_linux_swap_total;
                h->remote_linux_swap_free = sample.remote_linux_swap_free;
                h->remote_linux_disk_total = sample.remote_linux_disk_total;
                h->remote_linux_disk_used = sample.remote_linux_disk_used;
                h->remote_linux_error[0] = '\0';
                h->state = HOST_ONLINE;
                h->last_update = h->remote_linux_collected_at;
                if (!h->description[0]) g_strlcpy(h->description, "Remote Linux host", sizeof(h->description));
            } else {
                g_strlcpy(h->remote_linux_error,
                    result->error_message ? result->error_message :
                    (result->success ? "Incomplete remote Linux response" : "SSH command failed"),
                    sizeof(h->remote_linux_error));
            }
        }
        if (h) persistence_snapshot = *h;
        pthread_mutex_unlock(&ctx->mutex);
    }
    if (persistence_snapshot.hostname[0])
        host_db_save_collection_result(app, &persistence_snapshot, ok,
            result->elapsed_ms,
            ok ? "" : (result->error_message ? result->error_message :
                (result->success ? "Incomplete remote Linux response" : "SSH command failed")));
    gchar *message = g_strdup_printf("SSH Linux collection %s: %s%s%s\n",
        ok ? "SUCCESS" : "FAILED", job->hostname,
        ok ? "" : " : ", ok ? "" : (result->error_message ? result->error_message : "Incomplete response or SSH failure"));
    if (!job->quiet) write_to_ai_pane(app, "Hosts: ", message, "system_tag", "body_tag");
    else {
        DEBUG_PRINT("[ %sDEBUG%s ]: [%sSSH%s] %sHost Analysis scheduled SSH: %s%s%s\n", 
		lt_pl, nml, cy, nml, gr,
		red, message, nml);
    }
    g_free(message);
    g_free(job->hostname);
    g_free(job);

    g_free(lt_pl);
    g_free(cy);
    g_free(yl);
    g_free(gr);
    g_free(red);
    g_free(nml);

}

static gboolean collect_impl(AppContext *app, const char *hostname, gboolean quiet, GError **error)
{
    if (!app || !app->host_analysis.initialized || !hostname || !*hostname ||
        strlen(hostname) >= sizeof(((HostDetails *)0)->hostname) ||
        hostname[0] == '-' || strpbrk(hostname, "\\/;|&$`'\"<>!(){}[] \t\n")) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Invalid SSH host or Host Analysis not initialized");
        return FALSE;
    }
    HostAnalysisContext *ctx = &app->host_analysis;
    pthread_mutex_lock(&ctx->mutex);
    HostDetails *host = host_analysis_find_or_create_locked(ctx, hostname);
    if (!host) {
        pthread_mutex_unlock(&ctx->mutex);
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NO_SPACE, "Host registry full");
        return FALSE;
    }
    if (host->remote_linux_in_flight) {
        pthread_mutex_unlock(&ctx->mutex);
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_PENDING, "SSH collection already in progress");
        return FALSE;
    }
    host->remote_linux_in_flight = TRUE;
    pthread_mutex_unlock(&ctx->mutex);
    HostSSHProfile profile = {0};
    g_strlcpy(profile.hostname, hostname, sizeof(profile.hostname));
    profile.auth_method = HOST_SSH_CONFIG;
    profile.enabled = TRUE;
    RemoteJob *job = g_new0(RemoteJob, 1);
    job->hostname = g_strdup(hostname);
    job->quiet = quiet;
    if (!host_ssh_execute_async(app, &profile, remote_command, host_ssh_adaptive_timeout(app, hostname, FALSE), finished, job, error)) {
        pthread_mutex_lock(&ctx->mutex);
        host = host_analysis_find_or_create_locked(ctx, hostname);
        if (host) host->remote_linux_in_flight = FALSE;
        pthread_mutex_unlock(&ctx->mutex);
        g_free(job->hostname); g_free(job); return FALSE;
    }
    return TRUE;
}

gboolean host_linux_remote_collect(AppContext *app, const char *hostname, GError **error)
{ return collect_impl(app, hostname, FALSE, error); }

gboolean host_linux_remote_collect_scheduled(AppContext *app, const char *hostname, GError **error)
{ return collect_impl(app, hostname, TRUE, error); }
