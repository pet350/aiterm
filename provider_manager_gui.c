// Part of the AITerm project
// provider_manager_gui.c
// AI Provider Manager GUI
// 0.9.11-alpha

#include <gtk/gtk.h>
#include <string.h>

#include "gui.h"
#include "openai.h"
#include "gemini.h"
#include "terminal.h"
#include "provider_manager_gui.h"
#include "ai_provider.h"
#include "config.h"
#include "crypto.h"
#include "utils.h"

static const char *provider_names[] = {
    "openai", "gemini", "groq", "openrouter", "mistral", "ollama", "custom", NULL
};

static void set_entry(GtkWidget *entry, const char *value) {
    gtk_entry_set_text(GTK_ENTRY(entry), value ? value : "");
}

static const char *combo_text(GtkWidget *combo) {
    return gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(combo));
}

static void set_protocol(ProviderManagerDialog *dlg, ProviderKind kind) {
    gtk_combo_box_set_active(GTK_COMBO_BOX(dlg->protocol_combo),
                             kind == PROVIDER_KIND_GEMINI_GENERATE ? 1 : 0);
}

static void fill_from_provider(ProviderManagerDialog *dlg, const char *name) {
    if (!dlg || !name) return;

    const char *model = "";
    const char *base_url = "";
    const char *endpoint = "chat/completions";
    const char *auth_header = "Authorization";
    const char *auth_scheme = "Bearer";
    const char *query_key = "key";
    ProviderKind kind = PROVIDER_KIND_OPENAI_CHAT;
    gboolean key_in_query = FALSE;

    if (strcasecmp(name, "gemini") == 0) {
        model = "gemini-flash-latest";
        base_url = "https://generativelanguage.googleapis.com/v1beta";
        endpoint = "models/%s:generateContent";
        auth_header = "";
        auth_scheme = "";
        kind = PROVIDER_KIND_GEMINI_GENERATE;
        key_in_query = TRUE;
    } else if (strcasecmp(name, "groq") == 0) {
        model = "llama-3.3-70b-versatile";
        base_url = "https://api.groq.com/openai/v1";
    } else if (strcasecmp(name, "openrouter") == 0) {
        model = "openai/gpt-oss-20b:free";
        base_url = "https://openrouter.ai/api/v1";
    } else if (strcasecmp(name, "mistral") == 0) {
        model = "mistral-small-latest";
        base_url = "https://api.mistral.ai/v1";
    } else if (strcasecmp(name, "ollama") == 0) {
        model = "llama3.2";
        base_url = "http://127.0.0.1:11434/v1";
        auth_header = "";
        auth_scheme = "";
    } else if (strcasecmp(name, "custom") == 0) {
        model = "";
        base_url = "http://127.0.0.1:8000/v1";
    } else {
        model = OPENAI_MODEL;
        base_url = "https://api.openai.com/v1";
    }

    set_entry(dlg->provider_name, name);
    set_entry(dlg->model_entry, model);
    set_entry(dlg->base_url_entry, base_url);
    set_entry(dlg->endpoint_entry, endpoint);
    set_entry(dlg->auth_header_entry, auth_header);
    set_entry(dlg->auth_scheme_entry, auth_scheme);
    set_entry(dlg->api_key_entry, get_provider_api_key(dlg->app, name));
    set_entry(dlg->query_key_entry, query_key);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(dlg->api_key_query_check), key_in_query);
    set_protocol(dlg, kind);
}
static void on_provider_combo_changed(GtkComboBox *combo, gpointer user_data) {
    ProviderManagerDialog *dlg = user_data;
    gchar *name = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(combo));
    if (name) {
        fill_from_provider(dlg, name);
        g_free(name);
    }
}

