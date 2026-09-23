// Part of project: aiterm
// idle.h
// Idle detection and automatic toggle suspension
// By: Peter Talbott
// September 2026

#ifndef IDLE_H
#define IDLE_H

#include <glib.h>
#include <gtk/gtk.h>

typedef struct AppContext AppContext;

/*
 * Runtime state for the idle subsystem.
 *
 * The saved values are deliberately kept separate from the live system
 * booleans.  While idle, the live values are forced OFF.  When activity is
 * detected, only the values that were ON when idle began are restored.
 */
typedef struct {
    guint timer_id;
    guint timeout_minutes;
    gint64 last_activity_us;
    gboolean suspended;

    gboolean saved_autoreply;
    gboolean saved_auto_execute;
    gboolean saved_tee;
    gboolean saved_debug;
    gboolean saved_xml_tagging;
    gboolean saved_noise_filter;
    gboolean saved_smart_cache;
    gboolean saved_ratelimit;
    gboolean saved_session_read_global;
    gboolean saved_session_write_global;
    gboolean saved_snmp_payload;
    gboolean saved_snmp_ticker;
    gboolean saved_ai_retry;
    gboolean saved_session_config;
} IdleState;


// Function Prototypes
gboolean idle_event_after(GtkWidget *widget, GdkEvent *event, gpointer user_data);
guint idle_get_timeout_minutes(AppContext *app);

void idle_save_toggle_state(AppContext *app);
void idle_force_toggles_off(AppContext *app);
void idle_restore_toggle_state(AppContext *app);
void idle_sync_toggle_menu(AppContext *app);
void idle_log(AppContext *app, const char *message);
void idle_suspend(AppContext *app);
void idle_resume(AppContext *app);
void idle_mark_activity(AppContext *app);
void idle_init(AppContext *app);
void idle_shutdown(AppContext *app);

#endif
