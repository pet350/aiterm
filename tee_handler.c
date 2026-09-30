// part of aiterm project
// tee_handler.c
// Logic for capturing terminal streams and backgrounding AI analysis
// By: Peter Talbott
// Assisted by: Gemini
// May 2026

#include <string.h>
#include <stdlib.h>
#include "tee_handler.h"
#include "update.h"
#include "gemini.h"
#include "openai.h"
#include "ai_provider.h"
#include "utils.h"
#include "session_manager.h"
#include "noisefilter.h"
#include "snmp_manager.h"

// --- FORWARD DECLARATIONS (Private callbacks for 0.8.3) ---
static gboolean update_tee_ui(gpointer data);
static gpointer tee_ai_thread_func(gpointer data);
static gboolean reset_ai_status_idle(gpointer data) {
    AppContext *app = (AppContext *)data;
    if (app) update_status_label(app, "Ready");
    return FALSE;
}

static gboolean snmp_status_idle(gpointer data) {
    AppContext *app = (AppContext *)data;
    if (app) update_status_label(app, "Analyzing SNMP Telemetry (Background)...");
    return FALSE;
}


void tee_handler_init(AppContext *app) {
    if (!app) return;

    char *lt_pl   = g_strdup(app->ansi.lt_purple);
    char *cy      = g_strdup(app->ansi.cyan);
    char *yl      = g_strdup(app->ansi.yellow);
    char *gr      = g_strdup(app->ansi.green);
    char *red     = g_strdup(app->ansi.red);
    char *nml     = g_strdup(app->ansi.normal);

    app->aiterm_runtime.tee_accumulator = g_string_new("");
    g_mutex_init(&app->access.buffer_mutex);
    DEBUG_PRINT("[ %sDEBUG%s ]: [%sTee Handler%s] %sinitialized.%s\n",
	lt_pl, nml, cy, nml, gr, nml);

    g_free(cy);
    g_free(yl);
    g_free(gr);
    g_free(red);
    g_free(nml);

}

// ATOMIC SNAPSHOT (The 0.8.2 fix):
// Grabs text and clears the buffer in one locked operation
// to prevent splicing and duplication bugs.
/*
 * Return a snapshot of the pending Tee accumulator without consuming it.
 * This is used when a human asks the AI a question after terminal output has
 * arrived but before the timed Tee flush has persisted/forwarded it.
 */
char* tee_peek_for_ai(AppContext *app) {
    if (!app || !app->aiterm_runtime.tee_accumulator) return NULL;

    char *snapshot = NULL;
    g_mutex_lock(&app->access.buffer_mutex);

    if (app->aiterm_runtime.tee_accumulator->len > 0) {
        snapshot = g_strdup(app->aiterm_runtime.tee_accumulator->str);
    }

    g_mutex_unlock(&app->access.buffer_mutex);

    if (!snapshot || !*snapshot) {
        g_free(snapshot);
        return NULL;
    }

    char *clean = strip_blank_lines(snapshot);
    g_free(snapshot);
    return clean;
}

/*
 * Return the newest persisted terminal TEE row for the current session.
 *
 * The manual AI path can arrive after the timed TEE flush has already moved
 * the accumulator into MariaDB. In that case tee_peek_for_ai() is empty, but
 * the newest terminal capture is still the most relevant context. Do not use
 * the VTE widget snapshot in preference to this row because VTE scrollback
 * can contain substantially older output and duplicate the capture.
 */
