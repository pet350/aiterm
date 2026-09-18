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

/* Initialize the idle subsystem and start its lightweight 1-second watcher. */
void idle_init(AppContext *app);

/* Stop the watcher during orderly application shutdown. */
void idle_shutdown(AppContext *app);

/* Mark user activity.  This is intentionally cheap and main-thread only. */
void idle_mark_activity(AppContext *app);

/* GTK window event hook used to detect keyboard/mouse activity globally. */
gboolean idle_event_after(GtkWidget *widget, GdkEvent *event, gpointer user_data);

/* Return the configured timeout, defaulting to 10 minutes if unset. */
guint idle_get_timeout_minutes(AppContext *app);

#endif
