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
    app->aiterm_runtime.tee_accumulator = g_string_new("");
    g_mutex_init(&app->access.buffer_mutex);
    DEBUG_PRINT("[DEBUG]: [Tee Handler] initialized.\n");
}

// ATOMIC SNAPSHOT (The 0.8.2 fix):
// Grabs text and clears the buffer in one locked operation
// to prevent splicing and duplication bugs.
char* tee_extract_for_ai(AppContext *app) {
    if (!app || !app->aiterm_runtime.tee_accumulator) return NULL;
    char *snapshot = NULL;

    g_mutex_lock(&app->access.buffer_mutex);
    DEBUG_PRINT("[DEBUG]: [TEE_EXTRACT_FOR_AI]: Locked buffer mutex\n");
    if (app->aiterm_runtime.tee_accumulator->len > 5) {
        snapshot = g_strdup(app->aiterm_runtime.tee_accumulator->str);
        g_string_assign(app->aiterm_runtime.tee_accumulator, "");
    }
    g_mutex_unlock(&app->access.buffer_mutex);
    DEBUG_PRINT("[DEBUG]: [TEE_EXTRACT_FOR_AI]: Unlocked buffer mutex\n");
    return strip_blank_lines(snapshot);
}

// THREADED FLUSH (The 0.8.3 fix):
// This returns INSTANTLY to the UI thread, spawning a background
// worker to handle the network latency of the AI API.
void tee_flush_timed(AppContext *app) {
    if (!app) return;
    if (!g_atomic_int_compare_and_exchange(&app->sys.is_processing, 0, 1))
        return;

    // Grab the text snapshot safely
    char *local_out = tee_extract_for_ai(app);
    if (!local_out) {
        g_atomic_int_set(&app->sys.is_processing, 0);
        return;
    }

    DEBUG_PRINT("[DEBUG]: [Timed Tee Flush] Processing Payload\n");
    update_status_label(app, "AI is analyzing (Background)...");

    // Package data for the background thread
    TeeResponseData *trd = g_malloc0(sizeof(TeeResponseData));
    trd->app = app;
    char *clean_local = strip_blank_lines(local_out);
    trd->terminal_output = xml_wrap_with_type(app, clean_local, TAG_LOG_DUMP);
    g_free(clean_local);

    // START BACKGROUND THREAD: This is what stops the terminal from hanging!
    g_thread_unref(g_thread_new("tee_background_worker", (GThreadFunc)tee_ai_thread_func, trd));
}