char* tee_get_latest_for_ai(AppContext *app) {
    if (!app || !app->database.global_db_conn || !app->session.session_uuid)
        return NULL;

    MYSQL *db = app->database.global_db_conn;
    const char *uuid = app->session.write_to_global
                     ? GLOBAL_SESSION_UUID
                     : app->session.session_uuid;

    mysql_thread_init();
    pthread_mutex_lock(&app->access.db_mutex);

    char *escaped_uuid = g_malloc((strlen(uuid) * 2) + 1);
    if (!escaped_uuid) {
        pthread_mutex_unlock(&app->access.db_mutex);
        mysql_thread_end();
        return NULL;
    }

    unsigned long escaped_len = mysql_real_escape_string(
        db, escaped_uuid, uuid, (unsigned long)strlen(uuid));
    escaped_uuid[escaped_len] = '\0';

    char *query = g_strdup_printf(
        "SELECT content, sequence_id FROM aiterm_history "
        "WHERE session_uuid = '%s' AND is_tee = 1 AND role = 'terminal' "
        "ORDER BY id DESC LIMIT 1",
        escaped_uuid);

    char *snapshot = NULL;
    if (query && mysql_query(db, query) == 0) {
        MYSQL_RES *res = mysql_store_result(db);
        if (res) {
            MYSQL_ROW row = mysql_fetch_row(res);
            if (row && row[0] && *row[0]) {
                snapshot = g_strdup(row[0]);
                DEBUG_PRINT(
                    "[ TEE DB ] latest persisted terminal snapshot=%zu bytes sequence=%s\n",
                    strlen(snapshot), row[1] ? row[1] : "?");
            }
            mysql_free_result(res);
        }
    } else if (query) {
        DEBUG_PRINT("[ TEE DB ] latest snapshot query failed: %s\n",
                    mysql_error(db));
    }

    g_free(query);
    g_free(escaped_uuid);
    pthread_mutex_unlock(&app->access.db_mutex);
    mysql_thread_end();

    return snapshot;
}

char* tee_extract_for_ai(AppContext *app) {
    if (!app || !app->aiterm_runtime.tee_accumulator) return NULL;
    char *snapshot = NULL;

    char *lt_pl   = g_strdup(app->ansi.lt_purple);
    char *cy      = g_strdup(app->ansi.cyan);
    char *yl      = g_strdup(app->ansi.yellow);
    char *gr      = g_strdup(app->ansi.green);
    char *red     = g_strdup(app->ansi.red);
    char *nml     = g_strdup(app->ansi.normal);

    g_mutex_lock(&app->access.buffer_mutex);
    DEBUG_PRINT("[ %sDEBUG%s ]: [%sTEE_EXTRACT_FOR_AI%s]: %sLocked buffer mutex%s\n",
	lt_pl, nml, cy, nml, gr, nml);
    if (app->aiterm_runtime.tee_accumulator->len > 5) {
        snapshot = g_strdup(app->aiterm_runtime.tee_accumulator->str);
        DEBUG_PRINT("[ %sDEBUG%s ] [%sTEE_EXTRACT%s ] %sSnapshot=%s%zu%s bytes%s\n",
		lt_pl, nml, cy, nml, gr,
                red, snapshot ? strlen(snapshot) : 0UL,
		gr, nml);
        g_string_assign(app->aiterm_runtime.tee_accumulator, "");
    } else {
        DEBUG_PRINT("[ %sDEBUG%s ] [%sTEE_EXTRACT%s ] %saccumulator only %s%zu%s bytes; %snothing to flush%s\n",
		lt_pl, nml, cy, nml, gr,
                red, app->aiterm_runtime.tee_accumulator->len,
		gr, red, nml);
    }
    g_mutex_unlock(&app->access.buffer_mutex);
    DEBUG_PRINT("[ %sDEBUG%s ]: [%sTEE_EXTRACT_FOR_AI%s]: %sUnlocked buffer mutex%s\n",
	lt_pl, nml, cy, nml, gr, nml);

    g_free(lt_pl);
    g_free(cy);
    g_free(yl);
    g_free(gr);
    g_free(red);
    g_free(nml);

    return strip_blank_lines(snapshot);
}

// THREADED FLUSH (The 0.8.3 fix):
// This returns INSTANTLY to the UI thread, spawning a background
// worker to handle the network latency of the AI API.
void tee_flush_pending_to_history(AppContext *app) {
    if (!app || !app->aiterm_runtime.tee_accumulator)
        return;

    char *local_out = tee_extract_for_ai(app);
    if (!local_out || !*local_out) {
        g_free(local_out);
        DEBUG_PRINT("[ TEE FLUSH ] No pending TEE data to persist.\n");
        return;
    }

    DEBUG_PRINT("[ TEE FLUSH ] Persisting %zu bytes without AI dispatch.\n",
                strlen(local_out));
    save_tee_to_history(local_out, NULL, "terminal");
    g_free(local_out);
}

