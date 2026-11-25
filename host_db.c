// part of the aiterm project
// host_db.c
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

#include <string.h>
#include "host_db.h"
#include "host_analysis.h"
#include "utils.h"
#include "gui.h"
#include "update.h"

static void free_job(HostDBJob *job)
{
    if (job) { g_free(job->payload); g_free(job); }
}

static void queue_job(AppContext *app, HostDBJob *job)
{
    if (!app || !job || !app->hostdb.queue) { free_job(job); return; }
    HostDBContext *db = &app->hostdb;
    g_mutex_lock(&db->mutex);
    gboolean running = db->running;
    if (running) g_async_queue_push(db->queue, job);
    g_mutex_unlock(&db->mutex);
    if (!running) free_job(job);
}


/* All strings are copied before queueing; no collector lock is held by SQL. */
static void save_observation(AppContext *app, const char *hostname,
                             const char *collector, const char *metric,
                             time_t sampled_at, gboolean success,
                             struct json_object *obj)
{
    if (hostname && *hostname && sampled_at > 0) {
        HostDBJob *job = g_new0(HostDBJob, 1);
        job->type = DB_OBSERVATION;
        g_strlcpy(job->host.hostname, hostname, sizeof(job->host.hostname));
        g_strlcpy(job->collector, collector, sizeof(job->collector));
        g_strlcpy(job->metric, metric, sizeof(job->metric));
        job->sampled_at = sampled_at;
        job->success = success;
        job->payload = g_strdup(json_object_to_json_string_ext(obj, JSON_C_TO_STRING_PLAIN));
        queue_job(app, job);
    }
    json_object_put(obj);
}

void host_db_save_local(AppContext *app, const HostDetails *h)
{
    struct json_object *o = json_object_new_object();
    json_object_object_add(o, "description", json_object_new_string(h->description));
    json_object_object_add(o, "load_avg", json_object_new_double(h->load_avg));
#define ADD_INT(field) json_object_object_add(o, #field, json_object_new_int64(h->field))
    ADD_INT(memory_used); ADD_INT(memory_total); ADD_INT(swap_used); ADD_INT(swap_total);
    ADD_INT(disk_used); ADD_INT(disk_total); ADD_INT(uptime_seconds); ADD_INT(state);
    save_observation(app, h->hostname, "local", "linux", h->last_update, TRUE, o);
    o = json_object_new_object();
    ADD_INT(systemd_result);
    json_object_object_add(o, "error", json_object_new_string(h->systemd_error));
    if (h->systemd_result == HOST_COLLECT_OK) {
        ADD_INT(failed_services);
        struct json_object *units = json_object_new_array();
        for (guint i = 0; i < MIN(h->systemd_failed_stored, HOST_SYSTEMD_MAX_FAILED); i++)
            json_object_array_add(units, json_object_new_string(h->systemd_failed_units[i]));
        json_object_object_add(o, "failed_units", units);
    }
#undef ADD_INT
    save_observation(app, h->hostname, "systemd", "failed_services", h->systemd_collected_at,
                     h->systemd_result == HOST_COLLECT_OK, o);
}

void host_db_save_snmp(AppContext *app, const HostSnmpObservation *o)
{
    struct json_object *obj = json_object_new_object();
    json_object_object_add(obj, "oid", json_object_new_string(o->oid));
    json_object_object_add(obj, "label", json_object_new_string(o->label));
    json_object_object_add(obj, "value", json_object_new_string(o->value));
    save_observation(app, o->target, "snmp", o->oid, o->sampled_at, TRUE, obj);
}

void host_db_save_schedule(AppContext *app, const char *hostname, gboolean enabled)
{
    if (!hostname || !*hostname) return;
    HostDBJob *job = g_new0(HostDBJob, 1);
    job->type = DB_SCHEDULE; job->enabled = enabled;
    g_strlcpy(job->host.hostname, hostname, sizeof(job->host.hostname));
    queue_job(app, job);
}

void host_db_save_timing(AppContext *app, const HostDetails *host)
{
    if (!host || !host->hostname[0]) return;
    HostDBJob *job = g_new0(HostDBJob, 1);
    job->type = DB_TIMING; job->host = *host;
    queue_job(app, job);
}

void host_db_save_sample(AppContext *app, const HostDetails *host)
{
    if (!host || !host->hostname[0]) return;
    HostDBJob *job = g_new0(HostDBJob, 1);
    job->type = DB_SAMPLE; job->host = *host;
    queue_job(app, job);
}

void host_db_save_collection_result(AppContext *app, const HostDetails *host,
                                    gboolean success, guint duration_ms,
                                    const char *error_message)
{
    if (!host || !host->hostname[0]) return;
    HostDBJob *job = g_new0(HostDBJob, 1);
    job->type = DB_COLLECTION;
    job->host = *host;  /* Independent, immutable snapshot for DB thread. */
    job->success = success;
    job->duration_ms = duration_ms;
    g_strlcpy(job->error_message, error_message ? error_message : "",
              sizeof(job->error_message));
    queue_job(app, job);
}

