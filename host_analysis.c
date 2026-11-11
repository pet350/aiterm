// part of the aiterm project
// host_analysis.c
// aiterm The terminal emulator with an AI Pane
// Enables host analysis rom multiple sources
// host_analysis.c - additive host telemetry collection
// Existing SNMP polling and terminal/TEE workflows remain independent.
// By: Peter Talbott
// Assisted by: OpenAI and MS Copilot
// October 2026

#include "host_analysis.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/statvfs.h>
#include <sys/utsname.h>
#include <unistd.h>

static void read_local_host(HostDetails *host)
{
    FILE *file;
    char line[256];
    struct utsname system_info;
    struct statvfs filesystem_info;
    double one_minute = 0.0;
    unsigned long long memory_total_kb = 0;
    unsigned long long memory_available_kb = 0;
    unsigned long long swap_total_kb = 0;
    unsigned long long swap_free_kb = 0;
    double uptime_seconds = 0.0;

    memset(host, 0, sizeof(*host));
    host->source = HOST_SOURCE_LOCAL;
    host->source_flags = HOST_COLLECTOR_LOCAL;
    host->state = HOST_ONLINE;

    if (uname(&system_info) == 0) {
        g_strlcpy(host->hostname, system_info.nodename, sizeof(host->hostname));
        g_snprintf(host->description, sizeof(host->description), "%s %s (%s)",
                   system_info.sysname, system_info.release, system_info.machine);
    } else {
        g_strlcpy(host->hostname, "localhost", sizeof(host->hostname));
        g_strlcpy(host->description, "Local host", sizeof(host->description));
    }

    file = fopen("/proc/loadavg", "r");
    if (file) {
        if (fscanf(file, "%lf", &one_minute) == 1)
            host->load_avg = one_minute;
        fclose(file);
    }

    file = fopen("/proc/meminfo", "r");
    if (file) {
        while (fgets(line, sizeof(line), file)) {
            unsigned long long value = 0;
            if (sscanf(line, "MemTotal: %llu kB", &value) == 1)
                memory_total_kb = value;
            else if (sscanf(line, "MemAvailable: %llu kB", &value) == 1)
                memory_available_kb = value;
            else if (sscanf(line, "SwapTotal: %llu kB", &value) == 1)
                swap_total_kb = value;
            else if (sscanf(line, "SwapFree: %llu kB", &value) == 1)
                swap_free_kb = value;
        }
        fclose(file);
    }
    host->memory_total = (long)(memory_total_kb * 1024ULL);
    host->memory_used = (long)((memory_total_kb > memory_available_kb ?
                                memory_total_kb - memory_available_kb : 0) * 1024ULL);
    host->swap_total = (long)(swap_total_kb * 1024ULL);
    host->swap_used = (long)((swap_total_kb > swap_free_kb ?
                              swap_total_kb - swap_free_kb : 0) * 1024ULL);

    file = fopen("/proc/uptime", "r");
    if (file) {
        if (fscanf(file, "%lf", &uptime_seconds) == 1)
            host->uptime_seconds = (long)uptime_seconds;
        fclose(file);
    }

    if (statvfs("/", &filesystem_info) == 0) {
        unsigned long long block_size = filesystem_info.f_frsize ?
                                        filesystem_info.f_frsize : filesystem_info.f_bsize;
        unsigned long long total = (unsigned long long)filesystem_info.f_blocks * block_size;
        unsigned long long available = (unsigned long long)filesystem_info.f_bavail * block_size;
        host->disk_total = (long)total;
        host->disk_used = (long)(total > available ? total - available : 0);
    }

    host->last_update = time(NULL);
}

/* Local systemd collector. This never runs SSH or modifies unit state.
 * A failed query is distinct from a successful query reporting zero failures. */