void tee_flush_timed(AppContext *app) {
    if (!app) return;
    if (!g_atomic_int_compare_and_exchange(&app->sys.is_processing, 0, 1))
        return;

    char *lt_pl   = g_strdup(app->ansi.lt_purple);
    char *cy      = g_strdup(app->ansi.cyan);
    char *yl      = g_strdup(app->ansi.yellow);
    char *gr      = g_strdup(app->ansi.green);
    char *red     = g_strdup(app->ansi.red);
    char *nml     = g_strdup(app->ansi.normal);

    // Grab the text snapshot safely
    char *local_out = tee_extract_for_ai(app);
    if (!local_out) {
        g_atomic_int_set(&app->sys.is_processing, 0);
        return;
    }

    DEBUG_PRINT("[ %sDEBUG%s ]: [%sTimed Tee Flush%s] %sProcessing Payload%s\n",
	lt_pl, nml, cy, nml, yl, nml);

    /* Tee collection and AutoReply are deliberately separate functions:
     *   - Tee ON + AutoReply OFF: capture and save terminal data only.
     *   - Tee ON + AutoReply ON: capture, save, and send the data to AI.
     *
     * The accumulator must still be flushed when Tee is enabled by itself,
     * otherwise terminal history never reaches the database.  What caused
     * the regression was making that flush unconditional AI work. */
    if (!app->sys.autoreply_enabled) {
        DEBUG_PRINT("[ %sDEBUG%s ]: [%sTimed Tee Flush%s] %sAutoReply OFF: saving terminal data only%s\n",
            lt_pl, nml, cy, nml, gr, nml);

        save_tee_to_history(local_out, NULL, "terminal");
        g_free(local_out);
        g_atomic_int_set(&app->sys.is_processing, 0);
        update_status_label(app, "Ready");

        g_free(lt_pl);
        g_free(cy);
        g_free(yl);
        g_free(gr);
        g_free(red);
        g_free(nml);
        return;
    }

    update_status_label(app, "AI is analyzing (Background)...");

    // Package data for the background thread
    TeeResponseData *trd = g_malloc0(sizeof(TeeResponseData));
    trd->app = app;
    trd->history_role = g_strdup("terminal");
    char *clean_local = strip_blank_lines(local_out);
    trd->terminal_output = clean_local;
    trd->context_type = TAG_LOG_DUMP;
    g_free(local_out);

    // START BACKGROUND THREAD: This is what stops the terminal from hanging!
    g_thread_unref(g_thread_new("tee_background_worker", (GThreadFunc)tee_ai_thread_func, trd));

    g_free(lt_pl);
    g_free(cy);
    g_free(yl);
    g_free(gr);
    g_free(red);
    g_free(nml);

}

// BACKGROUND WORKER:
// This runs on a separate CPU thread. It can "hang" waiting for
// the internet/API without affecting the terminal UI responsiveness.
static gpointer tee_ai_thread_func(gpointer data) {
    TeeResponseData *trd = (TeeResponseData*)data;
    AppContext *app = trd->app;

    /* Terminal/SNMP data is supplied separately as typed XML context. Keep the
     * instruction itself small so the source payload is not duplicated. */
    char *final_prompt = g_strdup(
        "Analyze the supplied terminal context concisely. Focus on hardware IDs, "
        "network configurations, errors, and other actionable evidence.");

    char *clean_prompt = strip_blank_lines(final_prompt);
    TagType context_type = trd->context_type ? trd->context_type : TAG_LOG_DUMP;
    char *response = ai_provider_send_with_context_types(
        app, clean_prompt, trd->terminal_output, TAG_USER, context_type);
    g_free(clean_prompt);

    if (response) {
        trd->response_text = strip_blank_lines(response);
        // Signal the UI thread to display results safely
        g_idle_add(update_tee_ui, trd);
    } else {
        // RESET FLAG on failure so the app doesn't stay locked forever
        g_atomic_int_set(&app->sys.is_processing, 0);
        g_idle_add(reset_ai_status_idle, app);
        g_free(trd->terminal_output);
        g_free(trd->history_role);
        g_free(trd);
    }

    g_free(final_prompt);
    return NULL;
}