static gboolean query(MYSQL *conn, const char *sql);
static gchar *esc(MYSQL *conn, const char *value);

/* Zeta-3: read-only history reports and bounded retention, serialized by the
 * existing dedicated Host DB worker. Never query MariaDB from GTK callbacks. */
void host_db_query_history(AppContext *app, const char *hostname, guint hours,
                           guint limit, HostDBReportCallback callback, gpointer data)
{
    if (!hostname || !*hostname) return;
    HostDBJob *job = g_new0(HostDBJob, 1);
    job->type = DB_HISTORY_QUERY;
    g_strlcpy(job->host.hostname, hostname, sizeof(job->host.hostname));
    job->hours = CLAMP(hours, 1, 24 * 90);
    job->limit = CLAMP(limit, 1, 200);
    job->report_callback = callback;
    job->callback_data = data;
    queue_job(app, job);
}

void host_db_queue_cleanup(AppContext *app)
{
    HostDBJob *job = g_new0(HostDBJob, 1);
    job->type = DB_CLEANUP;
    queue_job(app, job);
}

gchar *host_db_format_status(AppContext *app)
{
    if (!app || !app->hostdb.queue) return g_strdup("Host database not initialized.\n");
    HostDBContext *db = &app->hostdb;
    g_mutex_lock(&db->mutex);
    gchar *result = g_strdup_printf(
        "Host DB: %s\nRestoration: %s\nJobs processed: %" G_GUINT64_FORMAT
        "\nJobs failed: %" G_GUINT64_FORMAT "\nPending jobs: %d\n"
        "Persistence: SSH, local Linux, systemd, SNMP\n"
        "History retention: 30 days (successful), 90 days (failed)\n",
        db->connected ? "Connected" : "Disconnected",
        db->restore_complete ? "Complete" : "Pending",
        db->jobs_processed, db->jobs_failed, g_async_queue_length(db->queue));
    g_mutex_unlock(&db->mutex);
    return result;
}

typedef struct {
    AppContext *app;
    gchar *report;
    HostDBReportCallback callback;
    gpointer user_data;
} HostDBReport;

static gboolean report_on_main(gpointer data)
{
    HostDBReport *r = data;
    if (r->callback) r->callback(r->app, r->report, r->user_data);
    g_free(r->report);
    g_free(r);
    return G_SOURCE_REMOVE;
}

static gboolean history_query(AppContext *app, MYSQL *conn, const HostDBJob *job)
{
    gchar *name = esc(conn, job->host.hostname);
    gchar *sql = g_strdup_printf(
        "SELECT FROM_UNIXTIME(collected_at),success,duration_ms,load_avg,"
        "mem_total,mem_available,disk_total,disk_used,error_message "
        "FROM host_analysis_history WHERE hostname='%s' "
        "AND collected_at >= UNIX_TIMESTAMP() - %u "
        "ORDER BY collected_at DESC,id DESC LIMIT %u",
        name, job->hours * 3600, job->limit);
    g_free(name);
    gboolean ok = query(conn, sql);
    g_free(sql);
    if (!ok) return FALSE;
    MYSQL_RES *res = mysql_store_result(conn);
    if (!res) return FALSE;
    GString *out = g_string_new(NULL);
    g_string_append_printf(out, "History: %s (last %u hours, up to %u samples)\n\n",
                           job->host.hostname, job->hours, job->limit);
    MYSQL_ROW row;
    guint count = 0;
    while ((row = mysql_fetch_row(res))) {
        g_string_append_printf(out, "%s  %s  %s ms  load=%s  memory=%s/%s  disk=%s/%s%s%s\n",
            row[0] ? row[0] : "unknown", row[1] && atoi(row[1]) ? "OK" : "FAIL",
            row[2] ? row[2] : "?", row[3] ? row[3] : "?",
            row[5] ? row[5] : "?", row[4] ? row[4] : "?",
            row[7] ? row[7] : "?", row[6] ? row[6] : "?",
            row[8] && *row[8] ? "  error=" : "", row[8] ? row[8] : "");
        count++;
    }
    if (!count) g_string_append(out, "No SSH samples found.\n");
    mysql_free_result(res);
    name = esc(conn, job->host.hostname);
    sql = g_strdup_printf("SELECT FROM_UNIXTIME(collected_at),collector,metric,success,payload "
        "FROM host_analysis_observation_history WHERE hostname='%s' "
        "AND collected_at >= UNIX_TIMESTAMP() - %u ORDER BY collected_at DESC,collector,metric LIMIT %u",
        name, job->hours * 3600, job->limit);
    g_free(name);
    ok = query(conn, sql); g_free(sql);
    g_string_append(out, "\nLocal / systemd / SNMP history:\n");
    res = ok ? mysql_store_result(conn) : NULL;
    if (res) {
        count = 0;
        while ((row = mysql_fetch_row(res))) {
            g_string_append_printf(out, "%s %s/%s %s %s\n", row[0], row[1], row[2],
                atoi(row[3]) ? "OK" : "FAIL", row[4]);
            count++;
        }
        if (!count) g_string_append(out, "No observations found.\n");
        mysql_free_result(res);
    } else g_string_append(out, "Observation history query failed.\n");
    if (job->report_callback) {
        HostDBReport *r = g_new0(HostDBReport, 1);
        r->app = app; r->report = g_string_free(out, FALSE);
        r->callback = job->report_callback; r->user_data = job->callback_data;
        g_main_context_invoke(NULL, report_on_main, r);
    } else g_string_free(out, TRUE);
    return TRUE;
}