static void host_analysis_collect_systemd(HostDetails *host)
{
    gchar *stdout_text = NULL, *stderr_text = NULL;
    gint exit_status = -1;
    GError *error = NULL;
    gchar *argv[] = { (gchar *)"systemctl", (gchar *)"--system",
        (gchar *)"--no-pager", (gchar *)"--plain",
        (gchar *)"--no-legend", (gchar *)"--no-ask-password",
        (gchar *)"--failed", (gchar *)"--type=service", NULL };
    gboolean launched = g_spawn_sync(NULL, argv, NULL, G_SPAWN_SEARCH_PATH,
                                    NULL, NULL, &stdout_text, &stderr_text,
                                    &exit_status, &error);
    host->systemd_collected_at = time(NULL);
    host->source_flags |= HOST_COLLECTOR_SYSTEMD;
    if (!launched || !g_spawn_check_wait_status(exit_status, &error)) {
        host->systemd_result = HOST_COLLECT_ERROR;
        g_strlcpy(host->systemd_error,
                  error ? error->message : (stderr_text && *stderr_text ? stderr_text : "systemctl failed"),
                  sizeof(host->systemd_error));
        if (error) g_error_free(error);
        g_free(stdout_text);
        g_free(stderr_text);
        return;
    }
    host->systemd_result = HOST_COLLECT_OK;
    if (stdout_text) {
        gchar **lines = g_strsplit(stdout_text, "\n", -1);
        for (guint i = 0; lines[i]; ++i) {
            gchar *line = g_strstrip(lines[i]);
            if (!*line) continue;
            /* --plain --no-legend prints the unit name as its first column. */
            gchar **parts = g_strsplit_set(line, " \t", 2);
            if (parts[0] && *parts[0]) {
                ++host->failed_services;
                if (host->systemd_failed_stored < HOST_SYSTEMD_MAX_FAILED)
                    g_strlcpy(host->systemd_failed_units[host->systemd_failed_stored++],
                              parts[0], HOST_SYSTEMD_UNIT_NAME_MAX);
            }
            g_strfreev(parts);
        }
        g_strfreev(lines);
    }
    g_free(stdout_text);
    g_free(stderr_text);
}

/* All callers must hold ctx->mutex. Never return this pointer after unlocking.
 * Host identity is independent of the collector that discovered it. */
HostDetails *host_analysis_find_or_create_locked(HostAnalysisContext *ctx,
                                                        const char *hostname)
{
    if (!hostname || !*hostname) return NULL;
    gchar *key = g_ascii_strdown(hostname, -1);
    gsize len = strlen(key);
    while (len && key[len - 1] == '.') key[--len] = '\0';
    HostDetails *result = NULL;
    for (int i = 0; i < ctx->count && i < MAX_HOSTS; ++i) {
        if (g_ascii_strcasecmp(ctx->hosts[i].hostname, key) == 0 ||
            (strlen(ctx->hosts[i].hostname) == len + 1 &&
             ctx->hosts[i].hostname[len] == '.' &&
             g_ascii_strncasecmp(ctx->hosts[i].hostname, key, len) == 0)) {
            result = &ctx->hosts[i];
            break;
        }
    }
    if (!result && ctx->count < MAX_HOSTS) {
        result = &ctx->hosts[ctx->count++];
        memset(result, 0, sizeof(*result));
        g_strlcpy(result->hostname, hostname, sizeof(result->hostname));
        result->state = HOST_UNKNOWN;
    }
    g_free(key);
    return result;
}