// GUI UPDATE CALLBACK:
// Safely runs on the Main UI Thread to update GTK widgets.
static gboolean update_tee_ui(gpointer data) {
    TeeResponseData *trd = (TeeResponseData *)data;
    if (!trd || !trd->app) return FALSE;

    char *lt_pl   = g_strdup(global_app->ansi.lt_purple);
    char *cy      = g_strdup(global_app->ansi.cyan);
    char *yl      = g_strdup(global_app->ansi.yellow);
    char *gr      = g_strdup(global_app->ansi.green);
    char *red     = g_strdup(global_app->ansi.red);
    char *nml     = g_strdup(global_app->ansi.normal);

    DEBUG_PRINT("[%sMEMDBG%s ]: [%sTEE_UI%s] %strd=[%s%p%s] terminal=[%s%p%s] response=[%s%p%s]%s\n",
	lt_pl, nml, cy, nml, yl,
        red, (void*)trd, yl,
        red, (void*)trd->terminal_output, yl,
	red, (void*)trd->response_text, yl, nml);
    char *ai_text = extract_ai_text(trd->response_text);
    DEBUG_PRINT("[%sMEMDBG %s]: [%sTEE_UI%s] %sextract_ai_text -> [%s%p%s]%s\n",
	lt_pl, nml, cy, nml, gr,
	red,  (void*)ai_text, gr, nml);

    if (ai_text) {
        // Display in AI Pane
        write_to_ai_pane(trd->app, "AI (Auto-Reply): ", ai_text, "user_tag", "ai_tag");

        // SAVE TO DATABASE: Ensure automated insights are in the 100-msg history
        DEBUG_PRINT("[%sMEMDBG %s]: [%sTEE_UI%s] %sBEFORE save_tee_to_history terminal=[%s%p%s] ai=[%s%p%s]%s\n",
		lt_pl, nml, cy, nml, gr,
                red, (void*)trd->terminal_output, gr,
		red, (void*)ai_text, gr, nml);

        save_tee_to_history(trd->terminal_output, ai_text,
                            trd->history_role ? trd->history_role : "terminal");

        DEBUG_PRINT("[%sMEMDBG %s]: [%sTEE_UI%s] %sAFTER save_tee_to_history ai=[%s%p%s]%s\n",
		lt_pl, nml, cy, nml, gr, 
		red, (void*)ai_text, gr, nml);

        DEBUG_PRINT("[%sMEMDBG %s]: [%sTEE_UI%s] %sFREE ai_text=[%s%p%s]%s\n", 
		lt_pl, nml, cy, nml, gr, 
		red, (void*)ai_text, gr, nml);

        g_free(ai_text);
    } else {
        write_to_ai_pane(trd->app, "System: ", "Tee Analysis failed to return text.", "cmd_tag", "cmd_tag");
    }

    // --- CRITICAL: Reset processing flag so next timer tick can trigger ---
    update_status_label(trd->app, "Ready");
    g_atomic_int_set(&trd->app->sys.is_processing, 0);

    // Final memory cleanup
    DEBUG_PRINT("[%sMEMDBG %s]: [%sTEE_UI%s] %sFREE response_text=[%s%p%s]%s\n",
	lt_pl, nml, cy, nml, gr, 
	red, (void*)trd->response_text, gr, nml);

    if (trd->response_text) g_free(trd->response_text);
    DEBUG_PRINT("[%sMEMDBG %s]: [%sTEE_UI%s] %sFREE terminal_output=[%s%p%s]%s\n",
	lt_pl, nml, cy, nml, gr,
	red,  (void*)trd->terminal_output, gr, nml);

    if (trd->terminal_output) g_free(trd->terminal_output);
    if (trd->history_role) g_free(trd->history_role);
    DEBUG_PRINT("[%sMEMDBG %s]: [%sTEE_UI%s] %sFREE trd=[%s%p%s]%s\n", 
	lt_pl, nml, cy, nml, gr,
	red, (void*)trd, gr, nml);

    g_free(trd);

    g_free(lt_pl);
    g_free(cy);
    g_free(yl);
    g_free(gr);
    g_free(red);
    g_free(nml);

    return FALSE;
}

