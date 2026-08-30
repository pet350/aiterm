// part of aiterm project
// snmp_manager.c
// Function for handling SNMP monitoring
// By: Peter Talbott
// Assisted by: Gemini
// August 2026

#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <pthread.h>
#include <sys/prctl.h>
#include <mariadb/mysql.h>
#include <net-snmp/net-snmp-config.h>
#include <net-snmp/net-snmp-includes.h>
#include <time.h>
#include <errno.h>

#include "gui.h"
#include "utils.h"
#include "snmp_manager.h"
#include "tee_handler.h"

// Initialize SNMP variables in AppContext
void init_snmp_subsystem(AppContext *app) {
    if (!app) return;

    // 1. Initialize Master Context
    pthread_mutex_init(&app->SnmpContext.lock, NULL);
    pthread_cond_init(&app->SnmpContext.poller_cond, NULL);
    app->SnmpContext.initialized = TRUE;
    DEBUG_PRINT("[DEBUG]: [SNMP Initialize] pthread mutex/condition initialized\n");
    app->SnmpContext.metrics = NULL;
    app->SnmpContext.poller_thread = 0;
    app->SnmpContext.loop_running = FALSE;
    app->SnmpContext.force_gemini_feed = FALSE;
    app->SnmpContext.poll_interval_sec = 10;
    app->SnmpContext.total_targets = 0;
    app->SnmpContext.active_alerts = 0;
    app->SnmpContext.payload = NULL;

    // IF not enabled or initialized specify it as FALSE
    if (!app->SnmpContext.enable_gemini_feed) {
        app->SnmpContext.enable_gemini_feed = FALSE;
    }

    // 2. Initialize Static Host Array
    for (int i = 0; i < MAX_SNMP_HOSTS; i++) {
        memset(&app->SnmpMetric[i], 0, sizeof(SnmpHost));
        app->SnmpMetric[i].is_active = FALSE;
        app->SnmpMetric[i].alert_flag = FALSE;
        app->SnmpMetric[i].last_updated = 0;
        
        // Default fallback community string
        snprintf(app->SnmpMetric[i].community, sizeof(app->SnmpMetric[i].community), "public");
    }
    DEBUG_PRINT("[DEBUG]: [SNMP Initialize] Completed\n");
}

gboolean snmp_load_targets_from_db(AppContext *app) {
    if (!app || !app->database.global_db_conn) return FALSE;

    pthread_mutex_lock(&app->access.db_mutex);

    // Added 'id' to query
    const char *query = "SELECT id, label, ip_address, community, oid_str, is_active "
                        "FROM snmp_targets LIMIT 128;";
    
    if (mysql_query(app->database.global_db_conn, query)) {
        DEBUG_PRINT("[DEBUG]: [SNMP] DB query failed: %s\n", mysql_error(app->database.global_db_conn));
        pthread_mutex_unlock(&app->access.db_mutex);
        return FALSE;
    }

    MYSQL_RES *res = mysql_store_result(app->database.global_db_conn);
    if (!res) {
        pthread_mutex_unlock(&app->access.db_mutex);
        return FALSE;
    }

    pthread_mutex_lock(&app->SnmpContext.lock);
    
    MYSQL_ROW row;
    int count = 0;
    while ((row = mysql_fetch_row(res)) && count < MAX_SNMP_HOSTS) {
        app->SnmpMetric[count].id = row[0] ? atoi(row[0]) : 0;
        snprintf(app->SnmpMetric[count].label, sizeof(app->SnmpMetric[count].label), "%s", row[1] ? row[1] : "");
        snprintf(app->SnmpMetric[count].ip_address, sizeof(app->SnmpMetric[count].ip_address), "%s", row[2] ? row[2] : "");
        snprintf(app->SnmpMetric[count].community, sizeof(app->SnmpMetric[count].community), "%s", row[3] ? row[3] : "public");
        snprintf(app->SnmpMetric[count].oid_str, sizeof(app->SnmpMetric[count].oid_str), "%s", row[4] ? row[4] : "");
        app->SnmpMetric[count].is_active = row[5] ? atoi(row[5]) : 1;
        count++;
    }

    app->SnmpContext.total_targets = count;

    pthread_mutex_unlock(&app->SnmpContext.lock);
    mysql_free_result(res);
    pthread_mutex_unlock(&app->access.db_mutex);

    DEBUG_PRINT("[DEBUG]: [SNMP] Loaded %d target(s) from database.\n", count);
    return TRUE;
}