/* Refresh local host data independently of SNMP. Collect before locking. */
static void host_analysis_refresh_local(AppContext *app)
{
    if (!app || !app->host_analysis.initialized) return;
    HostDetails local_host;
    read_local_host(&local_host);
    host_analysis_collect_systemd(&local_host);
    HostAnalysisContext *ctx = &app->host_analysis;
    pthread_mutex_lock(&ctx->mutex);
    HostDetails *host = host_analysis_find_or_create_locked(ctx, local_host.hostname);
    if (host) {
        /* Keep other collectors' membership and newer timestamps intact. */
        guint existing_flags = host->source_flags;
        time_t existing_update = host->last_update;
        HostDetails previous = *host;
        *host = local_host; /* SNMP observations live in a separate array. */
        host->remote_linux_valid = previous.remote_linux_valid;
        host->remote_linux_result = previous.remote_linux_result;
        host->remote_linux_collected_at = previous.remote_linux_collected_at;
        g_strlcpy(host->remote_linux_kernel, previous.remote_linux_kernel, sizeof(host->remote_linux_kernel));
        g_strlcpy(host->remote_linux_error, previous.remote_linux_error, sizeof(host->remote_linux_error));
        host->remote_linux_load = previous.remote_linux_load;
        host->remote_linux_uptime = previous.remote_linux_uptime;
        host->remote_linux_mem_total = previous.remote_linux_mem_total;
        host->remote_linux_mem_available = previous.remote_linux_mem_available;
        host->remote_linux_swap_total = previous.remote_linux_swap_total;
        host->remote_linux_swap_free = previous.remote_linux_swap_free;
        host->remote_linux_disk_total = previous.remote_linux_disk_total;
        host->remote_linux_disk_used = previous.remote_linux_disk_used;
        host->source_flags |= existing_flags;
        if (existing_update > host->last_update) host->last_update = existing_update;
    }
    pthread_mutex_unlock(&ctx->mutex);
}

/* Snapshot SNMP under its own lock, then update Host Analysis under its lock.
 * Never hold both locks, poll SNMP, or run external commands here. */
void host_analysis_import_snmp(AppContext *app)
{
    if (!app || !app->host_analysis.initialized) return;
    GArray *samples = g_array_new(FALSE, FALSE, sizeof(SnmpHost));
    if (app->SnmpContext.initialized) {
        pthread_mutex_lock(&app->SnmpContext.lock);
        for (guint i = 0; i < MAX_SNMP_HOSTS; ++i) {
            const SnmpHost *m = &app->SnmpMetric[i];
            if (m->is_active && m->ip_address[0] && m->oid_str[0] &&
                m->last_updated > 0 && m->last_value[0])
                g_array_append_val(samples, *m);
        }
        pthread_mutex_unlock(&app->SnmpContext.lock);
    }

    HostAnalysisContext *ctx = &app->host_analysis;
    pthread_mutex_lock(&ctx->mutex);
    /* Remove old SNMP-only records; retain local and future collector records.
     * Rebuild all observation indices against the surviving host records. */
    guint retained = 0;
    for (int i = 0; i < ctx->count && i < MAX_HOSTS; ++i) {
        if (!(ctx->hosts[i].source_flags & ~HOST_COLLECTOR_SNMP) &&
            (ctx->hosts[i].source_flags & HOST_COLLECTOR_SNMP)) continue;
        ctx->hosts[i].source_flags &= ~HOST_COLLECTOR_SNMP;
        if (retained != (guint)i) ctx->hosts[retained] = ctx->hosts[i];
        ++retained;
    }
    ctx->count = (int)retained;
    HostSnmpObservation *observations = g_new0(HostSnmpObservation, samples->len);
    guint nobs = 0;
    for (guint i = 0; i < samples->len; ++i) {
        const SnmpHost *m = &g_array_index(samples, SnmpHost, i);
        HostDetails *host = host_analysis_find_or_create_locked(ctx, m->ip_address);
        if (!host) continue;
        guint host_index = (guint)(host - ctx->hosts);
        if (!host->description[0])
            g_strlcpy(host->description, "SNMP target", sizeof(host->description));
        host->source_flags |= HOST_COLLECTOR_SNMP;
        if (!(host->source_flags & (HOST_COLLECTOR_LOCAL | HOST_COLLECTOR_SSH)))
            host->source = HOST_SOURCE_SNMP;
        host->state = HOST_ONLINE;
        if (m->last_updated > host->last_update) host->last_update = m->last_updated;
        HostSnmpObservation *o = &observations[nobs++];
        g_strlcpy(o->target, m->ip_address, sizeof(o->target));
        g_strlcpy(o->oid, m->oid_str, sizeof(o->oid));
        g_strlcpy(o->label, m->label, sizeof(o->label));
        g_strlcpy(o->value, m->last_value, sizeof(o->value));
        o->sampled_at = m->last_updated;
        o->host_index = host_index;
    }
    g_free(ctx->snmp_observations);
    ctx->snmp_observations = observations;
    ctx->snmp_observation_count = nobs;
    pthread_mutex_unlock(&ctx->mutex);
    g_array_free(samples, TRUE);
}