static gboolean apply_provider(ProviderManagerDialog *dlg, gboolean save) {
    AppContext *app = dlg->app;
    const char *name = gtk_entry_get_text(GTK_ENTRY(dlg->provider_name));
    const char *model = gtk_entry_get_text(GTK_ENTRY(dlg->model_entry));
    const char *base_url = gtk_entry_get_text(GTK_ENTRY(dlg->base_url_entry));
    const char *endpoint = gtk_entry_get_text(GTK_ENTRY(dlg->endpoint_entry));
    const char *auth_header = gtk_entry_get_text(GTK_ENTRY(dlg->auth_header_entry));
    const char *auth_scheme = gtk_entry_get_text(GTK_ENTRY(dlg->auth_scheme_entry));
    const char *api_key = gtk_entry_get_text(GTK_ENTRY(dlg->api_key_entry));
    const char *query_key = gtk_entry_get_text(GTK_ENTRY(dlg->query_key_entry));
    const char *protocol = combo_text(dlg->protocol_combo);

    if (!name || !*name || !model || !*model || !base_url || !*base_url ||
        !endpoint || !*endpoint) {
        gtk_label_set_text(GTK_LABEL(dlg->status_label),
                           "Provider, model, base URL and endpoint are required.");
        return FALSE;
    }

    ProviderConfig *p = &app->provider_config;
    free_provider_config(p);
    p->provider = g_strdup(name);
    p->name = g_strdup(name);
    p->model = g_strdup(model);
    p->base_url = g_strdup(base_url);
    p->endpoint = g_strdup(endpoint);
    p->auth_header = (*auth_header) ? g_strdup(auth_header) : NULL;
    p->auth_scheme = (*auth_scheme) ? g_strdup(auth_scheme) : NULL;
    p->query_key_name = (*query_key) ? g_strdup(query_key) : NULL;
    p->api_key_in_query = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(dlg->api_key_query_check));
    p->kind = (protocol && strcmp(protocol, "Gemini generateContent") == 0)
                  ? PROVIDER_KIND_GEMINI_GENERATE : PROVIDER_KIND_OPENAI_CHAT;
    p->api_key = (*api_key) ? g_strdup(api_key) : NULL;

    /* 0.9.11-alpha: keep a separate credential in SecurityConfig for every provider. */
    set_provider_api_key(app, name, api_key);
    g_free(app->aiterm_runtime.model);
    app->aiterm_runtime.model = g_strdup(model);

    if (save) {
        save_config(app);
        gtk_label_set_text(GTK_LABEL(dlg->status_label),
                           "Provider applied and configuration saved.");
    } else {
        gtk_label_set_text(GTK_LABEL(dlg->status_label), "Provider applied.");
    }
    return TRUE;
}

static void on_apply_clicked(GtkWidget *button, gpointer user_data) {
    apply_provider(user_data, TRUE);
}

static void on_test_clicked(GtkWidget *button, gpointer user_data) {
    ProviderManagerDialog *dlg = user_data;
    if (!apply_provider(dlg, FALSE)) return;

    gtk_label_set_text(GTK_LABEL(dlg->status_label), "Testing provider connection...");
    while (gtk_events_pending()) gtk_main_iteration();

    char *raw = ai_provider_send(dlg->app,
        "Reply with exactly: AITerm provider connection OK");
    if (!raw) {
        gtk_label_set_text(GTK_LABEL(dlg->status_label),
                           "Provider test failed. Check endpoint, model, key, and network.");
        return;
    }

    char *text = ai_provider_extract_text(raw);
    if (text && *text) {
        char *status = g_strdup_printf("Connection OK: %s", text);
        gtk_label_set_text(GTK_LABEL(dlg->status_label), status);
        g_free(status);
    } else {
        gtk_label_set_text(GTK_LABEL(dlg->status_label),
                           "HTTP request succeeded, but no assistant text was returned.");
    }
    g_free(text);
    g_free(raw);
}

static void on_dialog_destroy(GtkWidget *widget, gpointer user_data) {
    ProviderManagerDialog *dlg = user_data;
    if (dlg && dlg->app) dlg->app->manager.provider = NULL;
    g_free(dlg);
}

void close_provider_manager(AppContext *app) {
    if (app && app->manager.provider) {
        gtk_widget_destroy(app->manager.provider);
        app->manager.provider = NULL;
    }
}