static gboolean cleanup_history(MYSQL *conn)
{
    /* Delete in bounded batches to avoid long-running transactions. */
    gboolean extra = query(conn, "DELETE FROM host_analysis_observation_history WHERE "
        "(success=1 AND collected_at < UNIX_TIMESTAMP() - 30*86400) OR "
        "(success=0 AND collected_at < UNIX_TIMESTAMP() - 90*86400) LIMIT 1000");
    return query(conn, "DELETE FROM host_analysis_history WHERE "
        "(success=1 AND collected_at < UNIX_TIMESTAMP() - 30*86400) OR "
        "(success=0 AND collected_at < UNIX_TIMESTAMP() - 90*86400) LIMIT 1000") && extra;
}

static gboolean query(MYSQL *conn, const char *sql)
{
    if (mysql_query(conn, sql) == 0) return TRUE;

    char *lt_pl   = g_strdup(global_app->ansi.lt_purple);
    char *cy      = g_strdup(global_app->ansi.cyan);
    char *yl      = g_strdup(global_app->ansi.yellow);
    char *gr      = g_strdup(global_app->ansi.green);
    char *red     = g_strdup(global_app->ansi.red);
    char *nml     = g_strdup(global_app->ansi.normal);

    DEBUG_PRINT("[ %sDEBUG%s ]: [%sHOSTDB%s] %sSQL error: %s%s%s\n", 
	lt_pl, nml, cy, nml, yl,
	red, mysql_error(conn), nml);

    g_free(lt_pl);
    g_free(cy);
    g_free(yl);
    g_free(gr);
    g_free(red);
    g_free(nml);

    return FALSE;
}

static gchar *esc(MYSQL *conn, const char *value)
{
    const char *v = value ? value : "";
    gsize n = strlen(v);
    gchar *buf = g_malloc(2*n+1);
    mysql_real_escape_string(conn, buf, v, n);
    return buf;
}