/* Full refresh entry point for manual requests and the periodic timer. */
void host_analysis_refresh_all(AppContext *app)
{
    if (!app || !app->host_analysis.initialized) return;
    host_analysis_refresh_local(app);
    host_analysis_import_snmp(app);
}

/* Compatibility with existing callers. */
void host_analysis_analyze_all(AppContext *app)
{
    host_analysis_refresh_all(app);
}

static void *host_analysis_collector_main(void *argument)
{
    AppContext *app = argument;
    HostAnalysisContext *context = &app->host_analysis;

    for (;;) {
        struct timespec deadline;
        int wait_result = 0;

        host_analysis_refresh_all(app);

        pthread_mutex_lock(&context->mutex);
        if (!context->running) {
            pthread_mutex_unlock(&context->mutex);
            break;
        }

        clock_gettime(CLOCK_REALTIME, &deadline);
        deadline.tv_sec += context->refresh_interval ? context->refresh_interval : 300;
        while (context->running && wait_result != ETIMEDOUT)
            wait_result = pthread_cond_timedwait(&context->condition,
                                                 &context->mutex, &deadline);
        gboolean keep_running = context->running;
        pthread_mutex_unlock(&context->mutex);

        if (!keep_running)
            break;
    }

    return NULL;
}

int host_analysis_init(AppContext *app)
{
    HostAnalysisContext *context;
    if (!app)
        return -1;

    context = &app->host_analysis;
    if (context->initialized)
        return 0;

    context->count = 0;
    context->running = FALSE;
    context->collector_started = FALSE;
    context->refresh_interval = 300;
    context->analysis_interval = 600;

    if (pthread_mutex_init(&context->mutex, NULL) != 0)
        return -1;
    if (pthread_cond_init(&context->condition, NULL) != 0) {
        pthread_mutex_destroy(&context->mutex);
        return -1;
    }

    context->initialized = TRUE;
    return 0;
}

void host_analysis_start(AppContext *app)
{
    HostAnalysisContext *context;
    if (!app)
        return;

    context = &app->host_analysis;
    if (!context->initialized && host_analysis_init(app) != 0)
        return;

    pthread_mutex_lock(&context->mutex);
    if (context->running || context->collector_started) {
        pthread_mutex_unlock(&context->mutex);
        return;
    }
    context->running = TRUE;
    pthread_mutex_unlock(&context->mutex);

    if (pthread_create(&context->collector_thread, NULL,
                       host_analysis_collector_main, app) != 0) {
        pthread_mutex_lock(&context->mutex);
        context->running = FALSE;
        pthread_mutex_unlock(&context->mutex);
        return;
    }

    pthread_mutex_lock(&context->mutex);
    context->collector_started = TRUE;
    pthread_mutex_unlock(&context->mutex);
}

void host_analysis_stop(AppContext *app)
{
    HostAnalysisContext *context;
    gboolean join_thread;
    if (!app)
        return;

    context = &app->host_analysis;
    if (!context->initialized)
        return;

    pthread_mutex_lock(&context->mutex);
    context->running = FALSE;
    pthread_cond_broadcast(&context->condition);
    join_thread = context->collector_started;
    context->collector_started = FALSE;
    pthread_mutex_unlock(&context->mutex);

    if (join_thread)
        pthread_join(context->collector_thread, NULL);
}