void tee_handle_input(AppContext *app, const char *text) {
    if (!text || !app->aiterm_runtime.tee_accumulator) return;

    char *lt_pl   = g_strdup(global_app->ansi.lt_purple);
    char *cy      = g_strdup(global_app->ansi.cyan);
    char *yl      = g_strdup(global_app->ansi.yellow);
    char *gr      = g_strdup(global_app->ansi.green);
    char *red     = g_strdup(global_app->ansi.red);
    char *nml     = g_strdup(global_app->ansi.normal);

    char *clean_text = strip_blank_lines(text);
    DEBUG_PRINT("[ %sDEBUG%s ]: [%sTEE_HANDLE_INPUT%s] %sLocked buffer mutex%s\n",
	lt_pl, nml, cy, nml, gr, nml);
    g_mutex_lock(&app->access.buffer_mutex);
    g_string_append(app->aiterm_runtime.tee_accumulator, clean_text);
    g_mutex_unlock(&app->access.buffer_mutex);
    g_free(clean_text);
    DEBUG_PRINT("[%s DEBUG%s ]: [%sTEE_HANDLE_INPUT%s] %sUnlocked buffer mutex%s\n",
	lt_pl, nml, cy, nml, gr, nml);

    g_free(lt_pl);
    g_free(cy);
    g_free(yl);
    g_free(gr);
    g_free(red);
    g_free(nml);

}

void tee_handle_output(AppContext *app, const char *text_in) {
    if (!text_in || !app->aiterm_runtime.tee_accumulator) return;
    if (text_in[0] == '\n' && text_in[1] == '\0') return;
    char *blank_clean = strip_blank_lines(text_in);
    char *text = noise_filter_apply(app, blank_clean);
    g_free(blank_clean);
    if (!text) return;

    char *lt_pl   = g_strdup(app->ansi.lt_purple);
    char *cy      = g_strdup(app->ansi.cyan);
    char *yl      = g_strdup(app->ansi.yellow);
    char *gr      = g_strdup(app->ansi.green);
    char *red     = g_strdup(app->ansi.red);
    char *nml     = g_strdup(app->ansi.normal);

    DEBUG_PRINT("[ %sDEBUG%s ]: [%sTee Handler%s] %s%s%s\n", 
	lt_pl, nml, cy, nml, gr, text, nml);

    g_mutex_lock(&app->access.buffer_mutex);
    DEBUG_PRINT("[ %sEBUG%s ]: [%sTEE_HANDLE_OUTPUT%s] %sLocked buffer mutex%s\n",
	lt_pl, nml, cy, nml, gr, nml);

    // Delta Upgrade: If AI is already busy, ignore heavy stream chatter
    // to protect context integrity and memory.
    if (g_atomic_int_get(&app->sys.is_processing) && app->aiterm_runtime.tee_accumulator->len > 51200) {
        g_mutex_unlock(&app->access.buffer_mutex);
        g_free(text);
	DEBUG_PRINT("[ %sDEBUG%s ]: [%sTEE_HANDLE_OUTPUT%s] %sUnlocked buffer mutex%s\n",
	    lt_pl, nml, cy, nml, gr, nml);
        return;
    }
    char *clean_text = strip_blank_lines(text);
    g_string_append(app->aiterm_runtime.tee_accumulator, clean_text);

    // Hard limit safety: 64KB max buffer per flush
    if (app->aiterm_runtime.tee_accumulator->len > 65536) {
        char *first_newline = strchr(app->aiterm_runtime.tee_accumulator->str, '\n');
        if (first_newline) {
            size_t offset = first_newline - app->aiterm_runtime.tee_accumulator->str + 1;
            g_string_erase(app->aiterm_runtime.tee_accumulator, 0, offset);
        } else {
            g_string_erase(app->aiterm_runtime.tee_accumulator, 0, 8192);
        }
    }

    g_mutex_unlock(&app->access.buffer_mutex);
    g_free(clean_text);
    g_free(text);
    DEBUG_PRINT("[ %sDEBUG%s ]: [%sTEE_HANDLE_OUTPUT%s] %sUnlocked buffer mutex%s\n",
	lt_pl, nml, cy, nml, gr, nml);

    g_free(lt_pl);
    g_free(cy);
    g_free(yl);
    g_free(gr);
    g_free(red);
    g_free(nml);
}