// BACKGROUND WORKER:
// This runs on a separate CPU thread. It can "hang" waiting for
// the internet/API without affecting the terminal UI responsiveness.
static gpointer tee_ai_thread_func(gpointer data) {
    TeeResponseData *trd = (TeeResponseData*)data;
    AppContext *app = trd->app;

    char *final_prompt = g_strdup_printf(
        "Analyze this terminal snippet concisely. Focus on hardware IDs, "
        "network configurations, or error messages.\n\n"
        "TERMINAL OUTPUT:\n%s", trd->terminal_output
    );

    char *wrapped_prompt = NULL;
    char *clean_prompt = strip_blank_lines(final_prompt);
    wrapped_prompt = xml_wrap_with_type(app, clean_prompt, TAG_LOG_DUMP);
    g_free(clean_prompt);

    char *response = NULL;
    if (app->provider_config.kind == PROVIDER_KIND_GEMINI_GENERATE) {
        response = send_to_gemini(app, wrapped_prompt);
    } else {
        response = send_to_openai(app, wrapped_prompt);
    }

    if (response) {
        trd->response_text = strip_blank_lines(response);
        // Signal the UI thread to display results safely
        g_idle_add(update_tee_ui, trd);
    } else {
        // RESET FLAG on failure so the app doesn't stay locked forever
        g_atomic_int_set(&app->sys.is_processing, 0);
        g_idle_add(reset_ai_status_idle, app);
        g_free(trd->terminal_output);
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

    DEBUG_PRINT("[MEMDBG]: [TEE_UI] trd=%p terminal=%p response=%p\n",
                (void*)trd, (void*)trd->terminal_output, (void*)trd->response_text);
    char *ai_text = extract_ai_text(trd->response_text);
    DEBUG_PRINT("[MEMDBG]: [TEE_UI] extract_ai_text -> %p\n", (void*)ai_text);

    if (ai_text) {
        // Display in AI Pane
        write_to_ai_pane(trd->app, "AI (Auto-Reply): ", ai_text, "user_tag", "ai_tag");

        // SAVE TO DATABASE: Ensure automated insights are in the 100-msg history
        DEBUG_PRINT("[MEMDBG]: [TEE_UI] BEFORE save_tee_to_history terminal=%p ai=%p\n",
                    (void*)trd->terminal_output, (void*)ai_text);
        save_tee_to_history(trd->terminal_output, ai_text);
        DEBUG_PRINT("[MEMDBG]: [TEE_UI] AFTER save_tee_to_history ai=%p\n", (void*)ai_text);

        DEBUG_PRINT("[MEMDBG]: [TEE_UI] FREE ai_text=%p\n", (void*)ai_text);
        g_free(ai_text);
    } else {
        write_to_ai_pane(trd->app, "System: ", "Tee Analysis failed to return text.", "cmd_tag", "cmd_tag");
    }

    // --- CRITICAL: Reset processing flag so next timer tick can trigger ---
    update_status_label(trd->app, "Ready");
    g_atomic_int_set(&trd->app->sys.is_processing, 0);

    // Final memory cleanup
    DEBUG_PRINT("[MEMDBG]: [TEE_UI] FREE response_text=%p\n", (void*)trd->response_text);
    if (trd->response_text) g_free(trd->response_text);
    DEBUG_PRINT("[MEMDBG]: [TEE_UI] FREE terminal_output=%p\n", (void*)trd->terminal_output);
    if (trd->terminal_output) g_free(trd->terminal_output);
    DEBUG_PRINT("[MEMDBG]: [TEE_UI] FREE trd=%p\n", (void*)trd);
    g_free(trd);

    return FALSE;
}

void tee_handle_input(AppContext *app, const char *text) {
    if (!text || !app->aiterm_runtime.tee_accumulator) return;
    char *clean_text = strip_blank_lines(text);
    DEBUG_PRINT("[DEBUG]: TEE_HANDLE_INPUT: Locked buffer mutex\n");
    g_mutex_lock(&app->access.buffer_mutex);
    g_string_append(app->aiterm_runtime.tee_accumulator, clean_text);
    g_mutex_unlock(&app->access.buffer_mutex);
    g_free(clean_text);
    DEBUG_PRINT("[DEBUG]: TEE_HANDLE_INPUT: Unlocked buffer mutex\n");
}

void tee_handle_output(AppContext *app, const char *text_in) {
    if (!text_in || !app->aiterm_runtime.tee_accumulator) return;
    if (text_in[0] == '\n' && text_in[1] == '\0') return;
    char *blank_clean = strip_blank_lines(text_in);
    char *text = noise_filter_apply(app, blank_clean);
    g_free(blank_clean);
    if (!text) return;

    DEBUG_PRINT("[DEBUG]: [Tee Handler] %s\n", text);

    g_mutex_lock(&app->access.buffer_mutex);
    DEBUG_PRINT("[DEBUG]: TEE_HANDLE_OUTPUT: Locked buffer mutex\n");
    // Delta Upgrade: If AI is already busy, ignore heavy stream chatter
    // to protect context integrity and memory.
    if (g_atomic_int_get(&app->sys.is_processing) && app->aiterm_runtime.tee_accumulator->len > 51200) {
        g_mutex_unlock(&app->access.buffer_mutex);
        g_free(text);
	DEBUG_PRINT("[DEBUG]: TEE_HANDLE_OUTPUT: Unlocked buffer mutex\n");
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
    DEBUG_PRINT("[DEBUG]: TEE_HANDLE_OUTPUT: Unlocked buffer mutex\n");
}

// Process C-level SNMP poller data and send to Gemini/OpenAI off the main UI thread
void pipe_snmp_to_gemini(AppContext *app, const char *raw_snmp_data) {
    if (!app || !raw_snmp_data || strlen(raw_snmp_data) < 5) return;

    // Don't stack requests if the AI API is already processing an active prompt
    if (g_atomic_int_get(&app->sys.is_processing)) {
        DEBUG_PRINT("[DEBUG]: [SNMP Pipe] AI is busy, skipping SNMP tick.\n");
        return;
    }

    // Clean up input noise if needed
    char *clean_snmp = strip_blank_lines(raw_snmp_data);
    if (!clean_snmp || strlen(clean_snmp) == 0) return;

    DEBUG_PRINT("[DEBUG]: [SNMP Pipe] Packaging SNMP data for AI analysis...\n");

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
    trd->terminal_output = xml_wrap_with_type(app, formatted_prompt, TAG_LOG_DUMP);

    g_free(formatted_prompt);

    // Set non-blocking UI status
    if (!g_atomic_int_compare_and_exchange(&app->sys.is_processing, 0, 1)) {
        DEBUG_PRINT("[DEBUG]: [SNMP Pipe] AI became busy before reservation; dropping tick.\n");
        g_free(trd->terminal_output);
        g_free(trd);
        return;
    }
    /* Status updates are dispatched to GTK because this path may be called by the SNMP worker. */
    g_idle_add((GSourceFunc)snmp_status_idle, app);

    // Dispatch payload directly to your existing tee thread worker!
    g_thread_unref(g_thread_new("snmp_ai_worker", (GThreadFunc)tee_ai_thread_func, trd));
}

// Dedicated SNMP Background Thread Worker
static gpointer snmp_ai_thread_func(gpointer data) {
    TeeResponseData *trd = (TeeResponseData*)data;
    AppContext *app = trd->app;

    char *final_prompt = g_strdup_printf(
        "Analyze the following SNMP telemetry metrics. Identify any offline devices, "
        "timeouts, abnormal metric values, or network interface anomalies:\n\n%s", 
        trd->terminal_output
    );

    char *clean_prompt = strip_blank_lines(final_prompt);
    char *wrapped_prompt = xml_wrap_with_type(app, clean_prompt, TAG_LOG_DUMP);
    g_free(clean_prompt);

    char *response = NULL;
    if (app->provider_config.kind == PROVIDER_KIND_GEMINI_GENERATE) {
        response = send_to_gemini(app, wrapped_prompt);
    } else {
        response = send_to_openai(app, wrapped_prompt);
    }

    if (response) {
        trd->response_text = strip_blank_lines(response);
        g_idle_add(update_tee_ui, trd);
    } else {
        g_atomic_int_set(&app->sys.is_processing, 0);
        g_idle_add(reset_ai_status_idle, app);
        g_free(trd->terminal_output);
        g_free(trd);
    }

    g_free(final_prompt);
    return NULL;
}

// Trigger SNMP telemetry submission off the main thread
void snmp_flush_to_gemini(AppContext *app) {
    if (!app || g_atomic_int_get(&app->sys.is_processing)) return;
    if (!app->SnmpContext.enable_gemini_feed) return;

    // Grab XML telemetry payload using snmp_manager helper
    char *telemetry_xml = snmp_format_telemetry_payload(app);
    if (!telemetry_xml || strlen(telemetry_xml) < 25) {
        if (telemetry_xml) g_free(telemetry_xml);
        return;
    }

    if (!g_atomic_int_compare_and_exchange(&app->sys.is_processing, 0, 1)) {
        DEBUG_PRINT("[DEBUG]: [SNMP Flush] AI became busy before reservation; deferring.\n");
        g_free(telemetry_xml);
        return;
    }
    g_idle_add((GSourceFunc)snmp_status_idle, app);

    TeeResponseData *trd = g_malloc0(sizeof(TeeResponseData));
    trd->app = app;
    char *clean_telemetry = strip_blank_lines(telemetry_xml);
    trd->terminal_output = xml_wrap_with_type(app, clean_telemetry, TAG_LOG_DUMP);
    g_free(clean_telemetry);

    g_free(telemetry_xml);

    // Spawn non-blocking background thread
    g_thread_unref(g_thread_new("snmp_gemini_worker", (GThreadFunc)snmp_ai_thread_func, trd));
}