void host_analysis_shutdown(AppContext *app)
{
    HostAnalysisContext *context;
    if (!app)
        return;

    context = &app->host_analysis;
    if (!context->initialized)
        return;

    host_analysis_stop(app);
    g_free(context->snmp_observations);
    context->snmp_observations = NULL;
    context->snmp_observation_count = 0;
    pthread_cond_destroy(&context->condition);
    pthread_mutex_destroy(&context->mutex);
    context->initialized = FALSE;
}

/* Read-only access: never expose a pointer to the mutable hosts array. */
gboolean host_analysis_get_host(AppContext *app, guint index, HostDetails *out)
{
    HostAnalysisContext *context;
    if (!app || !out)
        return FALSE;
    context = &app->host_analysis;
    if (!context->initialized)
        return FALSE;
    pthread_mutex_lock(&context->mutex);
    gboolean found = index < (guint)context->count && index < MAX_HOSTS;
    if (found)
        *out = context->hosts[index];
    pthread_mutex_unlock(&context->mutex);
    return found;
}

/* Source membership is independent of host identity and primary source. */
static gchar *host_analysis_source_names(guint flags)
{
    GString *names = g_string_new(NULL);
    struct { guint bit; const char *name; } sources[] = {
        { HOST_COLLECTOR_LOCAL, "Local" },
        { HOST_COLLECTOR_SYSTEMD, "Systemd" },
        { HOST_COLLECTOR_SNMP, "SNMP" },
        { HOST_COLLECTOR_SSH, "SSH" },
        { HOST_COLLECTOR_DATABASE, "Database" }
    };
    for (guint i = 0; i < G_N_ELEMENTS(sources); ++i) {
        if (!(flags & sources[i].bit)) continue;
        if (names->len) g_string_append(names, ", ");
        g_string_append(names, sources[i].name);
    }
    if (!names->len) g_string_append(names, "None");
    return g_string_free(names, FALSE);
}

/* Pure formatter: no GTK calls, provider calls, or SNMP side effects. */
gchar *host_analysis_format_host(const HostDetails *host)
{
    if (!host)
        return NULL;
    gchar *sources = host_analysis_source_names(host->source_flags);
    gchar *result = g_strdup_printf(
        "Host: %s\nSystem: %s\nSources: %s\n"
        "Load average (1m): %.2f\n"
        "Memory: %ld / %ld bytes used\n"
        "Swap: %ld / %ld bytes used\n"
        "Root filesystem: %ld / %ld bytes used\n"
        "Uptime: %ld seconds\nCollected at: %lld\n",
        host->hostname, host->description,
        sources,
        host->load_avg, host->memory_used, host->memory_total,
        host->swap_used, host->swap_total, host->disk_used, host->disk_total,
        host->uptime_seconds, (long long)host->last_update);
    if (host->source_flags & HOST_COLLECTOR_SSH) {
        gchar *remote = g_strdup_printf(
            "\nRemote Linux (SSH): %s\nKernel: %s\nLoad (1m): %.2f\n"
            "Uptime: %lld seconds\nMemory: %lld / %lld bytes used\n"
            "Swap: %lld / %lld bytes used\nRoot filesystem: %lld / %lld bytes used\n"
            "Last SSH collection: %lld\n%s%s",
            host->remote_linux_valid ? "Collected" : "No successful sample",
            host->remote_linux_kernel, host->remote_linux_load,
            host->remote_linux_uptime,
            host->remote_linux_mem_total - host->remote_linux_mem_available,
            host->remote_linux_mem_total,
            host->remote_linux_swap_total - host->remote_linux_swap_free,
            host->remote_linux_swap_total,
            host->remote_linux_disk_used, host->remote_linux_disk_total,
            (long long)host->remote_linux_collected_at,
            host->remote_linux_error[0] ? "SSH error: " : "",
            host->remote_linux_error);
        gchar *combined = g_strconcat(result, remote, NULL);
        g_free(result); g_free(remote); result = combined;
    }
    g_free(sources);
    return result;
}

