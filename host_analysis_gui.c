// part of the aiterm project
// host_analysis_gui.c - additive host telemetry collection
// aiterm The terminal emulator with an AI Pane
// AITerm Delta-2: read-only Host Analysis Manager.
// Shares the existing .session-dialog CSS class with other managers.
// GTK access is confined to the main thread; collector data is snapshotted.
// By: Peter Talbott
// Assisted by: OpenAI
// October 2026

#include <gtk/gtk.h>
#include <string.h>
#include <time.h>
#include "host_analysis_gui.h"
#include "host_analysis.h"

typedef struct {
    AppContext *app;
    GtkWidget *window;
    GtkListStore *store;
    GtkWidget *details;
    GtkWidget *summary;
} HostManager;

enum { HOST_COL_NAME, HOST_COL_SOURCES, HOST_COL_STATE, HOST_COL_INDEX, HOST_COL_COUNT };

static gchar *source_list(guint flags)
{
    GString *s = g_string_new(NULL);
    const struct { guint flag; const char *name; } items[] = {
        { HOST_COLLECTOR_LOCAL, "Local" },
        { HOST_COLLECTOR_SYSTEMD, "Systemd" },
        { HOST_COLLECTOR_SNMP, "SNMP" },
        { HOST_COLLECTOR_SSH, "SSH" },
        { HOST_COLLECTOR_DATABASE, "Database" }
    };
    for (guint i = 0; i < G_N_ELEMENTS(items); ++i) {
        if (!(flags & items[i].flag)) continue;
        if (s->len) g_string_append(s, ", ");
        g_string_append(s, items[i].name);
    }
    if (!s->len) g_string_append(s, "None");
    return g_string_free(s, FALSE);
}

static const char *state_name(HostState state)
{
    switch (state) {
        case HOST_ONLINE: return "Online";
        case HOST_OFFLINE: return "Offline";
        case HOST_WARNING: return "Warning";
        case HOST_CRITICAL: return "Critical";
        default: return "Unknown";
    }
}

static void host_manager_show_details(HostManager *m, guint index)
{
    HostDetails host;
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(m->details));
    if (!host_analysis_get_host(m->app, index, &host)) {
        gtk_text_buffer_set_text(buffer, "Host record is no longer available. Refresh the list.", -1);
        return;
    }
    gchar *basic = host_analysis_format_host(&host);
    GString *s = g_string_new(basic ? basic : "");
    g_free(basic);
    g_string_append_printf(s, "State: %s\n", state_name(host.state));
    if (host.source_flags & HOST_COLLECTOR_SYSTEMD) {
        g_string_append_printf(s, "\nSystemd: %d failed service(s)\n", host.failed_services);
        for (guint i = 0; i < host.systemd_failed_stored && i < HOST_SYSTEMD_MAX_FAILED; ++i)
            g_string_append_printf(s, "  %s\n", host.systemd_failed_units[i]);
        if (host.systemd_error[0])
            g_string_append_printf(s, "Systemd error: %s\n", host.systemd_error);
    }
    /* Copy matching observations while holding only the Host Analysis mutex. */
    HostAnalysisContext *ctx = &m->app->host_analysis;
    pthread_mutex_lock(&ctx->mutex);
    guint count = 0;
    for (guint i = 0; i < ctx->snmp_observation_count; ++i) {
        const HostSnmpObservation *o = &ctx->snmp_observations[i];
        if (o->host_index != index) continue;
        if (!count) g_string_append(s, "\nSNMP observations:\n");
        g_string_append_printf(s, "  %s [%s] = %s\n", o->label, o->oid, o->value);
        ++count;
    }
    pthread_mutex_unlock(&ctx->mutex);
    gtk_text_buffer_set_text(buffer, s->str, -1);
    g_string_free(s, TRUE);
}

static void on_host_selected(GtkTreeSelection *selection, gpointer user_data)
{
    HostManager *m = user_data;
    GtkTreeModel *model;
    GtkTreeIter iter;
    if (!gtk_tree_selection_get_selected(selection, &model, &iter)) return;
    guint index = 0;
    gtk_tree_model_get(model, &iter, HOST_COL_INDEX, &index, -1);
    host_manager_show_details(m, index);
}

static void host_manager_reload(HostManager *m)
{
    GtkTreeSelection *selection = NULL;
    gchar *selected_hostname = NULL;
    GtkWidget *tree = g_object_get_data(G_OBJECT(m->window), "host-tree");
    if (tree) {
        selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(tree));
        GtkTreeModel *model;
        GtkTreeIter iter;
        if (gtk_tree_selection_get_selected(selection, &model, &iter))
            gtk_tree_model_get(model, &iter, HOST_COL_NAME, &selected_hostname, -1);
    }
    gtk_list_store_clear(m->store);
    guint n = 0;
    if (m->app->host_analysis.initialized) {
        pthread_mutex_lock(&m->app->host_analysis.mutex);
        n = (guint)CLAMP(m->app->host_analysis.count, 0, MAX_HOSTS);
        pthread_mutex_unlock(&m->app->host_analysis.mutex);
    }
    GtkTreeIter selected_iter;
    gboolean found_selected = FALSE;
    for (guint i = 0; i < n; ++i) {
        HostDetails host;
        if (!host_analysis_get_host(m->app, i, &host)) continue;
        gchar *sources = source_list(host.source_flags);
        GtkTreeIter iter;
        gtk_list_store_append(m->store, &iter);
        gtk_list_store_set(m->store, &iter,
                           HOST_COL_NAME, host.hostname,
                           HOST_COL_SOURCES, sources,
                           HOST_COL_STATE, state_name(host.state),
                           HOST_COL_INDEX, i, -1);
        if (selected_hostname && g_strcmp0(selected_hostname, host.hostname) == 0) {
            selected_iter = iter;
            found_selected = TRUE;
        }
        g_free(sources);
    }
    gchar *status = g_strdup_printf("%u host(s)  |  Snapshot from Host Analysis", n);
    gtk_label_set_text(GTK_LABEL(m->summary), status);
    g_free(status);
    if (selection && found_selected) gtk_tree_selection_select_iter(selection, &selected_iter);
    else if (selection && n) {
        GtkTreeIter first;
        if (gtk_tree_model_get_iter_first(GTK_TREE_MODEL(m->store), &first))
            gtk_tree_selection_select_iter(selection, &first);
    } else {
        GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(m->details));
        gtk_text_buffer_set_text(buffer, "No hosts collected yet. Use Refresh after collectors have run.", -1);
    }
    g_free(selected_hostname);
}