static gboolean schema(MYSQL *conn)
{
    const char *stmts[] = {
        "CREATE TABLE IF NOT EXISTS host_analysis_hosts ("
        "hostname VARCHAR(255) PRIMARY KEY, ssh_scheduled TINYINT(1) NOT NULL DEFAULT 0, "
        "ssh_interval_seconds INT UNSIGNED NOT NULL DEFAULT 300, "
        "ssh_last_duration_ms INT UNSIGNED NOT NULL DEFAULT 0, "
        "ssh_recent_max_ms INT UNSIGNED NOT NULL DEFAULT 0, "
        "ssh_success_count INT UNSIGNED NOT NULL DEFAULT 0, "
        "ssh_timeout_count INT UNSIGNED NOT NULL DEFAULT 0, "
        "updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP) ENGINE=InnoDB",
        "CREATE TABLE IF NOT EXISTS host_analysis_latest ("
        "hostname VARCHAR(255) NOT NULL, collector VARCHAR(24) NOT NULL, "
        "collected_at BIGINT NOT NULL, kernel VARCHAR(128), load_avg DOUBLE, "
        "uptime_seconds BIGINT, mem_total BIGINT, mem_available BIGINT, "
        "swap_total BIGINT, swap_free BIGINT, disk_total BIGINT, disk_used BIGINT, "
        "PRIMARY KEY(hostname,collector)) ENGINE=InnoDB",
        "CREATE TABLE IF NOT EXISTS host_analysis_history ("
        "id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY, "
        "hostname VARCHAR(255) NOT NULL, collector VARCHAR(24) NOT NULL, "
        "collected_at BIGINT NOT NULL, success TINYINT(1) NOT NULL, "
        "duration_ms INT UNSIGNED, error_message VARCHAR(255), "
        "kernel VARCHAR(128), load_avg DOUBLE, uptime_seconds BIGINT, "
        "mem_total BIGINT, mem_available BIGINT, swap_total BIGINT, swap_free BIGINT, "
        "disk_total BIGINT, disk_used BIGINT, "
        "KEY idx_host_time(hostname,collected_at)) ENGINE=InnoDB",
        "CREATE TABLE IF NOT EXISTS host_analysis_observation_latest ("
        "hostname VARCHAR(255) NOT NULL, collector VARCHAR(24) NOT NULL, "
        "metric VARCHAR(192) NOT NULL, collected_at BIGINT NOT NULL, payload LONGTEXT NOT NULL, "
        "PRIMARY KEY(hostname,collector,metric)) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4",
        "CREATE TABLE IF NOT EXISTS host_analysis_observation_history ("
        "hostname VARCHAR(255) NOT NULL, collector VARCHAR(24) NOT NULL, "
        "metric VARCHAR(192) NOT NULL, collected_at BIGINT NOT NULL, success TINYINT(1) NOT NULL, "
        "payload LONGTEXT NOT NULL, PRIMARY KEY(hostname,collector,metric,collected_at), "
        "KEY idx_observation_time(collected_at)) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4",
        NULL
    };
    for (int i=0; stmts[i]; i++) if (!query(conn, stmts[i])) return FALSE;
    /* Existing Zeta-1 databases have a minimal history table. Add telemetry
     * columns without dropping historical rows or changing its primary key.
     * ADD COLUMN IF NOT EXISTS is supported by MariaDB. */
    const char *columns[] = {
        "kernel VARCHAR(128)", "load_avg DOUBLE", "uptime_seconds BIGINT",
        "mem_total BIGINT", "mem_available BIGINT", "swap_total BIGINT",
        "swap_free BIGINT", "disk_total BIGINT", "disk_used BIGINT", NULL
    };
    for (int i = 0; columns[i]; i++) {
        gchar *sql = g_strdup_printf(
            "ALTER TABLE host_analysis_history ADD COLUMN IF NOT EXISTS %s", columns[i]);
        gboolean ok = query(conn, sql);
        g_free(sql);
        if (!ok) return FALSE;
    }
    return TRUE;
}

static gboolean restore_on_main(gpointer data)
{
    RestoreResult *r = data;
    AppContext *app = r->app;
    HostAnalysisContext *ctx = &app->host_analysis;

    char *lt_pl   = g_strdup(global_app->ansi.lt_purple);
    char *cy      = g_strdup(global_app->ansi.cyan);
    char *yl      = g_strdup(global_app->ansi.yellow);
    char *gr      = g_strdup(global_app->ansi.green);
    char *red     = g_strdup(global_app->ansi.red);
    char *nml     = g_strdup(global_app->ansi.normal);

    /* Schedules are only accessed on the main loop. Existing runtime changes win. */
    for (guint i=0; i<r->hosts->len; i++) {
        const char *name = g_ptr_array_index(r->hosts, i);
        gboolean present = FALSE;
        for (guint j=0; ctx->ssh_schedule_hosts && j<ctx->ssh_schedule_hosts->len; j++)
            if (!g_ascii_strcasecmp(name, g_ptr_array_index(ctx->ssh_schedule_hosts,j))) present=TRUE;
        if (!present) host_analysis_schedule_add(app, name);
    }
    pthread_mutex_lock(&ctx->mutex);
    for (guint i=0; i<r->timings->len; i++) {
        HostDetails *saved = g_ptr_array_index(r->timings,i);
        HostDetails *h = host_analysis_find_or_create_locked(ctx,saved->hostname);
        if (h) {
            h->ssh_recent_max_ms = MAX(h->ssh_recent_max_ms,saved->ssh_recent_max_ms);
            if (!h->ssh_success_count) h->ssh_last_duration_ms = saved->ssh_last_duration_ms;
            h->ssh_success_count = MAX(h->ssh_success_count,saved->ssh_success_count);
            h->ssh_timeout_count = MAX(h->ssh_timeout_count,saved->ssh_timeout_count);
        }
    }
    /* Restore cached SSH data only if no newer live sample exists. */
    for (guint i = 0; i < r->snapshots->len; i++) {
        HostDetails *saved = g_ptr_array_index(r->snapshots, i);
        HostDetails *h = host_analysis_find_or_create_locked(ctx, saved->hostname);
        if (!h || (h->remote_linux_valid &&
                   h->remote_linux_collected_at >= saved->remote_linux_collected_at))
            continue;
        h->source_flags |= HOST_COLLECTOR_SSH | HOST_COLLECTOR_DATABASE;
        if (!(h->source_flags & (HOST_COLLECTOR_LOCAL | HOST_COLLECTOR_SNMP)))
            h->source = HOST_SOURCE_SSH;
        h->remote_linux_valid = TRUE;
        h->remote_linux_result = HOST_COLLECT_OK;
        h->remote_linux_last_success = saved->remote_linux_collected_at;
        h->remote_linux_collected_at = saved->remote_linux_collected_at;
        g_strlcpy(h->remote_linux_kernel, saved->remote_linux_kernel,
                  sizeof(h->remote_linux_kernel));
        h->remote_linux_error[0] = '\0';
        h->remote_linux_load = saved->remote_linux_load;
        h->remote_linux_uptime = saved->remote_linux_uptime;
        h->remote_linux_mem_total = saved->remote_linux_mem_total;
        h->remote_linux_mem_available = saved->remote_linux_mem_available;
        h->remote_linux_swap_total = saved->remote_linux_swap_total;
        h->remote_linux_swap_free = saved->remote_linux_swap_free;
        h->remote_linux_disk_total = saved->remote_linux_disk_total;
        h->remote_linux_disk_used = saved->remote_linux_disk_used;
    }
    pthread_mutex_unlock(&ctx->mutex);
    g_mutex_lock(&app->hostdb.mutex);
    app->hostdb.restore_complete = TRUE;
    g_mutex_unlock(&app->hostdb.mutex);
    DEBUG_PRINT("[ %sDEBUG%s ]: [%sHOSTDB%s] %srestored [%s%u%s] scheduled hosts and [%s%u%s] timing profiles; cached SSH snapshots loaded.%s\n",
	lt_pl, nml, cy, nml, gr,
	red, r->hosts->len, gr,
	red, r->timings->len, gr, nml);

    g_ptr_array_free(r->hosts,TRUE); g_ptr_array_free(r->timings,TRUE);
    g_ptr_array_free(r->snapshots, TRUE); g_free(r);

    g_free(lt_pl);
    g_free(cy);
    g_free(yl);
    g_free(gr);
    g_free(red);
    g_free(nml);

    return G_SOURCE_REMOVE;
}

