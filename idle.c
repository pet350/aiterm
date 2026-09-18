// Part of project: aiterm
// idle.c
// Idle detection and automatic toggle suspension
// By: Peter Talbott
// Assisted by: OpenAI
// September 2026

#include <stdio.h>
#include <time.h>

#include "gui.h"
#include "idle.h"
#include "menu.h"
#include "ai_retry.h"
#include "commands.h"
#include "toggles.h"

#define IDLE_DEFAULT_TIMEOUT_MINUTES 10U
#define IDLE_CHECK_INTERVAL_MS       1000U

/*
 * Keep the idle watcher independent of the SNMP/AI worker threads.  All
 * changes to GTK widgets and all live-toggle changes happen on the GTK main
 * thread, exactly where the existing toggle system expects them.
 */
static void idle_save_toggle_state(AppContext *app)
{
    app->idle.saved_autoreply            = app->sys.autoreply_enabled;
    app->idle.saved_auto_execute         = app->sys.auto_execute_enabled;
    app->idle.saved_tee                  = app->sys.tee_enabled;
    app->idle.saved_debug                = app->sys.debug_mode;
    app->idle.saved_xml_tagging          = app->xml.tagging_enabled;
    app->idle.saved_noise_filter         = app->sys.noise_filter_enabled;
    app->idle.saved_smart_cache           = app->sys.smart_cache_enabled;
    app->idle.saved_ratelimit             = app->sys.ratelimit_enabled;
    app->idle.saved_session_read_global   = app->session.read_from_global;
    app->idle.saved_session_write_global  = app->session.write_to_global;
    app->idle.saved_snmp_payload          = app->SnmpContext.enable_gemini_feed;
    app->idle.saved_snmp_ticker           = app->sys.snmp_ticker_enabled;
    app->idle.saved_ai_retry               = app->retry_config.is_enabled;
    app->idle.saved_session_config         = app->sys.load_from_session;
}

static void idle_force_toggles_off(AppContext *app)
{
    app->sys.autoreply_enabled          = FALSE;
    app->sys.auto_execute_enabled       = FALSE;
    app->sys.tee_enabled                = FALSE;
    app->sys.debug_mode                 = FALSE;
    app->xml.tagging_enabled            = FALSE;
    app->sys.noise_filter_enabled       = FALSE;
    app->sys.smart_cache_enabled        = FALSE;
    app->sys.ratelimit_enabled          = FALSE;
    app->session.read_from_global       = FALSE;
    app->session.write_to_global        = FALSE;
    app->SnmpContext.enable_gemini_feed = FALSE;
    app->sys.snmp_ticker_enabled        = FALSE;
    app->retry_config.is_enabled        = FALSE;
    app->sys.load_from_session          = FALSE;

    /* The existing ticker callback runs every 150 ms.  Turning the toggle
     * boolean off makes it draw a disabled message, but leaves that timer
     * alive.  Suspend the timer too so an unattended session does not spend
     * CPU waking the GTK main loop unnecessarily. */
    if (app->gui.snmp_ticker_timer_id) {
        /* Let the existing callback paint its normal disabled state once,
         * then remove the repeating source. */
        update_snmp_ticker_scroll(app);
        g_source_remove(app->gui.snmp_ticker_timer_id);
        app->gui.snmp_ticker_timer_id = 0;
    }
}