gboolean update_snmp_ticker_payload_wrapper(gpointer data) {
    AppContext *app = (AppContext *)data;
    if (!app || !app->gui.snmp_ticker_label || !app->sys.snmp_ticker_enabled) {
        return FALSE;
    }

    /* Do not interrupt a ticker pass with a fresh SNMP poll.  The poller may
     * run more often than the ticker can traverse the full payload, so the
     * completed flag acts as a simple hand-off between the poller and the
     * scrolling display. */
    if (!app->aiterm_runtime.ticker_completed) {
        DEBUG_PRINT("[DEBUG]: [SNMP Ticker] Current payload still scrolling; deferring poll update.\n");
        return FALSE;
    }

    /*
     * Build the ticker from EVERY populated SNMP array entry, not just the
     * value returned by one OID.  Include the target label, address, OID,
     * latest value, active state, alert state and last-update timestamp.
     * This makes the ticker a moving view of the complete SNMP array.
     */
    GString *summary = g_string_new("SNMP: No configured targets");
    guint populated = 0;
    guint active = 0;

    pthread_mutex_lock(&app->SnmpContext.lock);

    g_string_truncate(summary, 0);

    for (guint i = 0; i < app->SnmpContext.total_targets && i < MAX_SNMP_HOSTS; i++) {
        SnmpHost *host = &app->SnmpMetric[i];

        /* A database-loaded target is considered populated when it has an ID,
         * address, label, OID, or an existing value. */
        if (host->id <= 0 && !host->label[0] && !host->ip_address[0] && !host->oid_str[0]) {
            continue;
        }

        /* The ticker is a live view of enabled SNMP targets only.
         * Disabled targets remain in the array/configuration but are skipped
         * completely so they do not consume ticker space. */
        if (!host->is_active) {
            continue;
        }

        const char *label = host->label[0] ? host->label : "(unnamed)";
        const char *ip = host->ip_address[0] ? host->ip_address : "(no address)";
        const char *oid = host->oid_str[0] ? host->oid_str : "(no OID)";
        const char *value = host->last_value[0] ? host->last_value : "PENDING";
        const char *state = host->is_active ? "ON" : "OFF";
        const char *alert = host->alert_flag ? "ALERT" : "OK";

        if (populated > 0) {
            g_string_append(summary, "     ||     ");
        }

        if (host->last_updated > 0) {
            char timestamp[32];
            struct tm tm_buf;
            localtime_r(&host->last_updated, &tm_buf);
            strftime(timestamp, sizeof(timestamp), "%H:%M:%S", &tm_buf);

            g_string_append_printf(summary,
                "[%u] %s | IP=%s | OID=%s | VALUE=%s | STATE=%s | %s | UPDATED=%s",
                i + 1, label, ip, oid, value, state, alert, timestamp);
        } else {
            g_string_append_printf(summary,
                "[%u] %s | IP=%s | OID=%s | VALUE=%s | STATE=%s | %s | UPDATED=NEVER",
                i + 1, label, ip, oid, value, state, alert);
        }

        populated++;
        if (host->is_active) active++;
    }

    pthread_mutex_unlock(&app->SnmpContext.lock);

    if (populated == 0) {
        g_string_assign(summary, "SNMP: No configured targets");
    } else {
        /* Prefix the complete array payload with useful target counts. */
        GString *full = g_string_new(NULL);
        g_string_append_printf(full,
            "SNMP: %u Targets | %u Active     ||     %s",
            populated, active, summary->str);
        g_string_free(summary, TRUE);
        summary = full;
    }

    update_snmp_ticker_payload(app, summary->str);

    pthread_mutex_lock(&app->SnmpContext.lock);
    g_free(app->SnmpContext.payload);
    app->SnmpContext.payload = g_strdup(summary->str);
    pthread_mutex_unlock(&app->SnmpContext.lock);

    DEBUG_PRINT("[DEBUG]: [SNMP Ticker] Updated complete array: %s\n", summary->str);
    g_string_free(summary, TRUE);

    return FALSE;
}

void dump_raw_snmp_payload_to_ai(AppContext *app) {
    if (!app) return;

    /* Copy the payload while holding the same mutex used by the writer. */
    pthread_mutex_lock(&app->SnmpContext.lock);
    char *payload_copy = app->SnmpContext.payload
        ? g_strdup(app->SnmpContext.payload)
        : NULL;
    pthread_mutex_unlock(&app->SnmpContext.lock);

    if (!payload_copy) {
        DEBUG_PRINT("[DEBUG]: [Dump SNMP] No Payload to dump!\n");
        return;
    }
    DEBUG_PRINT("[DEBUG]: [Dump SNMP] Raw Payload: %s\n", payload_copy);

    // Dispatch the insertion to the main GUI thread
    // Assuming 'append_ai_text' is the primary way to inject into the gemini_view
    // We pass the payload_copy to a callback that frees it after usage
    g_idle_add((GSourceFunc)append_raw_payload_idle, (gpointer)payload_copy);
}