static GtkWidget *make_labeled_entry(GtkWidget *grid, int row, const char *label_text,
                                     GtkWidget **entry, gboolean password) {
    GtkWidget *label = gtk_label_new(label_text);
    gtk_widget_set_halign(label, GTK_ALIGN_END);
    *entry = gtk_entry_new();
    if (password) gtk_entry_set_visibility(GTK_ENTRY(*entry), FALSE);
    gtk_grid_attach(GTK_GRID(grid), label, 0, row, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), *entry, 1, row, 1, 1);
    return *entry;
}

void open_provider_manager_window(AppContext *app) {
    if (!app) return;
    if (app->manager.provider) {
        gtk_window_present(GTK_WINDOW(app->manager.provider));
        return;
    }

    ProviderManagerDialog *dlg = g_new0(ProviderManagerDialog, 1);
    dlg->app = app;
    dlg->dialog = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    app->manager.provider = dlg->dialog;

    gtk_window_set_title(GTK_WINDOW(dlg->dialog), "AI Provider Manager");
    gtk_window_set_default_size(GTK_WINDOW(dlg->dialog), 760, 560);
    gtk_window_set_position(GTK_WINDOW(dlg->dialog), GTK_WIN_POS_CENTER);

    GtkStyleContext *ctx = gtk_widget_get_style_context(dlg->dialog);
    gtk_style_context_add_class(ctx, "session-dialog");

    GtkWidget *outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(outer), 14);
    gtk_container_add(GTK_CONTAINER(dlg->dialog), outer);

    GtkWidget *title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title), "<b>AI Provider Manager</b>");
    gtk_widget_set_halign(title, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(outer), title, FALSE, FALSE, 0);

    GtkWidget *info = gtk_label_new(
        "Select a provider, edit its connection details, test it, then Apply & Save.\n"
        "Each provider keeps its own encrypted API key, so providers can be swapped without re-entering credentials.\n"
        "Custom entries can point at any OpenAI-compatible or Gemini-compatible endpoint.");
    gtk_label_set_xalign(GTK_LABEL(info), 0.0);
    gtk_label_set_line_wrap(GTK_LABEL(info), TRUE);
    gtk_box_pack_start(GTK_BOX(outer), info, FALSE, FALSE, 0);

    GtkWidget *selector_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_pack_start(GTK_BOX(outer), selector_box, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(selector_box), gtk_label_new("Provider Preset:"), FALSE, FALSE, 0);

    dlg->provider_combo = gtk_combo_box_text_new();
    for (int i = 0; provider_names[i]; i++)
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(dlg->provider_combo), provider_names[i]);
    gtk_box_pack_start(GTK_BOX(selector_box), dlg->provider_combo, FALSE, FALSE, 0);

    GtkWidget *frame = gtk_frame_new("Provider Configuration");
    gtk_box_pack_start(GTK_BOX(outer), frame, TRUE, TRUE, 0);
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 10);
    gtk_container_set_border_width(GTK_CONTAINER(grid), 12);
    gtk_container_add(GTK_CONTAINER(frame), grid);

    make_labeled_entry(grid, 0, "Provider Name:", &dlg->provider_name, FALSE);
    make_labeled_entry(grid, 1, "Model:", &dlg->model_entry, FALSE);
    make_labeled_entry(grid, 2, "Base URL:", &dlg->base_url_entry, FALSE);
    make_labeled_entry(grid, 3, "Endpoint:", &dlg->endpoint_entry, FALSE);
    make_labeled_entry(grid, 4, "Auth Header:", &dlg->auth_header_entry, FALSE);
    make_labeled_entry(grid, 5, "Auth Scheme:", &dlg->auth_scheme_entry, FALSE);
    make_labeled_entry(grid, 6, "API Key:", &dlg->api_key_entry, TRUE);
    make_labeled_entry(grid, 7, "Query Key:", &dlg->query_key_entry, FALSE);

    GtkWidget *protocol_label = gtk_label_new("Protocol:");
    gtk_widget_set_halign(protocol_label, GTK_ALIGN_END);
    dlg->protocol_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(dlg->protocol_combo),
                                   "OpenAI Chat Completions");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(dlg->protocol_combo),
                                   "Gemini generateContent");
    gtk_combo_box_set_active(GTK_COMBO_BOX(dlg->protocol_combo), 0);
    gtk_grid_attach(GTK_GRID(grid), protocol_label, 0, 8, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), dlg->protocol_combo, 1, 8, 1, 1);

    dlg->api_key_query_check = gtk_check_button_new_with_label("Send API key as query parameter");
    gtk_grid_attach(GTK_GRID(grid), dlg->api_key_query_check, 1, 9, 1, 1);

    dlg->status_label = gtk_label_new("Ready.");
    gtk_label_set_xalign(GTK_LABEL(dlg->status_label), 0.0);
    gtk_label_set_line_wrap(GTK_LABEL(dlg->status_label), TRUE);
    gtk_box_pack_start(GTK_BOX(outer), dlg->status_label, FALSE, FALSE, 0);

    GtkWidget *buttons = gtk_button_box_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_button_box_set_layout(GTK_BUTTON_BOX(buttons), GTK_BUTTONBOX_END);
    gtk_box_set_spacing(GTK_BOX(buttons), 8);
    gtk_box_pack_start(GTK_BOX(outer), buttons, FALSE, FALSE, 0);

    GtkWidget *test = gtk_button_new_with_label("Test Connection");
    GtkWidget *apply = gtk_button_new_with_label("Apply & Save");
    GtkWidget *close = gtk_button_new_with_label("Close");
    gtk_container_add(GTK_CONTAINER(buttons), test);
    gtk_container_add(GTK_CONTAINER(buttons), apply);
    gtk_container_add(GTK_CONTAINER(buttons), close);

    g_signal_connect(dlg->provider_combo, "changed", G_CALLBACK(on_provider_combo_changed), dlg);
    g_signal_connect(test, "clicked", G_CALLBACK(on_test_clicked), dlg);
    g_signal_connect(apply, "clicked", G_CALLBACK(on_apply_clicked), dlg);
    g_signal_connect_swapped(close, "clicked", G_CALLBACK(gtk_widget_destroy), dlg->dialog);
    g_signal_connect(dlg->dialog, "destroy", G_CALLBACK(on_dialog_destroy), dlg);

    const char *current = app->provider_config.provider ? app->provider_config.provider : "openai";
    int active = 0;
    gboolean found = FALSE;
    for (int i = 0; provider_names[i]; i++) {
        if (strcasecmp(current, provider_names[i]) == 0) { active = i; found = TRUE; break; }
    }
    if (!found) {
        /* Preserve an arbitrary provider name as a selectable custom entry. */
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(dlg->provider_combo), current);
        active = 7;
    }
    gtk_combo_box_set_active(GTK_COMBO_BOX(dlg->provider_combo), active);
    fill_from_provider(dlg, found ? provider_names[active] : current);

    /* Restore the actual current configuration into the fields, including
     * any custom URL/endpoint values saved by the manager. */
    set_entry(dlg->provider_name, app->provider_config.provider);
    set_entry(dlg->model_entry, app->provider_config.model);
    set_entry(dlg->base_url_entry, app->provider_config.base_url);
    set_entry(dlg->endpoint_entry, app->provider_config.endpoint);
    set_entry(dlg->auth_header_entry, app->provider_config.auth_header);
    set_entry(dlg->auth_scheme_entry, app->provider_config.auth_scheme);
    set_entry(dlg->api_key_entry, get_provider_api_key(app, app->provider_config.provider));
    set_entry(dlg->query_key_entry, app->provider_config.query_key_name);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(dlg->api_key_query_check),
                                 app->provider_config.api_key_in_query);
    set_protocol(dlg, app->provider_config.kind);

    gtk_widget_show_all(dlg->dialog);
}
