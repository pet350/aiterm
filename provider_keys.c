// Part of the aiterm project
// provider_keys.c
// API provider key reference / status window
// By: Peter Talbott
// With assistance from Gemini and OpenAI
// September 2026

#include "provider_keys.h"
#include <string.h>

static gboolean provider_key_is_configured(const ProviderKeyInfo *info)
{
    return info && info->key_ptr &&
           *info->key_ptr && (*info->key_ptr)[0] != '\0';
}

static void add_provider_row(GtkGrid *grid,
                             gint row,
                             const ProviderKeyInfo *info,
                             const char *active_provider)
{
    GtkWidget *provider_label;
    GtkWidget *config_label;
    GtkWidget *status_label;
    GtkWidget *link_button;

    gboolean configured = provider_key_is_configured(info);
    gboolean active = FALSE;

    if (active_provider && info->name) {
        active = (g_ascii_strcasecmp(active_provider, info->name) == 0);
    }

    provider_label = gtk_label_new(NULL);
    config_label = gtk_label_new(info->config_name);
    status_label = gtk_label_new(NULL);

    gtk_widget_set_halign(provider_label, GTK_ALIGN_START);
    gtk_widget_set_halign(config_label, GTK_ALIGN_START);
    gtk_widget_set_halign(status_label, GTK_ALIGN_START);

    if (active) {
        char *markup = g_markup_printf_escaped(
            "<b>%s</b>  <small>(active)</small>", info->name);
        gtk_label_set_markup(GTK_LABEL(provider_label), markup);
        g_free(markup);
    } else {
        gtk_label_set_text(GTK_LABEL(provider_label), info->name);
    }

    if (info->local_only) {
        gtk_label_set_text(
            GTK_LABEL(status_label),
            configured ? "Configured (cloud)" : "Not required (local)");
    } else {
        gtk_label_set_text(
            GTK_LABEL(status_label),
            configured ? "Configured" : "Not configured");
    }

    if (info->url && info->url[0] != '\0') {
        link_button = gtk_link_button_new_with_label(
            info->url, "Create / Manage Key");
        gtk_widget_set_halign(link_button, GTK_ALIGN_START);
    } else {
        link_button = gtk_label_new("Custom provider");
        gtk_widget_set_halign(link_button, GTK_ALIGN_START);
    }

    gtk_grid_attach(grid, provider_label, 0, row, 1, 1);
    gtk_grid_attach(grid, config_label,   1, row, 1, 1);
    gtk_grid_attach(grid, status_label,   2, row, 1, 1);
    gtk_grid_attach(grid, link_button,    3, row, 1, 1);
}