gboolean append_raw_payload_idle(gpointer data) {
    char *text = (char *)data;
    // Assuming 'app' is accessible globally or passed in a custom struct
    // Ensure you target the correct pane if you have multiple
    append_ai_text(global_app, text, "LOG_DUMP");
    g_free(text);
    return FALSE;
}


char *snmp_format_telemetry_payload_old(AppContext *app) {
    if (!app || !app->SnmpContext.enable_gemini_feed) return NULL;

    GString *xml = g_string_new("<snmp_telemetry>\n");
    pthread_mutex_lock(&app->SnmpContext.lock);

    for (guint i = 0; i < app->SnmpContext.total_targets; i++) {
        if (!app->SnmpMetric[i].is_active) continue;

        g_string_append_printf(xml, 
            "  <target label=\"%s\" ip=\"%s\">\n"
            "    <oid>%s</oid>\n"
            "    <value>%s</value>\n"
            "  </target>\n",
            app->SnmpMetric[i].label,
            app->SnmpMetric[i].ip_address,
            app->SnmpMetric[i].oid_str,
            app->SnmpMetric[i].last_value[0] ? app->SnmpMetric[i].last_value : "PENDING"
        );
    }

    pthread_mutex_unlock(&app->SnmpContext.lock);
    g_string_append(xml, "</snmp_telemetry>\n");

    return g_string_free(xml, FALSE); // Returns dynamically allocated string
}

gboolean update_ticker_idle_cb(gpointer data) {
    AppContext *app = (AppContext *)data;
    if (app && app->gui.status_label) {
        gtk_label_set_text(GTK_LABEL(app->gui.status_label), "SNMP: Idle");
    }
    return FALSE;
}

gboolean update_ticker_polling_cb(gpointer data) {
    AppContext *app = (AppContext *)data;
    if (app && app->gui.status_label) {
        gtk_label_set_text(GTK_LABEL(app->gui.status_label), "SNMP: Busy");
    }
    return FALSE;
}

void snmp_poll_all_targets(AppContext *app) {
    if (!app) return;

    g_idle_add(update_ticker_polling_cb, app);

    /*
     * IMPORTANT: Never hold SnmpContext.lock while doing network I/O.
     * Net-SNMP's synchronous calls may block for several seconds on a bad
     * target.  The GTK thread also uses this mutex to build the ticker, so
     * holding it across snmp_synch_response() can freeze the entire UI.
     *
     * We snapshot one target's configuration, release the lock, perform the
     * network transaction, then briefly reacquire the lock only to publish
     * the result.
     */
    for (guint i = 0; i < MAX_SNMP_HOSTS; i++) {
        char ip_address[sizeof(app->SnmpMetric[i].ip_address)];
        char community[sizeof(app->SnmpMetric[i].community)];
        char oid_str[sizeof(app->SnmpMetric[i].oid_str)];
        gboolean is_active;

        pthread_mutex_lock(&app->SnmpContext.lock);
        if (i >= app->SnmpContext.total_targets) {
            pthread_mutex_unlock(&app->SnmpContext.lock);
            break;
        }

        is_active = app->SnmpMetric[i].is_active;
        snprintf(ip_address, sizeof(ip_address), "%s", app->SnmpMetric[i].ip_address);
        snprintf(community, sizeof(community), "%s", app->SnmpMetric[i].community);
        snprintf(oid_str, sizeof(oid_str), "%s", app->SnmpMetric[i].oid_str);
        pthread_mutex_unlock(&app->SnmpContext.lock);

        if (!is_active) continue;

        char result[sizeof(app->SnmpMetric[i].last_value)];
        time_t updated = 0;
        result[0] = '\0';

        netsnmp_session session, *ss = NULL;
        netsnmp_pdu *pdu = NULL, *response = NULL;
        oid name[MAX_OID_LEN];
        size_t name_length = MAX_OID_LEN;

        snmp_sess_init(&session);
        session.peername = ip_address;
        session.version = SNMP_VERSION_2c;
        session.community = (u_char *)community;
        session.community_len = strlen(community);
        // Keep an unresponsive device from monopolizing the poller. These
        // values affect only the worker thread, never the GTK event loop.
        session.timeout = 1500000L;  // 1.5 seconds
        session.retries = 1;

        DEBUG_PRINT("[DEBUG]: [SNMP] Polling target %u %s OID=%s\n", i + 1, ip_address, oid_str);

        ss = snmp_open(&session);
        if (!ss) {
            snprintf(result, sizeof(result), "SNMP OPEN ERROR");
        } else if (!read_objid(oid_str, name, &name_length)) {
            snprintf(result, sizeof(result), "INVALID OID");
        } else {
            pdu = snmp_pdu_create(SNMP_MSG_GET);
            if (!pdu) {
                snprintf(result, sizeof(result), "PDU ALLOCATION ERROR");
            } else {
                snmp_add_null_var(pdu, name, name_length);
                int status = snmp_synch_response(ss, pdu, &response);
                if (status == STAT_SUCCESS && response && response->errstat == SNMP_ERR_NOERROR) {
                    for (netsnmp_variable_list *vars = response->variables; vars; vars = vars->next_variable) {
                        snprint_value(result, sizeof(result), vars->name, vars->name_length, vars);
                        updated = time(NULL);
                        break;
                    }
                    if (!result[0]) {
                        snprintf(result, sizeof(result), "NO VALUE");
                    }
                } else if (status == STAT_TIMEOUT) {
                    snprintf(result, sizeof(result), "TIMEOUT");
                } else {
                    snprintf(result, sizeof(result), "SNMP ERROR");
                }
            }
        }

        if (response) snmp_free_pdu(response);
        if (ss) snmp_close(ss);

        pthread_mutex_lock(&app->SnmpContext.lock);
        /* The target table can be reloaded while a request is in flight.
         * Only publish into the same array slot if it still represents the
         * same target ID/address/OID snapshot.  Otherwise discard the stale
         * result rather than corrupting a newly loaded configuration. */
        if (i < app->SnmpContext.total_targets &&
            app->SnmpMetric[i].is_active &&
            strcmp(app->SnmpMetric[i].ip_address, ip_address) == 0 &&
            strcmp(app->SnmpMetric[i].oid_str, oid_str) == 0) {
            snprintf(app->SnmpMetric[i].last_value,
                     sizeof(app->SnmpMetric[i].last_value), "%s", result);
            if (updated > 0) app->SnmpMetric[i].last_updated = updated;
        }
        pthread_mutex_unlock(&app->SnmpContext.lock);
    }

    /* The worker thread must never touch GTK directly.  Schedule the ticker
     * update on the GTK main context after the poll has completed. */
    g_idle_add(update_snmp_ticker_payload_wrapper, app);
    g_idle_add(update_ticker_idle_cb, app);
}