static void restore(AppContext *app, MYSQL *conn)
{
    RestoreResult *r = g_new0(RestoreResult,1);
    r->app=app;
    r->hosts=g_ptr_array_new_with_free_func(g_free);
    r->timings=g_ptr_array_new_with_free_func(g_free);
    r->snapshots=g_ptr_array_new_with_free_func(g_free);
    if (query(conn,"SELECT hostname,ssh_scheduled,ssh_last_duration_ms,ssh_recent_max_ms,ssh_success_count,ssh_timeout_count FROM host_analysis_hosts")) {
        MYSQL_RES *res=mysql_store_result(conn);
        if (res) {
            MYSQL_ROW row;
            while ((row=mysql_fetch_row(res))) {
                if (!row[0]) continue;
                HostDetails *h=g_new0(HostDetails,1);
                g_strlcpy(h->hostname,row[0],sizeof(h->hostname));
                if (row[1] && atoi(row[1])) g_ptr_array_add(r->hosts,g_strdup(row[0]));
                h->ssh_last_duration_ms=row[2]?(guint)g_ascii_strtoull(row[2],NULL,10):0;
                h->ssh_recent_max_ms=row[3]?(guint)g_ascii_strtoull(row[3],NULL,10):0;
                h->ssh_success_count=row[4]?(guint)g_ascii_strtoull(row[4],NULL,10):0;
                h->ssh_timeout_count=row[5]?(guint)g_ascii_strtoull(row[5],NULL,10):0;
                if (h->ssh_last_duration_ms || h->ssh_recent_max_ms || h->ssh_success_count || h->ssh_timeout_count)
                    g_ptr_array_add(r->timings,h);
                else g_free(h);
            }
            mysql_free_result(res);
        }
    }
    /* MariaDB connection is exclusively owned by this worker thread.
     * Copy rows before dispatching to the GTK main context. */
    if (query(conn, "SELECT hostname,collected_at,kernel,load_avg,uptime_seconds,"
                    "mem_total,mem_available,swap_total,swap_free,disk_total,disk_used "
                    "FROM host_analysis_latest WHERE collector='ssh'")) {
        MYSQL_RES *res = mysql_store_result(conn);
        if (res) {
            MYSQL_ROW row;
            while ((row = mysql_fetch_row(res))) {
                if (!row[0] || !row[1]) continue;
                HostDetails *h = g_new0(HostDetails, 1);
                g_strlcpy(h->hostname, row[0], sizeof(h->hostname));
                h->remote_linux_collected_at = (time_t)g_ascii_strtoll(row[1], NULL, 10);
                if (row[2]) g_strlcpy(h->remote_linux_kernel, row[2], sizeof(h->remote_linux_kernel));
                h->remote_linux_load = row[3] ? g_ascii_strtod(row[3], NULL) : 0;
                h->remote_linux_uptime = row[4] ? g_ascii_strtoll(row[4], NULL, 10) : 0;
                h->remote_linux_mem_total = row[5] ? g_ascii_strtoll(row[5], NULL, 10) : 0;
                h->remote_linux_mem_available = row[6] ? g_ascii_strtoll(row[6], NULL, 10) : 0;
                h->remote_linux_swap_total = row[7] ? g_ascii_strtoll(row[7], NULL, 10) : 0;
                h->remote_linux_swap_free = row[8] ? g_ascii_strtoll(row[8], NULL, 10) : 0;
                h->remote_linux_disk_total = row[9] ? g_ascii_strtoll(row[9], NULL, 10) : 0;
                h->remote_linux_disk_used = row[10] ? g_ascii_strtoll(row[10], NULL, 10) : 0;
                g_ptr_array_add(r->snapshots, h);
            }
            mysql_free_result(res);
        }
    }
    g_main_context_invoke(NULL,restore_on_main,r);
}