void provider_keys_show(AppContext *app)
{
    GtkWidget *dialog;
    GtkWidget *content;
    GtkWidget *title;
    GtkWidget *description;
    GtkWidget *frame;
    GtkWidget *grid;
    GtkWidget *note;
    GtkWidget *scrolled;
    GtkWidget *active_label;

    const char *active_provider = NULL;

    if (app && app->provider_config.provider) {
        active_provider = app->provider_config.provider;
    }

    ProviderKeyInfo providers[] = {
        { "OpenAI",     "OPENAI_KEY",
          "https://platform.openai.com/api-keys",
          app ? &app->security.openai_key : NULL, FALSE },

        { "Gemini",     "GEMINI_KEY",
          "https://aistudio.google.com/apikey",
          app ? &app->security.gemini_key : NULL, FALSE },

        { "Groq",       "GROQ_KEY",
          "https://console.groq.com/keys",
          app ? &app->security.groq_key : NULL, FALSE },

        { "OpenRouter", "OPENROUTER_KEY",
          "https://openrouter.ai/settings/keys",
          app ? &app->security.openrouter_key : NULL, FALSE },

        { "Mistral",    "MISTRAL_KEY",
          "https://console.mistral.ai/api-keys/",
          app ? &app->security.mistral_key : NULL, FALSE },

        { "Ollama",     "OLLAMA_KEY",
          "https://ollama.com/settings/keys",
          app ? &app->security.ollama_key : NULL, TRUE },

        { "Custom",     "CUSTOM_KEY",
          NULL,
          app ? &app->security.custom_key : NULL, FALSE }
    };

    const gint provider_count =
        (gint)(sizeof(providers) / sizeof(providers[0]));

    dialog = gtk_dialog_new_with_buttons(
        "aiTerm API Provider Keys",
        app ? GTK_WINDOW(app->gui.window) : NULL,
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        "_Close",
        GTK_RESPONSE_CLOSE,
        NULL);

    gtk_window_set_default_size(GTK_WINDOW(dialog), 850, 500);
    gtk_window_set_resizable(GTK_WINDOW(dialog), TRUE);

    // Apply "session-dialog" CSS class to the dialog window context
    GtkStyleContext *dialog_context = gtk_widget_get_style_context(dialog);
    gtk_style_context_add_class(dialog_context, "session-dialog");

    content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_container_set_border_width(GTK_CONTAINER(content), 14);
    gtk_box_set_spacing(GTK_BOX(content), 10);

    // Apply "session-dialog" CSS class to the content container context
    GtkStyleContext *content_context = gtk_widget_get_style_context(content);
    gtk_style_context_add_class(content_context, "session-dialog");

    title = gtk_label_new(NULL);
    gtk_label_set_markup(
        GTK_LABEL(title), "<span size=\"large\" weight=\"bold\">"
        "aiTerm API Provider Reference</span>");
    gtk_widget_set_halign(title, GTK_ALIGN_START);

    description = gtk_label_new(
        "Configure provider credentials in aiTerm using the encrypted "
        "provider-specific configuration entries. API key values are "
        "never displayed in this window.");
    gtk_label_set_line_wrap(GTK_LABEL(description), TRUE);
    gtk_widget_set_halign(description, GTK_ALIGN_START);

    active_label = gtk_label_new(NULL);
    if (active_provider && active_provider[0] != '\0') {
        char *markup = g_markup_printf_escaped(
            "<b>Active provider:</b> %s", active_provider);
        gtk_label_set_markup(GTK_LABEL(active_label), markup);
        g_free(markup);
    } else {
        gtk_label_set_markup(
            GTK_LABEL(active_label),
            "<b>Active provider:</b> not selected");
    }
    gtk_widget_set_halign(active_label, GTK_ALIGN_START);

    frame = gtk_frame_new(NULL);
    gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_IN);

    scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(
        GTK_SCROLLED_WINDOW(scrolled),
        GTK_POLICY_AUTOMATIC,
        GTK_POLICY_AUTOMATIC);

    grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 16);
    gtk_container_set_border_width(GTK_CONTAINER(grid), 10);

    {
        GtkWidget *header_provider = gtk_label_new(NULL);
        GtkWidget *header_config = gtk_label_new(NULL);
        GtkWidget *header_status = gtk_label_new(NULL);
        GtkWidget *header_link = gtk_label_new(NULL);

        gtk_label_set_markup(GTK_LABEL(header_provider), "<b>Provider</b>");
        gtk_label_set_markup(GTK_LABEL(header_config),
                             "<b>Config Variable</b>");
        gtk_label_set_markup(GTK_LABEL(header_status),
                             "<b>Key Status</b>");
        gtk_label_set_markup(GTK_LABEL(header_link),
                             "<b>Key Management</b>");

        gtk_widget_set_halign(header_provider, GTK_ALIGN_START);
        gtk_widget_set_halign(header_config, GTK_ALIGN_START);
        gtk_widget_set_halign(header_status, GTK_ALIGN_START);
        gtk_widget_set_halign(header_link, GTK_ALIGN_START);

        gtk_grid_attach(GTK_GRID(grid), header_provider, 0, 0, 1, 1);
        gtk_grid_attach(GTK_GRID(grid), header_config,   1, 0, 1, 1);
        gtk_grid_attach(GTK_GRID(grid), header_status,   2, 0, 1, 1);
        gtk_grid_attach(GTK_GRID(grid), header_link,     3, 0, 1, 1);
    }

    for (gint i = 0; i < provider_count; ++i) {
        add_provider_row(GTK_GRID(grid), i + 1, &providers[i],
                         active_provider);
    }

    gtk_container_add(GTK_CONTAINER(scrolled), grid);
    gtk_container_add(GTK_CONTAINER(frame), scrolled);

    note = gtk_label_new(
        "Ollama note: local Ollama normally does not require an API key. "
        "OLLAMA_KEY is relevant when using Ollama's cloud service. "
        "The Custom provider does not have a predefined key-management URL.");
    gtk_label_set_line_wrap(GTK_LABEL(note), TRUE);
    gtk_widget_set_halign(note, GTK_ALIGN_START);

    gtk_box_pack_start(GTK_BOX(content), title,        FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(content), description,  FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(content), active_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(content), frame,        TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(content), note,         FALSE, FALSE, 0);

    gtk_widget_show_all(dialog);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