// Process C-level SNMP poller data and send to Gemini/OpenAI off the main UI thread
void pipe_snmp_to_gemini(AppContext *app, const char *raw_snmp_data) {
    if (!app || !raw_snmp_data || strlen(raw_snmp_data) < 5) return;

    char *lt_pl   = g_strdup(app->ansi.lt_purple);
    char *cy      = g_strdup(app->ansi.cyan);
    char *yl      = g_strdup(app->ansi.yellow);
    char *gr      = g_strdup(app->ansi.green);
    char *red     = g_strdup(app->ansi.red);
    char *nml     = g_strdup(app->ansi.normal);

    // Don't stack requests if the AI API is already processing an active prompt
    if (g_atomic_int_get(&app->sys.is_processing)) {
        DEBUG_PRINT("[ %sDEBUG%s ]: [%sSNMP Pipe%s] %sAI is busy, skipping SNMP tick.%s\n",
		lt_pl, nml, cy, nml, yl, nml);
        return;
    }

    // Clean up input noise if needed
    char *clean_snmp = strip_blank_lines(raw_snmp_data);
    if (!clean_snmp || strlen(clean_snmp) == 0) return;

    DEBUG_PRINT("[ %sDEBUG%s ]: [%sSNMP Pipe%s] %sPackaging SNMP data for AI analysis...%s\n",
	lt_pl, nml, cy, nml, gr, nml);

    // Format an explicit system prompt directing the AI to analyze network metrics
    char *formatted_prompt = g_strdup_printf(
        "Analyze the following raw SNMP poller output concisely.\n"
        "Highlight any device offline statuses, interface errors/drops, abnormal bandwidth spikes, or high system utilization:\n\n"
        "SNMP METRICS:\n%s", clean_snmp
    );

    // Allocate thread payload.  XML type is passed explicitly so worker
    // threads never race on the shared app->xml.type field.
    TeeResponseData *trd = g_malloc0(sizeof(TeeResponseData));
    trd->app = app;
    trd->history_role = g_strdup("snmp");
    trd->terminal_output = formatted_prompt;
    trd->context_type = TAG_SNMP;

    // Set non-blocking UI status
    if (!g_atomic_int_compare_and_exchange(&app->sys.is_processing, 0, 1)) {
        DEBUG_PRINT("[ %sDEBUG%s ]: [%sSNMP Pipe%s] %sAI became busy before reservation;%s dropping tick.%s\n", 
		lt_pl, nml, cy, nml, gr, red, nml);
        g_free(trd->terminal_output);
        g_free(trd->history_role);
        g_free(trd);
        return;
    }
    /* Status updates are dispatched to GTK because this path may be called by the SNMP worker. */
    g_idle_add((GSourceFunc)snmp_status_idle, app);

    // Dispatch payload directly to your existing tee thread worker!
    g_thread_unref(g_thread_new("snmp_ai_worker", (GThreadFunc)tee_ai_thread_func, trd));

    g_free(lt_pl);
    g_free(cy);
    g_free(yl);
    g_free(gr);
    g_free(red);
    g_free(nml);

}