static gboolean process(AppContext *app, MYSQL *conn, const HostDBJob *job);

/* Zeta-2: append one event for each completed remote Linux collection.
 * A successful collection also updates latest in the SAME transaction.
 * Failures leave last-good measurements untouched. */
static gboolean save_collection(AppContext *app, MYSQL *conn, const HostDBJob *job)
{
    if (!query(conn, "START TRANSACTION")) return FALSE;
    gchar *inventory_name = esc(conn, job->host.hostname);
    gchar *inventory_sql = g_strdup_printf("INSERT INTO host_analysis_hosts(hostname) VALUES('%s') "
        "ON DUPLICATE KEY UPDATE hostname=hostname", inventory_name);
    gboolean ok = query(conn, inventory_sql);
    g_free(inventory_name); g_free(inventory_sql);
    if (ok && job->success) {
        HostDBJob latest = *job;
        latest.type = DB_SAMPLE;
        ok = process(app, conn, &latest);
    }
    if (ok) {
        const HostDetails *h = &job->host;
        gchar *name = esc(conn, h->hostname);
        gchar *error = esc(conn, job->error_message);
        gchar *kernel = esc(conn, job->success ? h->remote_linux_kernel : "");
        char load[G_ASCII_DTOSTR_BUF_SIZE];
        g_ascii_dtostr(load, sizeof(load), h->remote_linux_load);
        gchar *sql = g_strdup_printf(
            "INSERT INTO host_analysis_history "
            "(hostname,collector,collected_at,success,duration_ms,error_message,"
            "kernel,load_avg,uptime_seconds,mem_total,mem_available,swap_total,swap_free,disk_total,disk_used) "
            "VALUES('%s','ssh',%lld,%u,%u,'%s','%s',%s,%lld,%lld,%lld,%lld,%lld,%lld,%lld)",
            name, (long long)h->remote_linux_collected_at, job->success ? 1 : 0,
            job->duration_ms, error, kernel, job->success ? load : "NULL",
            job->success ? h->remote_linux_uptime : 0LL,
            job->success ? h->remote_linux_mem_total : 0LL,
            job->success ? h->remote_linux_mem_available : 0LL,
            job->success ? h->remote_linux_swap_total : 0LL,
            job->success ? h->remote_linux_swap_free : 0LL,
            job->success ? h->remote_linux_disk_total : 0LL,
            job->success ? h->remote_linux_disk_used : 0LL);
        ok = query(conn, sql);
        g_free(sql); g_free(name); g_free(error); g_free(kernel);
    }
    if (ok) ok = query(conn, "COMMIT");
    if (!ok) query(conn, "ROLLBACK");
    return ok;
}


static gboolean save_extra_observation(MYSQL *conn, const HostDBJob *job)
{
    gchar *name = esc(conn, job->host.hostname);
    gchar *collector = esc(conn, job->collector);
    gchar *metric = esc(conn, job->metric);
    gchar *payload = esc(conn, job->payload);
    gboolean ok = query(conn, "START TRANSACTION");
    if (ok) {
        gchar *sql = g_strdup_printf("INSERT INTO host_analysis_hosts(hostname) VALUES('%s') "
            "ON DUPLICATE KEY UPDATE hostname=hostname", name);
        ok = query(conn, sql); g_free(sql);
    }
    if (ok) {
        gchar *sql = g_strdup_printf("INSERT INTO host_analysis_observation_history "
            "(hostname,collector,metric,collected_at,success,payload) VALUES('%s','%s','%s',%lld,%u,'%s') "
            "ON DUPLICATE KEY UPDATE payload=VALUES(payload),success=VALUES(success)",
            name, collector, metric, (long long)job->sampled_at, job->success ? 1u : 0u, payload);
        ok = query(conn, sql); g_free(sql);
    }
    if (ok && job->success) {
        gchar *sql = g_strdup_printf("INSERT INTO host_analysis_observation_latest "
            "(hostname,collector,metric,collected_at,payload) VALUES('%s','%s','%s',%lld,'%s') "
            "ON DUPLICATE KEY UPDATE payload=IF(VALUES(collected_at)>=collected_at,VALUES(payload),payload), "
            "collected_at=GREATEST(collected_at,VALUES(collected_at))",
            name, collector, metric, (long long)job->sampled_at, payload);
        ok = query(conn, sql); g_free(sql);
    }
    if (ok) ok = query(conn, "COMMIT");
    if (!ok) query(conn, "ROLLBACK");
    g_free(name); g_free(collector); g_free(metric); g_free(payload);
    return ok;
}