static void on_reload(GtkButton *button, gpointer data)
{
    (void)button;
    host_manager_reload(data);
}

static void on_destroy(GtkWidget *widget, gpointer data)
{
    (void)widget;
    HostManager *m = data;
    g_object_set_data(G_OBJECT(m->app->gui.window), "host-analysis-manager", NULL);
    g_free(m);
}

void host_analysis_manager_show(AppContext *app)
{
    if (!app || !app->gui.window) return;
    HostManager *existing = g_object_get_data(G_OBJECT(app->gui.window), "host-analysis-manager");
    if (existing) {
        gtk_window_present(GTK_WINDOW(existing->window));
        return;
    }
    HostManager *m = g_new0(HostManager, 1);
    m->app = app;
    m->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(m->window), "Host Analysis Manager");
    gtk_window_set_default_size(GTK_WINDOW(m->window), 1000, 640);
    gtk_window_set_transient_for(GTK_WINDOW(m->window), GTK_WINDOW(app->gui.window));
    gtk_style_context_add_class(gtk_widget_get_style_context(m->window), "session-dialog");
    g_object_set_data(G_OBJECT(app->gui.window), "host-analysis-manager", m);
    g_signal_connect(m->window, "destroy", G_CALLBACK(on_destroy), m);

    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(root), 10);
    gtk_style_context_add_class(gtk_widget_get_style_context(root), "session-dialog");
    gtk_container_add(GTK_CONTAINER(m->window), root);
    m->summary = gtk_label_new("Host Analysis");
    gtk_widget_set_halign(m->summary, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(root), m->summary, FALSE, FALSE, 0);

    GtkWidget *paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(root), paned, TRUE, TRUE, 0);
    m->store = gtk_list_store_new(HOST_COL_COUNT, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_UINT);
    GtkWidget *tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(m->store));
    g_object_unref(m->store);
    g_object_set_data(G_OBJECT(m->window), "host-tree", tree);
    const struct { const char *title; gint column; } columns[] = {
        { "Hostname", HOST_COL_NAME }, { "Sources", HOST_COL_SOURCES }, { "State", HOST_COL_STATE }
    };
    for (guint i = 0; i < G_N_ELEMENTS(columns); ++i) {
        GtkCellRenderer *r = gtk_cell_renderer_text_new();
        gtk_tree_view_append_column(GTK_TREE_VIEW(tree),
            gtk_tree_view_column_new_with_attributes(columns[i].title, r, "text", columns[i].column, NULL));
    }
    GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(tree));
    gtk_tree_selection_set_mode(selection, GTK_SELECTION_SINGLE);
    g_signal_connect(selection, "changed", G_CALLBACK(on_host_selected), m);
    GtkWidget *left = gtk_scrolled_window_new(NULL, NULL);
    gtk_style_context_add_class(gtk_widget_get_style_context(left), "session-dialog");
    gtk_container_add(GTK_CONTAINER(left), tree);
    gtk_paned_pack1(GTK_PANED(paned), left, TRUE, FALSE);

    m->details = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(m->details), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(m->details), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(m->details), TRUE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(m->details), GTK_WRAP_WORD_CHAR);
    GtkWidget *right = gtk_scrolled_window_new(NULL, NULL);
    gtk_style_context_add_class(gtk_widget_get_style_context(right), "session-dialog");
    gtk_container_add(GTK_CONTAINER(right), m->details);
    gtk_paned_pack2(GTK_PANED(paned), right, TRUE, FALSE);
    gtk_paned_set_position(GTK_PANED(paned), 370);

    GtkWidget *actions = gtk_button_box_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_button_box_set_layout(GTK_BUTTON_BOX(actions), GTK_BUTTONBOX_END);
    gtk_box_set_spacing(GTK_BOX(actions), 8);
    gtk_box_pack_start(GTK_BOX(root), actions, FALSE, FALSE, 0);
    GtkWidget *reload = gtk_button_new_with_label("Refresh Display");
    gtk_container_add(GTK_CONTAINER(actions), reload);
    g_signal_connect(reload, "clicked", G_CALLBACK(on_reload), m);
    GtkWidget *close = gtk_button_new_with_label("Close");
    gtk_container_add(GTK_CONTAINER(actions), close);
    g_signal_connect_swapped(close, "clicked", G_CALLBACK(gtk_widget_destroy), m->window);
    gtk_widget_show_all(m->window);
    host_manager_reload(m);
}