// Dedicated SNMP Background Thread Worker
static gpointer snmp_ai_thread_func(gpointer data) {
    TeeResponseData *trd = (TeeResponseData*)data;
    AppContext *app = trd->app;

    char *lt_pl   = g_strdup(app->ansi.lt_purple);
    char *cy      = g_strdup(app->ansi.cyan);
    char *yl      = g_strdup(app->ansi.yellow);
    char *gr      = g_strdup(app->ansi.green);
    char *red     = g_strdup(app->ansi.red);
    char *nml     = g_strdup(app->ansi.normal);

    char *final_prompt = g_strdup(
        "Analyze the supplied SNMP telemetry. Identify offline devices, timeouts, "
        "abnormal metric values, and network interface anomalies.");

    char *clean_prompt = strip_blank_lines(final_prompt);
    TagType context_type = trd->context_type ? trd->context_type : TAG_SNMP;

    DEBUG_PRINT("[ %sDEBUG%s ] [%sTEE%s] %s========================================%s\n", 
	lt_pl, nml, cy, nml, red, nml);

    DEBUG_PRINT("[ %sDEBUG%s ] [%sTEE%s] %sSending Tee request to AI provider%s\n",
	lt_pl, nml, cy, nml, gr, nml);

    DEBUG_PRINT("[ %sDEBUG%s ] [%sTEE%s] %sProvider=[%s%s%s]%s\n",
	lt_pl, nml, cy, nml, gr,
        red, app->provider_config.provider ? app->provider_config.provider : "(null)", 
	gr, nml);

    DEBUG_PRINT("[ %sDEBUG%s ] [%sTEE%s] %smodel=[%s%s%s]%s\n",
	lt_pl, nml, cy, nml, gr,
        red, app->provider_config.model ? app->provider_config.model : "(null)",
	gr, nml);

    DEBUG_PRINT("[ %sDEBUG%s ] [%sTEE%s] %sSNMP context length=[%s%zu%s]%s\n",
	lt_pl, nml, cy, nml, gr,
        red, trd->terminal_output ? strlen(trd->terminal_output) : 0UL,
	gr, nml);

    if (trd->terminal_output) {
        DEBUG_PRINT("[ %sDEBUG%s ] [%sTEE%s] %sSNMP context:\n%s%.4000s%s\n",
            lt_pl, nml, cy, nml, gr, lt_pl, trd->terminal_output, nml);
    }

DEBUG_PRINT("[ %sDEBUG%s ] [%sTEE%s] %s========================================%s\n",
	lt_pl, nml, cy, nml, red, nml);

    char *response = ai_provider_send_with_context_types(
        app, clean_prompt, trd->terminal_output, TAG_USER, context_type);
    g_free(clean_prompt);

    if (response) {
        trd->response_text = strip_blank_lines(response);
        g_idle_add(update_tee_ui, trd);
    } else {
        g_atomic_int_set(&app->sys.is_processing, 0);
        g_idle_add(reset_ai_status_idle, app);
        g_free(trd->terminal_output);
        g_free(trd->history_role);
        g_free(trd);
    }

    g_free(final_prompt);

    g_free(lt_pl);
    g_free(cy);
    g_free(yl);
    g_free(gr);
    g_free(red);
    g_free(nml);

    return NULL;
}

// Trigger SNMP telemetry submission off the main thread
void snmp_flush_to_gemini(AppContext *app) {
    if (!app || g_atomic_int_get(&app->sys.is_processing)) return;
    if (!app->SnmpContext.enable_gemini_feed) return;

    char *lt_pl   = g_strdup(app->ansi.lt_purple);
    char *cy      = g_strdup(app->ansi.cyan);
    char *yl      = g_strdup(app->ansi.yellow);
    char *gr      = g_strdup(app->ansi.green);
    char *red     = g_strdup(app->ansi.red);
    char *nml     = g_strdup(app->ansi.normal);

    // Grab XML telemetry payload using snmp_manager helper
    char *telemetry_xml = snmp_format_telemetry_payload(app);
    if (!telemetry_xml || strlen(telemetry_xml) < 25) {
        if (telemetry_xml) g_free(telemetry_xml);
        return;
    }

    if (!g_atomic_int_compare_and_exchange(&app->sys.is_processing, 0, 1)) {
        DEBUG_PRINT("[ %sDEBUG%s ]: [%sSNMP Flush%s] %sAI became busy before reservation; %sdeferring.%s\n",
		lt_pl, nml, cy, nml, gr, red, nml);
        g_free(telemetry_xml);
        return;
    }
    g_idle_add((GSourceFunc)snmp_status_idle, app);

    TeeResponseData *trd = g_malloc0(sizeof(TeeResponseData));
    trd->app = app;
    trd->history_role = g_strdup("snmp");
    char *clean_telemetry = strip_blank_lines(telemetry_xml);
    trd->terminal_output = clean_telemetry;
    trd->context_type = TAG_SNMP;

    g_free(telemetry_xml);

    // Spawn non-blocking background thread
    g_thread_unref(g_thread_new("snmp_gemini_worker", (GThreadFunc)snmp_ai_thread_func, trd));

    g_free(lt_pl);
    g_free(cy);
    g_free(yl);
    g_free(gr);
    g_free(red);
    g_free(nml);

}