static gboolean process(AppContext *app, MYSQL *conn, const HostDBJob *job)
{
    if (job->type==DB_OBSERVATION) return save_extra_observation(conn, job);
    if (job->type==DB_RESTORE) { restore(app,conn); return TRUE; }
    if (job->type==DB_COLLECTION) return save_collection(app, conn, job);
    if (job->type==DB_HISTORY_QUERY) return history_query(app, conn, job);
    if (job->type==DB_CLEANUP) return cleanup_history(conn);
    gchar *name=esc(conn,job->host.hostname);
    gchar *sql=NULL;
    switch (job->type) {
    case DB_SCHEDULE:
        sql=g_strdup_printf("INSERT INTO host_analysis_hosts(hostname,ssh_scheduled) VALUES('%s',%u) "
            "ON DUPLICATE KEY UPDATE ssh_scheduled=VALUES(ssh_scheduled)",name,job->enabled?1:0);
        break;
    case DB_TIMING:
        sql=g_strdup_printf("INSERT INTO host_analysis_hosts(hostname,ssh_last_duration_ms,ssh_recent_max_ms,ssh_success_count,ssh_timeout_count) "
            "VALUES('%s',%u,%u,%u,%u) ON DUPLICATE KEY UPDATE "
            "ssh_last_duration_ms=VALUES(ssh_last_duration_ms), "
            "ssh_recent_max_ms=GREATEST(ssh_recent_max_ms,VALUES(ssh_recent_max_ms)), "
            "ssh_success_count=GREATEST(ssh_success_count,VALUES(ssh_success_count)), "
            "ssh_timeout_count=GREATEST(ssh_timeout_count,VALUES(ssh_timeout_count))",
            name,job->host.ssh_last_duration_ms,job->host.ssh_recent_max_ms,
            job->host.ssh_success_count,job->host.ssh_timeout_count);
        break;
    case DB_SAMPLE: {
        const HostDetails *h=&job->host;
        gchar *kernel=esc(conn,h->remote_linux_kernel);
        char load[G_ASCII_DTOSTR_BUF_SIZE];
        g_ascii_dtostr(load, sizeof(load), h->remote_linux_load);
        sql=g_strdup_printf("INSERT INTO host_analysis_latest(hostname,collector,collected_at,kernel,load_avg,uptime_seconds,mem_total,mem_available,swap_total,swap_free,disk_total,disk_used) "
            "VALUES('%s','ssh',%lld,'%s',%s,%lld,%lld,%lld,%lld,%lld,%lld,%lld) "
            "ON DUPLICATE KEY UPDATE kernel=IF(VALUES(collected_at)>=collected_at,VALUES(kernel),kernel), "
            "load_avg=IF(VALUES(collected_at)>=collected_at,VALUES(load_avg),load_avg), "
            "uptime_seconds=IF(VALUES(collected_at)>=collected_at,VALUES(uptime_seconds),uptime_seconds), "
            "mem_total=IF(VALUES(collected_at)>=collected_at,VALUES(mem_total),mem_total), "
            "mem_available=IF(VALUES(collected_at)>=collected_at,VALUES(mem_available),mem_available), "
            "swap_total=IF(VALUES(collected_at)>=collected_at,VALUES(swap_total),swap_total), "
            "swap_free=IF(VALUES(collected_at)>=collected_at,VALUES(swap_free),swap_free), "
            "disk_total=IF(VALUES(collected_at)>=collected_at,VALUES(disk_total),disk_total), "
            "disk_used=IF(VALUES(collected_at)>=collected_at,VALUES(disk_used),disk_used), "
            "collected_at=GREATEST(collected_at,VALUES(collected_at))",
            name,(long long)h->remote_linux_collected_at,kernel,load,
            h->remote_linux_uptime,h->remote_linux_mem_total,h->remote_linux_mem_available,
            h->remote_linux_swap_total,h->remote_linux_swap_free,
            h->remote_linux_disk_total,h->remote_linux_disk_used);
        g_free(kernel); break;
    }
    default: break;
    }
    gboolean ok=sql && query(conn,sql);
    g_free(sql); g_free(name);
    return ok;
}