void snmp_force_poll(AppContext *app) {
    if (app) snmp_poll_all_targets(app);
}

char *snmp_format_telemetry_payload(AppContext *app) {
    if (!app) return NULL;

    GString *xml = g_string_new("<snmp_telemetry>\n");
    pthread_mutex_lock(&app->SnmpContext.lock);

    for (guint i = 0; i < app->SnmpContext.total_targets; i++) {
        if (!app->SnmpMetric[i].is_active) continue;

        g_string_append_printf(xml, 
            "  <target label=\"%s\" ip=\"%s\">\n"
            "    <oid>%s</oid>\n"
            "    <value>%s</value>\n"
            "  </target>\n",
            app->SnmpMetric[i].label,
            app->SnmpMetric[i].ip_address,
            app->SnmpMetric[i].oid_str,
            app->SnmpMetric[i].last_value[0] ? app->SnmpMetric[i].last_value : "PENDING"
        );
    }

    pthread_mutex_unlock(&app->SnmpContext.lock);
    g_string_append(xml, "</snmp_telemetry>\n");

    return g_string_free(xml, FALSE);
}

void *snmp_poller_worker(void *data) {
    AppContext *app = (AppContext *)data;
    if (!app) return NULL;
    SET_THREAD_NAME("aiterm-snmp");
    while (TRUE) {
        // 1. Check if we should stop
        pthread_mutex_lock(&app->SnmpContext.lock);
        if (!app->SnmpContext.loop_running) {
            pthread_mutex_unlock(&app->SnmpContext.lock);
            break;
        }
        
        // 2. Get poll interval
        int interval = app->SnmpContext.poll_interval_sec;
        if (interval <= 0) interval = 10;
        pthread_mutex_unlock(&app->SnmpContext.lock);
        
        // 3. Do polling work (outside the lock)
        snmp_poll_all_targets(app);
      
        // 4. Database updates
        for (guint i = 0; i < MAX_SNMP_HOSTS; i++) {
            int target_id;
            char last_value[256];

            pthread_mutex_lock(&app->SnmpContext.lock);
            if (i >= app->SnmpContext.total_targets) {
                pthread_mutex_unlock(&app->SnmpContext.lock);
                break;
            }
            target_id = app->SnmpMetric[i].id;
            snprintf(last_value, sizeof(last_value), "%s", app->SnmpMetric[i].last_value);
            pthread_mutex_unlock(&app->SnmpContext.lock);

            // Update database if connected
            if (app->database.global_db_conn && target_id > 0) {
                pthread_mutex_lock(&app->access.db_mutex);
                char query[512];
                snprintf(query, sizeof(query),
                         "UPDATE snmp_targets SET last_value = '%s' WHERE id = %d;",
                         last_value, target_id);
                mysql_query(app->database.global_db_conn, query);
                pthread_mutex_unlock(&app->access.db_mutex);
            }
        }

        pthread_mutex_lock(&app->SnmpContext.lock);
        interval = app->SnmpContext.poll_interval_sec;
        pthread_mutex_unlock(&app->SnmpContext.lock);

        // Track last time Gemini was fed SNMP telemetry
        static time_t last_gemini_flush = 0;
        time_t now = time(NULL);

        // Set desired flush interval (e.g., 300 seconds = 5 minutes)
        int gemini_flush_interval = 300; 

        if (app->SnmpContext.enable_gemini_feed && (now - last_gemini_flush >= gemini_flush_interval)) {
            last_gemini_flush = now;
            DEBUG_PRINT("[DEBUG]: [SNMP Poller] Flushing payload to Gemini.\n");
            snmp_flush_to_gemini(app);
        }

        gboolean force_feed = FALSE;
        pthread_mutex_lock(&app->SnmpContext.lock);
        force_feed = app->SnmpContext.force_gemini_feed;
        if (force_feed) app->SnmpContext.force_gemini_feed = FALSE;
        pthread_mutex_unlock(&app->SnmpContext.lock);

        if (force_feed) {
            last_gemini_flush = now;
            DEBUG_PRINT("[DEBUG]: [SNMP Poller] Forced Flushing payload to Gemini.\n");
            snmp_force_poll(app);
            snmp_flush_to_gemini(app);
        }

        // 5. Wait for next poll interval (or shutdown signal)
        struct timespec wake_time;
        clock_gettime(CLOCK_REALTIME, &wake_time);
        wake_time.tv_sec += interval;

        pthread_mutex_lock(&app->SnmpContext.lock);
        if (app->SnmpContext.loop_running) {
            pthread_cond_timedwait(&app->SnmpContext.poller_cond,
                                   &app->SnmpContext.lock,
                                   &wake_time);
        }
        pthread_mutex_unlock(&app->SnmpContext.lock);
    }
    
    return NULL;
}