static void idle_restore_toggle_state(AppContext *app)
{
    app->sys.autoreply_enabled          = app->idle.saved_autoreply;
    app->sys.auto_execute_enabled       = app->idle.saved_auto_execute;
    app->sys.tee_enabled                = app->idle.saved_tee;
    app->sys.debug_mode                 = app->idle.saved_debug;
    app->xml.tagging_enabled            = app->idle.saved_xml_tagging;
    app->sys.noise_filter_enabled       = app->idle.saved_noise_filter;
    app->sys.smart_cache_enabled        = app->idle.saved_smart_cache;
    app->sys.ratelimit_enabled          = app->idle.saved_ratelimit;
    app->session.read_from_global       = app->idle.saved_session_read_global;
    app->session.write_to_global        = app->idle.saved_session_write_global;
    app->SnmpContext.enable_gemini_feed = app->idle.saved_snmp_payload;
    app->sys.snmp_ticker_enabled        = app->idle.saved_snmp_ticker;
    app->retry_config.is_enabled        = app->idle.saved_ai_retry;
    app->sys.load_from_session          = app->idle.saved_session_config;

    /* Re-create the ticker timer only if it was enabled before suspension. */
    if (app->sys.snmp_ticker_enabled && !app->gui.snmp_ticker_timer_id) {
        app->gui.snmp_ticker_timer_id =
            g_timeout_add(150, update_snmp_ticker_scroll, app);
        g_source_set_name_by_id(app->gui.snmp_ticker_timer_id,
                                "aiterm-snmp-ticker");
    }
}

static void idle_sync_toggle_menu(AppContext *app)
{
    if (!app) return;

    /* setup_menu_toggle() connects its callback with NULL user data.  Block
     * that exact connection while changing the menu state so an idle
     * transition never dispatches the normal command pipeline. */
#define IDLE_SET_MENU(widget, state) \
    do { \
        if ((widget) && GTK_IS_CHECK_MENU_ITEM(widget)) { \
            g_signal_handlers_block_by_func((widget), G_CALLBACK(on_menu_toggle_item_toggled), NULL); \
            gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(widget), (state)); \
            g_signal_handlers_unblock_by_func((widget), G_CALLBACK(on_menu_toggle_item_toggled), NULL); \
        } \
    } while (0)

    IDLE_SET_MENU(app->ui.toggle_autoreply, app->sys.autoreply_enabled);
    IDLE_SET_MENU(app->ui.toggle_autoexe, app->sys.auto_execute_enabled);
    IDLE_SET_MENU(app->ui.toggle_tee, app->sys.tee_enabled);
    IDLE_SET_MENU(app->ui.toggle_debug, app->sys.debug_mode);
    IDLE_SET_MENU(app->ui.toggle_xml_payload_tagging, app->xml.tagging_enabled);
    IDLE_SET_MENU(app->ui.toggle_noise_filter, app->sys.noise_filter_enabled);
    IDLE_SET_MENU(app->ui.toggle_smart_cache, app->sys.smart_cache_enabled);
    IDLE_SET_MENU(app->ui.toggle_ratelimit, app->sys.ratelimit_enabled);
    IDLE_SET_MENU(app->ui.toggle_session_read_global, app->session.read_from_global);
    IDLE_SET_MENU(app->ui.toggle_session_write_global, app->session.write_to_global);
    IDLE_SET_MENU(app->ui.toggle_snmp_payload, app->SnmpContext.enable_gemini_feed);
    IDLE_SET_MENU(app->ui.toggle_snmp_ticker, app->sys.snmp_ticker_enabled);
    IDLE_SET_MENU(app->ui.toggle_session_config, app->sys.load_from_session);

    GtkWidget *retry_item = NULL;
    if (app->gui.window && G_IS_OBJECT(app->gui.window)) {
        retry_item = GTK_WIDGET(g_object_get_data(
            G_OBJECT(app->gui.window), "retry_toggle_menu_item"));
    }
    if (retry_item && GTK_IS_CHECK_MENU_ITEM(retry_item)) {
        /* The retry callback is private to menu.c, so idle.c must not refer
         * to it directly.  The callback already ignores redundant state
         * changes, and we set retry_config.is_enabled first.  Therefore the
         * signal can safely fire here without producing duplicate side
         * effects or changing the saved idle state. */
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(retry_item),
                                       app->retry_config.is_enabled);
    }

#undef IDLE_SET_MENU
}

