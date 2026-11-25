// part of the aiterm project
// host_ssh.c - additive host telemetry collection
// AITerm 0.9.14-delta-3: asynchronous OpenSSH transport.
// No passwords, no interactive prompts, no host registry writes.
// aiterm The terminal emulator with an AI Pane
// AITerm Delta-2: read-only Host Analysis Manager.
// Shares the existing .session-dialog CSS class with other managers.
// GTK access is confined to the main thread; collector data is snapshotted.
// By: Peter Talbott
// Assisted by: OpenAI               
// October 2026

#include <string.h>
#include "host_db.h"
#include "host_ssh.h"
#include "host_analysis.h"
#include "utils.h"

/* Only the host registry owns adaptive timing; no SSH I/O under its mutex. */
guint host_ssh_adaptive_timeout(AppContext *app, const char *hostname, gboolean test)
{
    if (test) return 60;
    guint seconds = 60; /* safe initial collection before calibration */
    if (!app || !hostname || !app->host_analysis.initialized) return seconds;
    HostAnalysisContext *ctx = &app->host_analysis;
    pthread_mutex_lock(&ctx->mutex);
    HostDetails *host = host_analysis_find_or_create_locked(ctx, hostname);
    if (host && host->ssh_success_count) {
        guint ms = host->ssh_recent_max_ms;
        seconds = (ms + 999) / 1000 + 5;
        if (seconds < 15) seconds = 15;
        if (seconds > 120) seconds = 120;
    }
    if (host) host->ssh_timeout_seconds = seconds;
    pthread_mutex_unlock(&ctx->mutex);
    return seconds;
}

void record_ssh_timing(SSHJob *job, const HostSSHResult *result)
{
    if (!job->app->host_analysis.initialized) return;

    char *lt_pl   = g_strdup(global_app->ansi.lt_purple);
    char *cy      = g_strdup(global_app->ansi.cyan);
    char *yl      = g_strdup(global_app->ansi.yellow);
    char *gr      = g_strdup(global_app->ansi.green);
    char *red     = g_strdup(global_app->ansi.red);
    char *nml     = g_strdup(global_app->ansi.normal);

    HostAnalysisContext *ctx = &job->app->host_analysis;
    pthread_mutex_lock(&ctx->mutex);
    HostDetails *host = host_analysis_find_or_create_locked(ctx, job->hostname);
    if (host) {
        host->ssh_last_duration_ms = result->elapsed_ms;
        if (result->success) {
            /* Retain the highest recent observation. A new fast sample must
             * not immediately erase the slow-host safety allowance. */
            if (result->elapsed_ms > host->ssh_recent_max_ms)
                host->ssh_recent_max_ms = result->elapsed_ms;
            host->ssh_success_count++;
        } else if (result->timed_out) {
            host->ssh_timeout_count++;
            guint ms = job->timeout_seconds * 1000;
            if (ms > host->ssh_recent_max_ms) host->ssh_recent_max_ms = ms;
        }
    }
    HostDetails snapshot = {0};
    if (host) snapshot = *host;
    pthread_mutex_unlock(&ctx->mutex);
    if (snapshot.hostname[0]) host_db_save_timing(job->app, &snapshot);
    char *SUCCESS_VAL = g_malloc(64);
    char *FAIL_VAL = g_malloc(64);
    snprintf(SUCCESS_VAL, 64, "%sSUCCESS%s", gr, nml);
    snprintf(FAIL_VAL, 64, "%sFAILED%s", red, nml);
    DEBUG_PRINT("[ %sDEBUG%s ]: [%sSSH%s] %sHost SSH [%s%s%s] : %s %selapsed=%s%u ms%s, limit=%s%u s%s, exit=%s%d%s\n",
	lt_pl, nml, cy, nml, gr,
        red, job->hostname, gr,
	result->success ? SUCCESS_VAL : FAIL_VAL, gr,
        yl, result->elapsed_ms, gr,
	yl, job->timeout_seconds, gr, 
	red, result->exit_status, nml);

    g_free(SUCCESS_VAL);
    g_free(FAIL_VAL);

    g_free(lt_pl);
    g_free(cy);
    g_free(yl);
    g_free(gr);
    g_free(red);
    g_free(nml);

}

gboolean ssh_timeout(gpointer data)
{
    SSHJob *job = data;
    job->timeout_id = 0;
    job->timed_out = TRUE;
    g_cancellable_cancel(job->cancellable);
    g_subprocess_force_exit(job->process);
    return G_SOURCE_REMOVE;
}