/* Read-only dump: copy the entire table under its mutex, then format it. */
gchar *host_analysis_format_all(AppContext *app)
{
    if (!app || !app->host_analysis.initialized)
        return g_strdup("Host Analysis is not initialized.\n");

    HostAnalysisContext *ctx = &app->host_analysis;
    pthread_mutex_lock(&ctx->mutex);
    guint count = (guint)CLAMP(ctx->count, 0, MAX_HOSTS);
    gboolean running = ctx->running;
    HostDetails *snapshot = count ? g_new(HostDetails, count) : NULL;
    if (count) memcpy(snapshot, ctx->hosts, count * sizeof(*snapshot));
    guint observation_count = ctx->snmp_observation_count;
    HostSnmpObservation *observations = observation_count ?
        g_memdup2(ctx->snmp_observations, observation_count * sizeof(*observations)) : NULL;
    pthread_mutex_unlock(&ctx->mutex);

    GString *out = g_string_new("========================================\n"
                                "       AITerm Host Analysis Dump\n"
                                "========================================\n");
    g_string_append_printf(out, "Collector: %s | Hosts: %u\n",
                           running ? "running" : "stopped", count);
    if (!count) g_string_append(out, "No host observations collected yet.\n");
    for (guint i = 0; i < count; ++i) {
        const HostDetails *host = &snapshot[i];
        gchar *source = host_analysis_source_names(host->source_flags);
        const char *state = host->state == HOST_ONLINE ? "Online" :
                            host->state == HOST_OFFLINE ? "Offline" :
                            host->state == HOST_WARNING ? "Warning" :
                            host->state == HOST_CRITICAL ? "Critical" : "Unknown";
        char timestamp[64] = "Unknown";
        struct tm tm_value;
        if (host->last_update && localtime_r(&host->last_update, &tm_value))
            strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S %Z", &tm_value);
        g_string_append_printf(out,
            "\n--- Host %u of %u --------------------------------\n"
            "Hostname       : %s\nDescription    : %s\n"
            "Sources        : %s\nState          : %s\n"
            "Last collected : %s\nLoad avg (1m)  : %.2f\n"
            "Memory         : %ld / %ld bytes used\n"
            "Swap           : %ld / %ld bytes used\n"
            "Root disk      : %ld / %ld bytes used\n"
            "Uptime         : %ld seconds\n",
            i + 1, count, host->hostname, host->description, source, state,
            timestamp, host->load_avg, host->memory_used, host->memory_total,
            host->swap_used, host->swap_total, host->disk_used, host->disk_total,
            host->uptime_seconds);
        g_free(source);
        if (host->source_flags & HOST_COLLECTOR_LOCAL)
            g_string_append(out,
                "CPU utilization: Not collected yet\n");
        if (host->source_flags & HOST_COLLECTOR_LOCAL) {
            if (host->systemd_result == HOST_COLLECT_OK) {
                g_string_append_printf(out, "Failed services: %d (systemd, collected)\n",
                                       host->failed_services);
                for (guint j = 0; j < host->systemd_failed_stored; ++j)
                    g_string_append_printf(out, "  Failed unit: %s\n", host->systemd_failed_units[j]);
                if ((guint)host->failed_services > host->systemd_failed_stored)
                    g_string_append_printf(out, "  ... %u additional failed units omitted\n",
                        (guint)host->failed_services - host->systemd_failed_stored);
            } else if (host->systemd_result == HOST_COLLECT_ERROR)
                g_string_append_printf(out, "Failed services: Collection error: %s\n",
                                       host->systemd_error);
            else
                g_string_append(out, "Failed services: Not collected yet\n");
            g_string_append(out, "TCP connections: Not collected yet\n"
                                 "Journal errors : Not collected yet\n");
        }
        else if (!(host->source_flags & HOST_COLLECTOR_SNMP))
            g_string_append_printf(out,
                "CPU utilization: %.2f%%\nFailed services: %d\n"
                "TCP connections: %d\nJournal errors : %lu\n",
                host->cpu_percent, host->failed_services,
                host->tcp_connections, host->journal_errors);
        guint n = 0;
        for (guint j = 0; j < observation_count; ++j)
            if (observations[j].host_index == i) ++n;
        if (n) {
            g_string_append_printf(out, "SNMP observations: %u\n", n);
            for (guint j = 0; j < observation_count; ++j) {
                const HostSnmpObservation *o = &observations[j];
                if (o->host_index != i) continue;
                g_string_append_printf(out, "  %s [%s] = %s (sample: %ld)\n",
                    o->label, o->oid, o->value, (long)o->sampled_at);
            }
        }
    }
    g_free(snapshot);
    g_free(observations);
    g_string_append(out, "\n========================================\n");
    return g_string_free(out, FALSE);
}