static gpointer worker(gpointer data)
{
    AppContext *app=data;
    HostDBContext *db=&app->hostdb;
    MYSQL *conn=mysql_init(NULL);

    char *lt_pl   = g_strdup(global_app->ansi.lt_purple);
    char *cy      = g_strdup(global_app->ansi.cyan);
    char *yl      = g_strdup(global_app->ansi.yellow);
    char *gr      = g_strdup(global_app->ansi.green);
    char *red     = g_strdup(global_app->ansi.red);
    char *nml     = g_strdup(global_app->ansi.normal);

    if (conn) {
        guint timeout=5;
        mysql_options(conn,MYSQL_OPT_CONNECT_TIMEOUT,&timeout);
        if (!mysql_real_connect(conn,app->database.db_host,app->database.db_user,
                app->database.db_pass,app->database.db_name,0,NULL,0)) {
            DEBUG_PRINT("[ %sDEBUG%s ]: [%sHOSTDB%s] %sconnection failed: [%s%s%s]%s\n",
		lt_pl, nml, cy, nml, yl,
		red, mysql_error(conn), yl, nml);
            mysql_close(conn); conn=NULL;
        }
    }
    if (conn && !schema(conn)) { mysql_close(conn); conn=NULL; }
    g_mutex_lock(&db->mutex); db->connection=conn; db->connected=conn!=NULL; g_mutex_unlock(&db->mutex);
    if (conn) { 
	DEBUG_PRINT("[ %sDEBUG%s ]: [%sHOSTDB%s] %sdedicated MariaDB connection ready%s\n",
		lt_pl, nml, cy, nml, gr, nml); 
	restore(app,conn);
        cleanup_history(conn);
    } else {
        DEBUG_PRINT("[ %sDEBUG%s ]: [%sHOSTDB%s] %sUnavailable%s; %slive Host Analysis remains active%s, %spersistence disabled.%s\n",
	    lt_pl, nml, cy, nml, red, nml, gr, nml, yl, nml);
    }
    for (;;) {
        HostDBJob *job=g_async_queue_pop(db->queue);
        if (job->type==DB_QUIT) { g_free(job); break; }
        gboolean ok=FALSE;
        if (conn) {
            if (mysql_ping(conn)!=0) {
                DEBUG_PRINT("[ %sDEBUG%s ]: [%sHOSTDB%s] %slost MariaDB connection: %s%s%s\n",
			lt_pl, nml, cy, nml, yl, 
			red, mysql_error(conn), nml);
            } else ok=process(app,conn,job);
        }
        g_mutex_lock(&db->mutex);
        db->jobs_processed++;
        if (!ok) db->jobs_failed++;

        guint64 processed = db->jobs_processed;
        g_mutex_unlock(&db->mutex);
        if (conn && processed % 288 == 0) cleanup_history(conn);
        free_job(job);
    }
    if (conn) mysql_close(conn);
    g_mutex_lock(&db->mutex); db->connection=NULL; db->connected=FALSE; g_mutex_unlock(&db->mutex);

    g_free(lt_pl);
    g_free(cy);
    g_free(yl);
    g_free(gr);
    g_free(red);
    g_free(nml);

    return NULL;
}

gboolean host_db_init(AppContext *app)
{
    if (!app) return FALSE;
    HostDBContext *db=&app->hostdb;
    g_mutex_init(&db->mutex);
    db->queue=g_async_queue_new();
    db->running=TRUE;
    db->thread=g_thread_new("host-db",worker,app);
    if (!db->thread) { db->running=FALSE; g_async_queue_unref(db->queue); db->queue=NULL; g_mutex_clear(&db->mutex); return FALSE; }
    return TRUE;
}

void host_db_shutdown(AppContext *app)
{
    if (!app || !app->hostdb.queue) return;

    char *lt_pl   = g_strdup(global_app->ansi.lt_purple);
    char *cy      = g_strdup(global_app->ansi.cyan);
    char *yl      = g_strdup(global_app->ansi.yellow);
    char *gr      = g_strdup(global_app->ansi.green);
    char *red     = g_strdup(global_app->ansi.red);
    char *nml     = g_strdup(global_app->ansi.normal);

    HostDBContext *db=&app->hostdb;
    g_mutex_lock(&db->mutex);
    db->running=FALSE;
    HostDBJob *quit=g_new0(HostDBJob,1); quit->type=DB_QUIT;
    g_async_queue_push(db->queue,quit);
    g_mutex_unlock(&db->mutex);
    g_thread_join(db->thread);
    DEBUG_PRINT("[ %sDEBUG%s ]: [%sHOSTDB%s] %sstopped, processed=[%s%ld%s] failed=[%s%ld%s]%s\n",
	lt_pl, nml, cy, nml, yl,
        red, db->jobs_processed, yl,
	red, db->jobs_failed, yl, nml);

    g_async_queue_unref(db->queue); db->queue=NULL;
    g_mutex_clear(&db->mutex);

    g_free(lt_pl);
    g_free(cy);
    g_free(yl);
    g_free(gr);
    g_free(red);
    g_free(nml);

}