void ssh_complete(GObject *source, GAsyncResult *async, gpointer data)
{
    SSHJob *job = data;
    HostSSHResult result = {0};
    GError *error = NULL;
    result.exit_status = -1;
    result.timed_out = job->timed_out;
    result.elapsed_ms = (guint)MIN((g_get_monotonic_time() - job->started_us) / 1000, G_MAXUINT);
    if (job->timeout_id) g_source_remove(job->timeout_id);
    gboolean communicated = g_subprocess_communicate_utf8_finish(
        G_SUBPROCESS(source), async, &result.stdout_text, &result.stderr_text, &error);
    if (communicated && g_subprocess_get_if_exited(job->process))
        result.exit_status = g_subprocess_get_exit_status(job->process);
    result.success = communicated && result.exit_status == 0 && !result.timed_out;
    if (result.timed_out) {
        result.error_message = g_strdup("SSH execution timed out");
    } else if (error) {
        result.error_message = g_strdup(error->message);
        g_error_free(error);
    } else if (!result.success) {
        result.error_message = g_strdup_printf("SSH exited with status %d", result.exit_status);
    }
    record_ssh_timing(job, &result);
    if (job->completion) job->completion(job->app, &result, job->user_data);
    g_free(result.stdout_text);
    g_free(result.stderr_text);
    g_free(result.error_message);
    g_object_unref(job->process);
    g_object_unref(job->cancellable);
    g_free(job->hostname);
    g_free(job);
}

gboolean host_ssh_execute_async(AppContext *app, const HostSSHProfile *profile,
                                const gchar *remote_command, guint timeout_seconds,
                                HostSSHCompletion completion, gpointer user_data,
                                GError **error)
{
    if (!app || !profile || !profile->hostname[0] || !remote_command ||
        !*remote_command || !completion || !profile->enabled) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                            "SSH requires an enabled profile, hostname, command and callback");
        return FALSE;
    }
    if (profile->auth_method == HOST_SSH_PASSWORD) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                            "Password SSH is not supported in delta-3; use SSH keys or ssh config");
        return FALSE;
    }
    if (profile->auth_method == HOST_SSH_KEY && !profile->identity_file[0]) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                            "Key authentication requires an identity file");
        return FALSE;
    }
    /* Hostname is a separate argv element; -- prevents option injection. */
    GPtrArray *argv = g_ptr_array_new_with_free_func(g_free);
#define ARG(s) g_ptr_array_add(argv, g_strdup(s))
    ARG("ssh");
    ARG("-T");
    ARG("-o"); ARG("BatchMode=yes");
    ARG("-o"); ARG("NumberOfPasswordPrompts=0");
    ARG("-o"); ARG("ConnectionAttempts=1");
    ARG("-o"); ARG("StrictHostKeyChecking=yes");
    ARG("-o"); ARG("ConnectTimeout=8");
    if (profile->port) {
        ARG("-p");
        g_ptr_array_add(argv, g_strdup_printf("%u", profile->port));
    }
    if (profile->username[0]) {
        ARG("-l"); ARG(profile->username);
    }
    if (profile->auth_method == HOST_SSH_KEY) {
        ARG("-i"); ARG(profile->identity_file);
        ARG("-o"); ARG("IdentitiesOnly=yes");
    }
    ARG("--"); ARG(profile->hostname); ARG(remote_command);
    g_ptr_array_add(argv, NULL);
#undef ARG
    GSubprocess *process = g_subprocess_newv((const gchar *const *)argv->pdata,
        G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_PIPE, error);
    g_ptr_array_free(argv, TRUE);
    if (!process) return FALSE;
    SSHJob *job = g_new0(SSHJob, 1);
    job->app = app;
    job->process = process;
    job->cancellable = g_cancellable_new();
    job->completion = completion;
    job->user_data = user_data;
    job->hostname = g_strdup(profile->hostname);
    job->started_us = g_get_monotonic_time();
    job->timeout_seconds = timeout_seconds ? timeout_seconds : 60;
    job->timeout_id = g_timeout_add_seconds(job->timeout_seconds,
                                            ssh_timeout, job);
    g_subprocess_communicate_utf8_async(process, NULL, job->cancellable,
                                        ssh_complete, job);
    return TRUE;
}