void snmp_start_poller(AppContext *app) {
    if (!app || !app->SnmpContext.initialized) return;

    pthread_mutex_lock(&app->SnmpContext.lock);
    if (app->SnmpContext.loop_running) {
        pthread_mutex_unlock(&app->SnmpContext.lock);
        return;
    }
    app->SnmpContext.loop_running = TRUE;
    pthread_mutex_unlock(&app->SnmpContext.lock);

    if (pthread_create(&app->SnmpContext.poller_thread, NULL, snmp_poller_worker, app) != 0) {
        pthread_mutex_lock(&app->SnmpContext.lock);
        app->SnmpContext.loop_running = FALSE;
        app->SnmpContext.poller_thread = 0;
        pthread_mutex_unlock(&app->SnmpContext.lock);
        DEBUG_PRINT("[ERROR]: [SNMP] Failed to spawn poller thread.\n");
        return;
    }
    DEBUG_PRINT("[DEBUG]: [SNMP] Poller thread spawned successfully.\n");
}

void snmp_stop_poller(AppContext *app) {
    if (!app || !app->SnmpContext.initialized) return;

    pthread_mutex_lock(&app->SnmpContext.lock);
    gboolean was_running = app->SnmpContext.loop_running;
    app->SnmpContext.loop_running = FALSE;
    pthread_cond_broadcast(&app->SnmpContext.poller_cond);
    pthread_t thread = app->SnmpContext.poller_thread;
    app->SnmpContext.poller_thread = 0;
    pthread_mutex_unlock(&app->SnmpContext.lock);

    if (was_running && thread != 0) {
        DEBUG_PRINT("[DEBUG]: [SNMP] Joining poller thread.\n");
        pthread_join(thread, NULL);
    }

    snmp_shutdown("aiterm");
    pthread_cond_destroy(&app->SnmpContext.poller_cond);
    pthread_mutex_destroy(&app->SnmpContext.lock);
    app->SnmpContext.initialized = FALSE;
    g_free(app->SnmpContext.payload);
    app->SnmpContext.payload = NULL;
}