static void idle_log(AppContext *app, const char *message)
{
    if (!app || !message) return;

    /* Do not use DEBUG_PRINT here when debug mode itself is being suspended.
     * stderr logging remains useful, but this avoids coupling idle handling
     * to the state of the debug toggle. */
    fprintf(stderr, "[ aiterm IDLE ]: %s\n", message);
}

static void idle_suspend(AppContext *app)
{
    if (!app || app->idle.suspended) return;

    /* Snapshot exactly what was enabled immediately before suspension. */
    idle_save_toggle_state(app);
    idle_force_toggles_off(app);
    app->idle.suspended = TRUE;

    /* Update the existing menu widgets without invoking their toggle
     * callbacks.  This is important: going idle must not generate a stream
     * of command/DB side effects. */
    idle_sync_toggle_menu(app);

    idle_log(app, "No user activity detected; toggle systems suspended.");
}

static void idle_resume(AppContext *app)
{
    if (!app || !app->idle.suspended) return;

    idle_restore_toggle_state(app);
    app->idle.suspended = FALSE;

    /* Restore the menu exactly to the state it had before idle suspension. */
    idle_sync_toggle_menu(app);

    idle_log(app, "User activity detected; previous toggle states restored.");
}

static gboolean idle_watchdog(gpointer user_data)
{
    AppContext *app = (AppContext *)user_data;
    if (!app) return FALSE;

    if (app->idle.timeout_minutes == 0) {
        /* Zero means disabled.  Keep the timer alive so the timeout can be
         * changed later without having to rebuild the source. */
        return TRUE;
    }

    gint64 now = g_get_monotonic_time();
    gint64 timeout_us = (gint64)app->idle.timeout_minutes * 60 * G_USEC_PER_SEC;

    if (!app->idle.suspended && now - app->idle.last_activity_us >= timeout_us) {
        idle_suspend(app);
    }

    return TRUE;
}

void idle_mark_activity(AppContext *app)
{
    if (!app) return;

    app->idle.last_activity_us = g_get_monotonic_time();

    if (app->idle.suspended) {
        idle_resume(app);
    }
}

gboolean idle_event_after(GtkWidget *widget, GdkEvent *event, gpointer user_data)
{
    AppContext *app = (AppContext *)user_data;
    if (!app || !event) return FALSE;

    switch (event->type) {
        case GDK_KEY_PRESS:
        case GDK_BUTTON_PRESS:
        case GDK_2BUTTON_PRESS:
        case GDK_3BUTTON_PRESS:
        case GDK_MOTION_NOTIFY:
        case GDK_SCROLL:
        case GDK_TOUCH_BEGIN:
        case GDK_TOUCH_UPDATE:
        case GDK_TOUCH_END:
        case GDK_FOCUS_CHANGE:
            idle_mark_activity(app);
            break;
        default:
            break;
    }

    return FALSE;
}

guint idle_get_timeout_minutes(AppContext *app)
{
    if (!app) {
        return IDLE_DEFAULT_TIMEOUT_MINUTES;
    }

    return app->idle.timeout_minutes;
}

void idle_init(AppContext *app)
{
    if (!app) return;

    if (app->idle.timeout_minutes == 0) {
        app->idle.timeout_minutes = IDLE_DEFAULT_TIMEOUT_MINUTES;
    }

    app->idle.suspended = FALSE;
    app->idle.last_activity_us = g_get_monotonic_time();

    if (!app->idle.timer_id) {
        app->idle.timer_id = g_timeout_add(IDLE_CHECK_INTERVAL_MS, idle_watchdog, app);
        g_source_set_name_by_id(app->idle.timer_id, "aiterm-idle-watchdog");
    }

    idle_log(app, "Idle watchdog started.");
}

void idle_shutdown(AppContext *app)
{
    if (!app) return;

    if (app->idle.suspended) {
        /* Never let a temporary idle state become the persisted shutdown
         * state.  main.c synchronizes booleans to the DB after this returns. */
        idle_resume(app);
    }

    if (app->idle.timer_id) {
        g_source_remove(app->idle.timer_id);
        app->idle.timer_id = 0;
    }
}