/* Report existing collector and SNMP state without polling or modifying either. */
gchar *host_analysis_format_status(AppContext *app)
{
    if (!app) return g_strdup("Host Analysis: application context unavailable.\n");
    HostAnalysisContext *ctx = &app->host_analysis;
    if (!ctx->initialized)
        return g_strdup("Host Analysis is not initialized.\n");

    guint local = 0, snmp = 0, other = 0, interval = 0;
    gboolean running;
    time_t latest = 0;
    pthread_mutex_lock(&ctx->mutex);
    running = ctx->running;
    interval = ctx->refresh_interval;
    for (int i = 0; i < ctx->count && i < MAX_HOSTS; i++) {
        const HostDetails *h = &ctx->hosts[i];
        if (h->source_flags & HOST_COLLECTOR_LOCAL) local++;
        else if (h->source_flags & HOST_COLLECTOR_SNMP) snmp++;
        else other++;
        if (h->last_update > latest) latest = h->last_update;
    }
    guint imported = ctx->snmp_observation_count;
    gboolean snmp_seen[MAX_HOSTS] = { FALSE };
    guint snmp_hosts = 0;
    for (guint j = 0; j < imported; ++j) {
        guint index = ctx->snmp_observations[j].host_index;
        if (index < (guint)ctx->count && index < MAX_HOSTS && !snmp_seen[index]) {
            snmp_seen[index] = TRUE;
            ++snmp_hosts;
        }
    }
    pthread_mutex_unlock(&ctx->mutex);

    guint configured = 0, active = 0, sampled = 0;
    if (app->SnmpContext.initialized) {
        pthread_mutex_lock(&app->SnmpContext.lock);
        for (guint i = 0; i < MAX_SNMP_HOSTS; i++) {
            const SnmpHost *m = &app->SnmpMetric[i];
            if (!m->ip_address[0] || !m->oid_str[0]) continue;
            configured++;
            if (m->is_active) {
                active++;
                if (m->last_updated > 0 && m->last_value[0]) sampled++;
            }
        }
        pthread_mutex_unlock(&app->SnmpContext.lock);
    }
    char when[64] = "No host samples yet";
    struct tm tm_value;
    if (latest && localtime_r(&latest, &tm_value))
        strftime(when, sizeof(when), "%Y-%m-%d %H:%M:%S %Z", &tm_value);
    return g_strdup_printf(
        "========== Host Analysis Status ==========\n"
        "Collector          : %s\n"
        "Refresh interval   : %u seconds\n"
        "Host records       : %u local, %u SNMP-only, %u other\n"
        "Latest host sample : %s\n"
        "SNMP initialized   : %s\n"
        "SNMP OID targets   : %u configured, %u active, %u sampled\n"
        "SNMP host import   : %u hosts, %u observations\n"
        "==========================================\n",
        running ? "running" : "stopped", interval, local, snmp, other,
        when, app->SnmpContext.initialized ? "yes" : "no",
        configured, active, sampled, snmp_hosts, imported);
}
